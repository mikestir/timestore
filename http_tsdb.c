#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <inttypes.h>
#include <time.h>
#include <math.h>

#include "tsdb.h"
#include "cJSON/cJSON.h"

#include "http.h"
#include "http_tsdb.h"
#include "logging.h"
#include "profile.h"

#define MIME_TYPE		"application/json"

#define SCN_NODE		("/nodes/%" SCNx64)
#define SCN_NODE_TIMESTAMP	("/nodes/%" SCNx64 "/values/%" SCNi64)
#define SCN_NODE_METRIC		("/nodes/%" SCNx64 "/series/%u")
/*! FIXME: Location: URLs are supposed to be absolute.  We need to determine at runtime the
 * correct values for scheme, host and port */
#define PRI_NODE		("http://127.0.0.1:8080/nodes/%016" PRIx64)
#define PRI_NODE_TIMESTAMP	("http://127.0.0.1:8080/nodes/%016" PRIx64 "/values/%" PRIi64)
#define PRI_NODE_METRIC		("http://127.0.0.1:8080/nodes/%016" PRIx64 "/series/%u")

/*! Maximum length of output buffer for Location and MIME headers */
#define MAX_HEADER_STRING	128

/*! Default number of points to return in a series */
/* FIXME: Make this runtime configurable */
#define DEFAULT_SERIES_NPOINTS	24

static int post_values_value_parser(cJSON *json, unsigned int *nmetrics, double *values)
{
	cJSON *subitem = json->child;
	
	FUNCTION_TRACE;
	
	while (subitem) {
		if (*nmetrics == TSDB_MAX_METRICS) {
			ERROR("Maximum number of metrics exceeded\n");
			return -EINVAL;
		}
		switch (subitem->type) {
			case cJSON_NULL:
				values[*nmetrics] = NAN;
				break;
			case cJSON_Number:
				values[*nmetrics] = subitem->valuedouble;
				break;
			default:
				ERROR("values must be numeric or null\n");
				return -EINVAL;
		}
		(*nmetrics)++;
		subitem = subitem->next;
	}
	DEBUG("found values for %u metrics\n", *nmetrics);
	return 0;
}

static int post_values_data_parser(cJSON *json, int64_t *timestamp, unsigned int *nmetrics, double *values)
{
	cJSON *subitem = json->child;
	
	FUNCTION_TRACE;
	
	*nmetrics = 0;
	for ( ; subitem; subitem = subitem->next) {
		if (subitem->string == NULL)
			continue;

		/* Look for items of interest - we don't recurse except to parse the
		 * value array */
		if (strcmp(subitem->string, "timestamp") == 0) {
			if (subitem->type != cJSON_Number) {
				ERROR("timestamp must be numeric\n");
				return -EINVAL;
			}
#ifdef HTTP_DENY_FUTURE_POST
			if (subitem->valuedouble / 1000.0 > *timestamp) {
				ERROR("timestamp in the future is forbidden\n");
				return -EACCES;
			}
#endif
			*timestamp = subitem->valuedouble / 1000.0;
		} else if (strcmp(subitem->string, "values") == 0) {
			if (post_values_value_parser(subitem, nmetrics, values) < 0) {
				return -EINVAL;
			}
		}	
	}
	return 0;
}

static int put_node_metrics_parser(cJSON *json, unsigned int *nmetrics,
	tsdb_pad_mode_t *pad_mode, tsdb_downsample_mode_t *ds_mode)
{
	cJSON *subitem = json->child;
	cJSON *subitem2;
	
	FUNCTION_TRACE;
	
	*nmetrics = 0;
	for ( ; subitem; subitem = subitem->next) {
		if (*nmetrics == TSDB_MAX_METRICS) {
			ERROR("Maximum number of metrics exceeded\n");
			return -EINVAL;
		}
		
		/* Defaults */
		pad_mode[*nmetrics] = tsdbPad_Unknown;
		ds_mode[*nmetrics] = tsdbDownsample_Mean;
		
		/* Parse for anything specified in the object */
		subitem2 = subitem->child;
		for ( ; subitem2; subitem2 = subitem2->next) {
			if (subitem2->string == NULL)
				continue;
			if (strcmp(subitem2->string, "pad_mode") == 0) {
				if (subitem2->type != cJSON_Number) {
					ERROR("pad_mode must be numeric\n");
					return -EINVAL;
				}
				if (subitem2->valueint < 0 || subitem2->valueint > ((1 << TSDB_PAD_MASK) - 1)) {
					ERROR("pad_mode out of range\n");
					return -EINVAL;
				}
				pad_mode[*nmetrics] = (tsdb_pad_mode_t)subitem2->valueint;
				DEBUG("metric %u pad_mode %d\n", *nmetrics, pad_mode[*nmetrics]);
			} else if (strcmp(subitem2->string, "downsample_mode") == 0) {
				if (subitem2->type != cJSON_Number) {
					ERROR("downsample_mode must be numeric\n");
					return -EINVAL;
				}
				if (subitem2->valueint < 0 || subitem2->valueint > ((1 << TSDB_DOWNSAMPLE_MASK) - 1)) {
					ERROR("downsample_mode out of range\n");
					return -EINVAL;
				}
				ds_mode[*nmetrics] = (tsdb_downsample_mode_t)subitem2->valueint;
				DEBUG("metric %u ds_mode %d\n", *nmetrics, ds_mode[*nmetrics]);
			}
		}
		(*nmetrics)++;
	}
	DEBUG("found definitions for %u metrics\n", *nmetrics);
	return 0;
}

