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

#define MIME_TYPE		"text/plain"

#define SCN_NODE		("/nodes/%" SCNx64)

/*! Maximum length of output buffer for Location and MIME headers */
#define MAX_HEADER_STRING	128

HTTP_HANDLER(http_csv_get_values)
{
	return MHD_HTTP_NOT_FOUND;
}

HTTP_HANDLER(http_csv_put_values)
{
	uint64_t node_id;
	
	FUNCTION_TRACE;
	
	/* Extract node ID from the URL */
	if (sscanf(url, SCN_NODE, &node_id) != 1) {
		/* If the node ID part of the URL doesn't decode then treat as a 404 */
		ERROR("Invalid node\n");
		return MHD_HTTP_NOT_FOUND;
	}
	
	/* Parse content for points to be added/updated */
	DEBUG("%s\n", req_data);
	
	return MHD_HTTP_OK;
}
