/*
 * General purpose embedded HTTP daemon
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

#ifndef HTTP_H
#define HTTP_H

#include <microhttpd.h>

/*! 
 * \brief Type used for URL method handlers
 * \param conn			Pointer to the microhttpd connection object
 * \param url			Full request URL
 * \param content_type	Pointer to return a buffer for setting Content-type: header
 * \param location		Pointer to return a buffer for setting Location: header
 * \param req_data		Upload data 
 * \param req_data_size		Size of upload data
 * \param resp_data		Pointer to return a buffer for entity body
 * \param resp_data_size	Size of entity body
 * \param arg			Generic argument
 * \return			HTTP status code
 */
typedef unsigned short (*http_handler_t)(
	struct MHD_Connection *conn,
	const char *url,
	char **content_type,
	char **location,
	char *req_data,
	size_t req_data_size,
	char **resp_data,
	size_t *resp_data_size,
	void *arg
);

#define HTTP_HANDLER(a)		unsigned short a(\
	struct MHD_Connection *conn,\
	const char *url,\
	char **content_type,\
	char **location,\
	char *req_data,\
	size_t req_data_size,\
	char **resp_data,\
	size_t *resp_data_size,\
	void *arg)

struct http_hander;
/*! Defines entities supported by the webserver and their
 * corresponding actions */
typedef struct http_entity {
	const char 		*name;			/*< URL component string or * for wildcard */
	void			*arg;			/*< Argument passed to handler */
	http_handler_t		get_handler;		/*< Pointer to GET handler or NULL */
	http_handler_t		put_handler;		/*< Pointer to PUT handler or NULL */
	http_handler_t		post_handler;		/*< Pointer to POST handler or NULL */
	http_handler_t		delete_handler;		/*< Pointer to DELETE handler or NULL */
	
	struct http_entity	*next;			/*< Pointer to sibling entity */
	struct http_entity	*child;			/*< Pointer to child entity */
} http_entity_t;

struct MHD_Daemon* http_init(uint16_t port);
void http_destroy(struct MHD_Daemon *d);

/*!
 * \brief			Checks the request signature
 * \param conn		Pointer to microhttpd connection object
 * \param key		Pointer to MAC key
 * \param key_size	Size of MAC key in bytes
 * \param method	Pointer to method string (GET, POST, etc.)
 * \param url		Pointer to request URL part
 * \param req		Pointer to buffer containing request body, or NULL if no body
 * \param req_size	Size of request body (bytes)
 * \return			0 if signature present and valid, otherwise -1
 */
int http_check_signature(struct MHD_Connection *conn, const unsigned char *key, size_t key_size,
		const char *method, const char *url, const char *req, size_t req_size);

#endif