static int put_node_decimation_parser(cJSON *json, unsigned int *decimation)
{
	int ndecimation = 0;
	cJSON *subitem = json->child;
	
	FUNCTION_TRACE;
	
	for ( ; subitem; subitem = subitem->next) {
		if (ndecimation == TSDB_MAX_LAYERS) {
			ERROR("Maximum number of layers exceeded\n");
			return -EINVAL;
		}
		switch (subitem->type) {
			case cJSON_Number:
				if (subitem->valueint < 0) {
					ERROR("decimation values must be positive\n");
					return -EINVAL;
				}
				decimation[ndecimation++] = (unsigned int)subitem->valueint;
				DEBUG("layer %u decimation %u\n", ndecimation, decimation[ndecimation - 1]);
				break;
			default:
				ERROR("decimation values must be numeric\n");
				return -EINVAL;
		}
	}
	return 0;	
}

static int put_node_data_parser(cJSON *json, unsigned int *interval,
	unsigned int *nmetrics, tsdb_pad_mode_t *pad_mode, tsdb_downsample_mode_t *ds_mode,
	unsigned int *decimation)
{
	cJSON *subitem = json->child;
	
	FUNCTION_TRACE;
	
	for ( ; subitem; subitem = subitem->next) {
		if (subitem->string == NULL)
			continue;
		
		if (strcmp(subitem->string, "interval") == 0) {
			if (subitem->type != cJSON_Number) {
				ERROR("interval must be numeric\n");
				return -EINVAL;
			}
			if (subitem->valueint < 0) {
				ERROR("interval must be positive\n");
				return -EINVAL;
			}
			*interval = (unsigned int)subitem->valueint;
			DEBUG("interval = %u\n", *interval);
		} else if (strcmp(subitem->string, "decimation") == 0) {
			if (put_node_decimation_parser(subitem, decimation) < 0) {
				return -EINVAL;
			}
		} else if (strcmp(subitem->string, "metrics") == 0) {
			if (put_node_metrics_parser(subitem, nmetrics, pad_mode, ds_mode) < 0) {
				return -EINVAL;
			}
		}
	}
	return 0;
}
	
HTTP_HANDLER(http_tsdb_get_nodes)
{
	FUNCTION_TRACE;
	
	ERROR("http_tsdb_get_nodes not supported yet\n");
	
	/* Not supported - returns 404 */
	return MHD_HTTP_NOT_FOUND;
}

HTTP_HANDLER(http_tsdb_get_node)
{
	tsdb_ctx_t *db;
	cJSON *json, *metrics, *metric;
	uint64_t node_id;
	int n, nlayers;
	
	FUNCTION_TRACE;
	
	/* Extract node ID from the URL */
	if (sscanf(url, SCN_NODE, &node_id) != 1) {
		/* If the node ID part of the URL doesn't decode then treat as a 404 */
		ERROR("Invalid node\n");
		return MHD_HTTP_NOT_FOUND;
	}
	
	/* Attempt to open specified node - do not create if it doesn't exist */
	db = tsdb_open(node_id);
	if (db == NULL) {
		ERROR("Invalid node\n");
		return MHD_HTTP_NOT_FOUND;
	}
	
	/* Encode the response record */
	json = cJSON_CreateObject();
	cJSON_AddNumberToObject(json, "interval", db->meta->interval);
	/* The next two are not in the spec but could be useful */
	cJSON_AddNumberToObject(json, "start", (double)db->meta->start_time * 1000.0);
	cJSON_AddNumberToObject(json, "npoints", db->meta->npoints);
	for (nlayers = 0; nlayers < TSDB_MAX_LAYERS; nlayers++) {
		if (db->meta->decimation[nlayers] == 0)
			break;
	}
	cJSON_AddItemToObject(json, "decimation", cJSON_CreateIntArray((int*)db->meta->decimation, nlayers));
	metrics = cJSON_CreateArray();
	for (n = 0; n < db->meta->nmetrics; n++) {
		metric = cJSON_CreateObject();
		cJSON_AddNumberToObject(metric, "pad_mode", (db->meta->flags[n] >> TSDB_PAD_SHIFT) & TSDB_PAD_MASK);
		cJSON_AddNumberToObject(metric, "downsample_mode", (db->meta->flags[n] >> TSDB_DOWNSAMPLE_SHIFT) & TSDB_DOWNSAMPLE_MASK);
		cJSON_AddItemToArray(metrics, metric);
	}
	cJSON_AddItemToObject(json, "metrics", metrics);	
	tsdb_close(db);	
	
	/* Pass response back to handler and set MIME type */
	*resp_data = cJSON_Print(json);
	cJSON_Delete(json);
	DEBUG("JSON: %s\n", *resp_data);
	*resp_data_size = strlen(*resp_data);
	*mime_type = strdup(MIME_TYPE);
	return MHD_HTTP_OK;
}

