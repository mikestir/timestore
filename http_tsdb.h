#ifndef HTTP_TSDB_H
#define HTTP_TSDB_H

#include "http.h"

/*! Deny posts with a future timestamp (guards against DoS by ballooning the db) */
#define HTTP_TSDB_DENY_FUTURE_POST

/*! Whether or not we do a 302 redirect when timestamp needs rounding (or just
 * return the data anyway) */
#define HTTP_TSDB_ROUND_TIMESTAMP_URLS

/*! Returns a list of hyperlinks to each registered node */
HTTP_HANDLER(http_tsdb_get_nodes);
/*! Returns metadata for a specific node */
HTTP_HANDLER(http_tsdb_get_node);
/*! Allows creation of a new node.  Metadata specified in the request. */
HTTP_HANDLER(http_tsdb_create_node);
/*! Generate an HTTP redirect to the URL of the latest time point for
 * the addressed node */
HTTP_HANDLER(http_tsdb_redirect_latest);
/*! Post an array of values to update the addressed node */
HTTP_HANDLER(http_tsdb_post_values);
/*! Return the values at the specified time point for the addressed node */
HTTP_HANDLER(http_tsdb_get_values);
/*! Return a time series on the specified metric for the addressed node */
HTTP_HANDLER(http_tsdb_get_series);

#endif

