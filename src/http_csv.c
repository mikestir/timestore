/*
 * Bulk import/export handlers for HTTP interface
 *
 * Copyright (C) 2012, 2013 Mike Stirling
 *
 * This file is part of TimeStore (http://www.livesense.co.uk/timestore)
 *
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <inttypes.h>
#include <time.h>
#include <math.h>

#include "tsdb.h"

#include "http.h"
#include "http_csv.h"
#include "logging.h"
#include "profile.h"

#define CONTENT_TYPE		"text/plain"

#define SCN_NODE			("/nodes/%" SCNx64)

/*! Maximum length of output buffer for Location and Content-type headers */
#define MAX_HEADER_STRING	128

HTTP_HANDLER(http_csv_get_values)
{
	return MHD_HTTP_NOT_FOUND;
}

HTTP_HANDLER(http_csv_post_values)
{
	tsdb_ctx_t *db;
	uint64_t node_id;
	char *start_ptr, *end_ptr, *eof_ptr = &req_data[req_data_size];
	char c;
	int rc, nmetrics, nrows = 0;
	int64_t timestamp;
	tsdb_data_t values[TSDB_MAX_METRICS];
	unsigned short status = MHD_HTTP_OK;
	tsdb_key_t key;
	
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

	/* Check access */
	if (tsdb_get_key(db, tsdbKey_Write, &key) == 0) {
		/* Key is set - check signature */
		if (http_check_signature(conn, (uint8_t*)key, sizeof(key),
				"POST", url, req_data, req_data_size)) {
			/* Bad signature */
			tsdb_close(db);
			return MHD_HTTP_FORBIDDEN;
		}
	}
	
	/* Decode rows (we can modify the request data since it has already been copied) */
	start_ptr = req_data;
	nmetrics = -1;
	for (end_ptr = start_ptr; end_ptr < eof_ptr; end_ptr++) {
		c = *end_ptr;
		if (c == ',' || c == '\r' || c == '\n') {
			*end_ptr = '\0';
			if (nmetrics == TSDB_MAX_METRICS) {
				ERROR("Too many metrics\n");
				status = MHD_HTTP_BAD_REQUEST;
				goto done;
			}
			if (nmetrics == -1) {
				/* Decode timestamp */
				if (sscanf(start_ptr, "%" SCNi64, &timestamp) != 1) {
					ERROR("Couldn't decode timestamp on row %d\n", nrows);
					status = MHD_HTTP_BAD_REQUEST;
					goto done;
				}
			} else {
				/* Decode value */
				const char *fmt = (sizeof(tsdb_data_t) == sizeof(float)) ? "%f" : "%lf";
				if (sscanf(start_ptr, fmt, &values[nmetrics]) != 1) {
					values[nmetrics] = NAN;
				}
			}
			nmetrics++;
			if (c != ',') {
				if (nmetrics != db->meta->nmetrics) {
					ERROR("Invalid number of metrics on row %d\n", nrows);
					status = MHD_HTTP_BAD_REQUEST;
					goto done;
				}

				/* Valid row - write to database */
				if ((rc = tsdb_update_values(db, &timestamp, values)) < 0) {
					/* -ENOENT returned if timestamp is before the start of the database */
					ERROR("Update failed\n");
					status = (rc == -ENOENT) ? MHD_HTTP_BAD_REQUEST : MHD_HTTP_INTERNAL_SERVER_ERROR;
					goto done;
				}
				nmetrics = -1;
				nrows++;

				/* Skip blank lines or second part of CR/LF pair */
				while (end_ptr[1] == '\r' || end_ptr[1] == '\n')
					end_ptr++;
			}
			start_ptr = end_ptr + 1;
		}
	}
	INFO("Imported %d rows to node %016" PRIx64 "\n", nrows, node_id);
done:
	tsdb_close(db);
	return status;
}