HTTP_HANDLER(http_tsdb_create_node)
{
	uint64_t node_id;
	unsigned int interval = 0;
	unsigned int nmetrics = 0;
	unsigned int decimation[TSDB_MAX_LAYERS] = {0};
	tsdb_pad_mode_t pad_mode[TSDB_MAX_METRICS];
	tsdb_downsample_mode_t ds_mode[TSDB_MAX_METRICS];
	cJSON *json;
	int rc;
	
	FUNCTION_TRACE;
	
	/* Extract node ID from the URL */
	if (sscanf(url, SCN_NODE, &node_id) != 1) {
		/* If the node ID part of the URL doesn't decode then treat as a 404 */
		ERROR("Invalid node\n");
		return MHD_HTTP_NOT_FOUND;
	}	
	
	/* Parse payload - returns 400 Bad Request on syntax error */
	json = cJSON_Parse(req_data);
	if (!json || (rc = put_node_data_parser(json, &interval, &nmetrics, pad_mode, ds_mode, decimation))) {
		ERROR("JSON error: %d\n", rc);
		return (rc == -EACCES) ? MHD_HTTP_FORBIDDEN : MHD_HTTP_BAD_REQUEST;
	}
	cJSON_Delete(json);
	
	/* Check for missing mandatory arguments */
	if (interval == 0 || nmetrics == 0) {
		ERROR("Missing mandatory arguments\n");
		return MHD_HTTP_BAD_REQUEST;
	}
	
	/* Create the TSDB */
	if (tsdb_create(node_id, interval, nmetrics, pad_mode, ds_mode, decimation) < 0) {
		ERROR("Error creating new database (probably exists)\n");
		return MHD_HTTP_FORBIDDEN;
	}
	
 	return MHD_HTTP_OK;
}

HTTP_HANDLER(http_tsdb_delete_node)
{
	uint64_t node_id;
	
	FUNCTION_TRACE;
#ifdef HTTP_TSDB_ENABLE_DELETE	
	/* Extract node ID from the URL */
	if (sscanf(url, SCN_NODE, &node_id) != 1) {
		/* If the node ID part of the URL doesn't decode then treat as a 404 */
		ERROR("Invalid node\n");
		return MHD_HTTP_NOT_FOUND;
	}
	
	if (tsdb_delete(node_id) < 0) {
		ERROR("Deletion failed\n");
		return MHD_HTTP_NOT_FOUND;
	}
	
	return MHD_HTTP_OK;
#else
	ERROR("Node deletion is disabled\n");
	return MHD_HTTP_FORBIDDEN;
#endif
}

HTTP_HANDLER(http_tsdb_redirect_latest)
{
	tsdb_ctx_t *db;
	int64_t timestamp;
	uint64_t node_id;
	
	FUNCTION_TRACE;
	
	/* Extract node ID from the URL */
	if (sscanf(url, SCN_NODE, &node_id) != 1) {
		/* If the node ID part of the URL doesn't decode then treat as a 404 */
		ERROR("Invalid node\n");
		return MHD_HTTP_NOT_FOUND;
	}
	
	/* Attempt to open specified node - do not create if it doesn't exist */
	db = tsdb_open(node_id);
	if (db == NULL) {
		ERROR("Invalid node\n");
		return MHD_HTTP_NOT_FOUND;
	}
	
	/* Get latest time point */
	timestamp = tsdb_get_latest(db);
	tsdb_close(db);
	
	if (timestamp == TSDB_NO_TIMESTAMP) {
		/* Special case - no points in database */
		return MHD_HTTP_NOT_FOUND;
	}
	
	/* Set Location: header to redirect to specific URL */
	*location = (char*)malloc(MAX_HEADER_STRING);
	if (*location == NULL) {
		CRITICAL("Out of memory\n");
		return MHD_HTTP_INTERNAL_SERVER_ERROR;
	}
	snprintf(*location, MAX_HEADER_STRING, PRI_NODE_TIMESTAMP, node_id, timestamp);
	return MHD_HTTP_FOUND;
}

