/*
 * Time-series REST handlers for HTTP interface
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

#ifndef HTTP_TSDB_H
#define HTTP_TSDB_H

#include "http.h"

/*! Deny posts with a future timestamp (guards against DoS by ballooning the db) */
#define HTTP_TSDB_DENY_FUTURE_POST

/*! Enable DELETE on a node to destroy the corresponding database */
#define HTTP_TSDB_ENABLE_DELETE

/*! Whether or not we do a 302 redirect when timestamp needs rounding (or just
 * return the data anyway) */
#define HTTP_TSDB_ROUND_TIMESTAMP_URLS

/*! Returns a list of hyperlinks to each registered node */
HTTP_HANDLER(http_tsdb_get_nodes);
/*! Returns metadata for a specific node */
HTTP_HANDLER(http_tsdb_get_node);
/*! Allows creation of a new node.  Metadata specified in the request. */
HTTP_HANDLER(http_tsdb_create_node);
/*! Deletes a node */
HTTP_HANDLER(http_tsdb_delete_node);
/*! Returns access key names for a node */
HTTP_HANDLER(http_tsdb_get_keys);
/*! Returns an access key for a node */
HTTP_HANDLER(http_tsdb_get_key);
/*! Updates an access key for a node */
HTTP_HANDLER(http_tsdb_put_key);
/*! Generate an HTTP redirect to the URL of the latest time point for
 * the addressed node */
HTTP_HANDLER(http_tsdb_redirect_latest);
/*! Post an array of values to update the addressed node */
HTTP_HANDLER(http_tsdb_post_values);
/*! Return the values at the specified time point for the addressed node */
HTTP_HANDLER(http_tsdb_get_values);
/*! Return a time series on the specified metric for the addressed node */
HTTP_HANDLER(http_tsdb_get_series);

/*!
 * \brief Generate random admin key.  MUST be called during startup
 */
void http_tsdb_gen_admin_key(void);

#endif