HTTP_HANDLER(http_tsdb_post_values)
{
	tsdb_ctx_t *db;
	cJSON *json;
	uint64_t node_id;
	unsigned int nmetrics;
	int64_t timestamp;
	double values[TSDB_MAX_METRICS];
	int rc;
	
	FUNCTION_TRACE;
	
	/* Extract node ID from the URL */
	if (sscanf(url, SCN_NODE, &node_id) != 1) {
		/* If the node ID part of the URL doesn't decode then treat as a 404 */
		ERROR("Invalid node\n");
		return MHD_HTTP_NOT_FOUND;
	}
	
	/* Submission timestamp defaults to current time */
	timestamp = (int64_t)time(NULL);
	
	/* Parse payload - returns 400 Bad Request on syntax error */
	json = cJSON_Parse(req_data);
	if (!json || (rc = post_values_data_parser(json, &timestamp, &nmetrics, values))) {
		ERROR("JSON error: %d\n", rc);
		return (rc == -EACCES) ? MHD_HTTP_FORBIDDEN : MHD_HTTP_BAD_REQUEST;
	}
	cJSON_Delete(json);
	
	INFO("POST point for %016" PRIx64 " at %" PRIi64 " for %u metrics\n", node_id, timestamp, nmetrics);

	/* Open specified node and validate new values */
	db = tsdb_open(node_id);
	if (db == NULL) {
		ERROR("Invalid node\n");
		return MHD_HTTP_NOT_FOUND;
	}
	if (nmetrics != db->meta->nmetrics) {
		ERROR("Incorrect number of metrics provided (got %u, expected %" PRIu32 ")\n", nmetrics, db->meta->nmetrics);
		tsdb_close(db);
		return MHD_HTTP_BAD_REQUEST;
	}
	
	/* Update the database */
	if ((rc = tsdb_update_values(db, &timestamp, values)) < 0) {
		/* -ENOENT returned if timestamp is before the start of the database */
		ERROR("Update failed\n");
		tsdb_close(db);
		return (rc == -ENOENT) ? MHD_HTTP_BAD_REQUEST : MHD_HTTP_INTERNAL_SERVER_ERROR;
	}
	tsdb_close(db);
	
	/* Set Location: header to redirect to specific URL */
	*location = (char*)malloc(MAX_HEADER_STRING);
	if (*location == NULL) {
		CRITICAL("Out of memory\n");
		return MHD_HTTP_INTERNAL_SERVER_ERROR;
	}
	snprintf(*location, MAX_HEADER_STRING, PRI_NODE_TIMESTAMP, node_id, timestamp);	
	return MHD_HTTP_CREATED;
}

HTTP_HANDLER(http_tsdb_get_values)
{
	tsdb_ctx_t *db;
	int64_t timestamp, timestamp_orig;
	uint64_t node_id;
	double values[TSDB_MAX_METRICS];
	cJSON *json;	
	int rc;
	
	FUNCTION_TRACE;
	
	/* Extract node ID and timestamp from the URL */
	if (sscanf(url, SCN_NODE_TIMESTAMP, &node_id, &timestamp) != 2) {
		/* If the URL doesn't parse then treat as a 404 */
		ERROR("Invalid node or timestamp\n");
		return MHD_HTTP_NOT_FOUND;
	}
	timestamp_orig = timestamp;
	
	/* Attempt to open specified node - do not create if it doesn't exist */
	db = tsdb_open(node_id);
	if (db == NULL) {
		ERROR("Invalid node\n");
		return MHD_HTTP_NOT_FOUND;
	}
	
	/* Get values for the selected time point */
	if ((rc = tsdb_get_values(db, &timestamp, values)) < 0) {
		/* For an out of range time point we return a 404 */
		tsdb_close(db);
		ERROR("Fetch failed\n");
		return (rc == -ENOENT) ? MHD_HTTP_NOT_FOUND : MHD_HTTP_INTERNAL_SERVER_ERROR;
	}
	
#ifdef HTTP_TSDB_ROUND_TIMESTAMP_URLS
	/* If the timestamp needs rounding then redirect the client */
	if (timestamp != timestamp_orig) {
		tsdb_close(db);
		
		/* Set Location: header to redirect to specific URL */
		*location = (char*)malloc(MAX_HEADER_STRING);
		if (*location == NULL) {
			CRITICAL("Out of memory\n");
			return MHD_HTTP_INTERNAL_SERVER_ERROR;
		}
		snprintf(*location, MAX_HEADER_STRING, PRI_NODE_TIMESTAMP, node_id, timestamp);
		return MHD_HTTP_FOUND;
	}
#endif

	/* Encode the response record */
	json = cJSON_CreateObject();
	cJSON_AddNumberToObject(json, "timestamp", timestamp * 1000);
	cJSON_AddItemToObject(json, "values", cJSON_CreateDoubleArray(values, db->meta->nmetrics));
	tsdb_close(db);
	
	/* Pass response back to handler and set MIME type */
	*resp_data = cJSON_Print(json);
	cJSON_Delete(json);
	DEBUG("JSON: %s\n", *resp_data);
	*resp_data_size = strlen(*resp_data);
	*mime_type = strdup(MIME_TYPE);
	return MHD_HTTP_OK;
}

// FIXME: Do this more intelligently
#define BUF_SIZE	(1024 * 1024)

HTTP_HANDLER(http_tsdb_get_series)
{
	tsdb_ctx_t *db;
	const char *param;
	uint64_t node_id;
	unsigned int metric_id, npoints = DEFAULT_SERIES_NPOINTS;
	int64_t start = TSDB_NO_TIMESTAMP, end = TSDB_NO_TIMESTAMP;
	tsdb_series_point_t *points, *pointptr;
	char *outbuf, *bufptr;
	
	FUNCTION_TRACE;
	
	/* Extract node and metric IDs from the URL */
	if (sscanf(url, SCN_NODE_METRIC, &node_id, &metric_id) != 2) {
		/* If the URL doesn't parse then treat as a 404 */
		ERROR("Invalid series\n");
		return MHD_HTTP_NOT_FOUND;
	}
	
	/* Parse query parameters */
	param = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "start");
	if (param) {
		sscanf(param, "%" SCNi64, &start);
	}
	param = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "end");
	if (param) {
		sscanf(param, "%" SCNi64, &end);
	}
	param = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "npoints");
	if (param) {
		sscanf(param, "%u", &npoints);
	}
	DEBUG("start = %" PRIi64 " end = %" PRIi64 " npoints = %u\n", start, end, npoints);
	
	/* Allocate output buffer */
	pointptr = points = (tsdb_series_point_t*)malloc(sizeof(tsdb_series_point_t) * npoints);
	if (points == NULL) {
		CRITICAL("Out of memory\n");
		return MHD_HTTP_INTERNAL_SERVER_ERROR;
	}
	
	/* Fetch the requested series */
	db = tsdb_open(node_id);
	if (db == NULL) {
		ERROR("Invalid node\n");
		return MHD_HTTP_NOT_FOUND;
	}
	
	if ((npoints = tsdb_get_series(db, metric_id, start, end, npoints, 0, points)) < 0) {
		/* Will fail with -ENOENT if the metric ID is invalid - 404 */
		tsdb_close(db);
		ERROR("Fetch failed\n");
		return (npoints == -ENOENT) ? MHD_HTTP_NOT_FOUND : MHD_HTTP_INTERNAL_SERVER_ERROR;
	}
	tsdb_close(db);
	
	/* Encode the response record - this is a 2D array and easier to serialise without
	 * using CJSON */
	bufptr = outbuf = (char*)malloc(BUF_SIZE);
	if (outbuf == NULL) {
		CRITICAL("Out of memory\n");
		return MHD_HTTP_INTERNAL_SERVER_ERROR;
	}
	
	bufptr += snprintf(bufptr, outbuf + BUF_SIZE - bufptr, "[");
	while (npoints--) {
		bufptr += snprintf(bufptr, outbuf + BUF_SIZE - bufptr, "[ %" PRIi64 ", %f ]%s",
			pointptr->timestamp * 1000, /* return in ms for JavaScript */
			pointptr->value,
			npoints ? ", " : "");
		pointptr++;
	}
	bufptr += snprintf(bufptr, outbuf + BUF_SIZE - bufptr, "]");
	free(points);
	
	/* Pass response back to handler and set MIME type */
	*resp_data = outbuf;
	DEBUG("JSON: %s\n", *resp_data);
	*resp_data_size = (unsigned int)(bufptr - outbuf);
	*mime_type = strdup(MIME_TYPE);
	return MHD_HTTP_OK;
}

