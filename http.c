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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <microhttpd.h>

#include "http.h"
#include "http_tsdb.h"
#include "http_csv.h"
#include "logging.h"
#include "base64.h"
#include "sha2.h"

#define DEFAULT_CONTENT_TYPE	"text/plain"
#define SERVER_NAME				"timestore/0.1 (Linux)"
#define CONNECTION_LIMIT		100
#define CONNECTION_TIMEOUT		10

typedef struct {
	char *upload_data;
	size_t upload_data_size;
} http_ctx_t;

/* Prototypes for built-in handlers */
static HTTP_HANDLER(http_redirect);
static HTTP_HANDLER(http_get_file);

/* Webserver tree spec */
static http_entity_t *http_root_entity = (http_entity_t[]){{
	.name = "root", /* Name of the root node doesn't matter */
	.get_handler = http_redirect,
	.arg = (void*)"/nodes",
	
	.child = (http_entity_t[]) {{
		.name = "nodes",
		.get_handler = http_tsdb_get_nodes,
		
		.child = (http_entity_t[]) {{			
			.name = "*", /* node id */
			.get_handler = http_tsdb_get_node,
			.put_handler = http_tsdb_create_node,
			.delete_handler = http_tsdb_delete_node,
			
			.child = (http_entity_t[]) {{
				.name = "keys",
				.get_handler = http_tsdb_get_keys,

				.child = (http_entity_t[]) {{
					.name = "*", /* key name */
					.get_handler = http_tsdb_get_key,
					.put_handler = http_tsdb_put_key,
				}},

				.next = (http_entity_t[]) {{
				.name = "values",
				.get_handler = http_tsdb_redirect_latest,
				.post_handler = http_tsdb_post_values,
				
				.child = (http_entity_t[]) {{
					.name = "*", /* timestamp */
					.get_handler = http_tsdb_get_values
				}},				
				.next = (http_entity_t[]) {{
				.name = "series",
				/* FIXME: Need links to individual series (get_handler) */
				
				.child = (http_entity_t[]) {{
					.name = "*", /* metric id */
					.get_handler = http_tsdb_get_series
				}},
				.next = (http_entity_t[]) {{
				.name = "csv",
#if 0
				.get_handler = http_csv_get_values,
#endif
				.post_handler = http_csv_post_values,
				}},
				}},
				}},
			}},
		}},
#if 0
		.next = (http_entity_t[]) {{
		.name = "test",
		.arg = (void*)"main.c",
		.get_handler = http_get_file,
		}},
#endif
	}},
}};

/*!
 * \brief Built-in redirect handler
 */
static HTTP_HANDLER(http_redirect)
{
	FUNCTION_TRACE;
	
	DEBUG("Redirect request for %s to %s\n", url, (char*)arg);
	
	/* Send back specified location in Location: header */
	*location = strdup((char*)arg);
	return MHD_HTTP_FOUND; /* 302 Found */
}

/*!
 * \brief Built-in read-only file handler
 */
static HTTP_HANDLER(http_get_file)
{
	struct stat st;
	int fd;
	FUNCTION_TRACE;
	
	DEBUG("Request for %s returns contents of file: %s\n", url, (char*)arg);
	fd = open((char*)arg, O_RDONLY);
	if (fd < 0) {
		ERROR("Open file %s failed: %s\n", (char*)arg, strerror(errno));
		return MHD_HTTP_NOT_FOUND;
	}
	if (fstat(fd, &st) < 0) {
		ERROR("Stat file %s failed: %s\n", (char*)arg, strerror(errno));
		return MHD_HTTP_NOT_FOUND;
	}
	DEBUG("Allocating %d bytes\n", (int)st.st_size);
	*resp_data = malloc(st.st_size);
	if (resp_data == NULL) {
		CRITICAL("Out of memory\n");
		close(fd);
		return MHD_HTTP_INTERNAL_SERVER_ERROR;
	}
	*resp_data_size = read(fd, *resp_data, st.st_size);
	close(fd);
	
	/* FIXME: Add content type */
	
	return MHD_HTTP_OK;
}

/*!
 * \brief Find an entity for the given URL
 * \param url_in	Target URL
 * \return		Pointer to entity descriptor or NULL if not found
 */
static http_entity_t *http_find_entity(const char *url_in)
{
	http_entity_t *ent = http_root_entity;
	char *url = strdup(url_in);
	char *needle, *haystack;
	char *saveptr;
	
	FUNCTION_TRACE;
	
	haystack = url;
	while (ent && (needle = strtok_r(haystack, "/", &saveptr))) {
		haystack = NULL;
		ent = ent->child;
		
		/* Recurse through all entities at this level */
		while (ent) {
			/* Match on equality or wildcard */
			if (strcmp(ent->name, "*") == 0 || strcasecmp(ent->name, needle) == 0) {
				break;
			}
			ent = ent->next;
		}
	}
	free(url); /* free strdup */
	return ent;
}

/*!
 * \brief Microhttpd access handler
 */
static int http_handler(void *arg,
	struct MHD_Connection *conn,
	const char *url,
	const char *method,
	const char *version,
	const char *upload_data,
	size_t *upload_data_size,
	void **ptr)
{
	struct MHD_Response *response;
	http_ctx_t *ctx;
	http_entity_t *ent;
	http_handler_t handler;
	int rc;
	/* Handler variables */
	char *content_type = NULL, *location = NULL;
	char *resp_data = NULL;
	size_t resp_data_size = 0;
	unsigned int status = MHD_HTTP_OK;
	
	FUNCTION_TRACE;
	
	/* Allocate context if required */
	if (*ptr == NULL) {
		*ptr = calloc(1, sizeof(http_ctx_t));
		if (*ptr == NULL) {
			CRITICAL("Out of memory\n");
			return MHD_NO;
		}
		return MHD_YES;
	}
	ctx = (http_ctx_t*)*ptr;
	
	/* Get POST data if present */
	if (*upload_data_size != 0) {
		char *new_buffer;

		/* Join chunks from multiple calls */
		DEBUG("Growing upload buffer from %d to %d\n", (int)ctx->upload_data_size, (int)ctx->upload_data_size + (int)*upload_data_size);
		new_buffer = (char*)realloc(ctx->upload_data, ctx->upload_data_size + *upload_data_size);
		if (new_buffer == NULL) {
			CRITICAL("Out of memory\n");
			status = MHD_HTTP_INTERNAL_SERVER_ERROR;
			goto response;
		}
		ctx->upload_data = new_buffer;
		memcpy(ctx->upload_data + ctx->upload_data_size, upload_data, *upload_data_size);
		ctx->upload_data_size += *upload_data_size;
		*upload_data_size = 0 ;
		return MHD_YES;
	}

	/* FIXME: Check Accept header (for GET) - return 406 Not Acceptable,
	 * Check Content-type header (for POST) - return 415 Unsupported Media Type */

	/* Split URL on slashes and walk the entity tree for a suitable handler */
	DEBUG("%s %s\n", method, url);
//	DEBUG("Request data:\n%.*s\n", (int)ctx->upload_data_size, ctx->upload_data);
	ent = http_find_entity(url);
	if (ent == NULL) {
		/* No such entity */
		status = MHD_HTTP_NOT_FOUND;
		goto response;
	}
	
	/* Parse method and call appropriate handler if available */
	if (strcmp(method, "GET") == 0 && ent->get_handler) {
		handler = ent->get_handler;
	} else if (strcmp(method, "PUT") == 0 && ent->put_handler) {
		handler = ent->put_handler;
	} else if (strcmp(method, "POST") == 0 && ent->post_handler) {
		handler = ent->post_handler;
	} else if (strcmp(method, "DELETE") == 0 && ent->delete_handler) {
		handler = ent->delete_handler;
	} else {
		/* Unsupported method */
		ERROR("Unsupported method %s for %s\n", method, url);
		status = MHD_HTTP_METHOD_NOT_ALLOWED;
		handler = NULL;
	}
	if (handler) {
		status = (handler)(
			conn, url, &content_type, &location,
			ctx->upload_data, ctx->upload_data_size,
			&resp_data, &resp_data_size,
			ent->arg);
	}

response:
	/* Free request context */
	if (ctx->upload_data)
		free(ctx->upload_data);
	free(ctx);
	*ptr = NULL;
	
	/* Build response */
	DEBUG("status = %u\n", status);
	response = MHD_create_response_from_buffer(resp_data_size, resp_data,
		resp_data ? MHD_RESPMEM_MUST_FREE : MHD_RESPMEM_PERSISTENT);
	
	if (location) {
		/* Only add Location; header if the handler returned something */
		DEBUG("Handler supplied location: %s\n", location);
		MHD_add_response_header(response, MHD_HTTP_HEADER_LOCATION, location);
		free(location); /* Handlers expect us to clean this up */
	}

	/* If a bad method was requested than add the Allow header based on the handlers
	 * defined for the entity */
	if (status == MHD_HTTP_METHOD_NOT_ALLOWED) {
		char allow[32] = {0};
		char *allowptr = allow;
		
		if (ent->get_handler)
			allowptr += sprintf(allowptr, "GET, ");
		if (ent->put_handler)
			allowptr += sprintf(allowptr, "PUT, ");
		if (ent->post_handler)
			allowptr += sprintf(allowptr, "POST, ");
		if (ent->delete_handler)
			allowptr += sprintf(allowptr, "DELETE, ");
		if (allow[0] == '\0') {
			/* This entity doesn't support anything! */
			CRITICAL("Entity %s has no methods!\n", ent->name);
			status = MHD_HTTP_NOT_FOUND;
		} else {
			/* Remove last ", " */
			allowptr[-2] = '\0';
			
			DEBUG("Supported methods: %s\n", allow);
			MHD_add_response_header(response, MHD_HTTP_HEADER_ALLOW, allow);
		}
	}
	
	if (resp_data) {
		if (content_type) {
			/* Handler supplied a custom content type */
			DEBUG("Handler supplied content-type: %s\n", content_type);
			MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, content_type);
			free(content_type); /* Handlers expect us to clean this up */
		} else {
			/* Fall back to default */
			MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, DEFAULT_CONTENT_TYPE);
		}
	}
	
	/* Add generic headers */
	MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");	
	{
		char keepalive[32];
		snprintf(keepalive, 32, "timeout=%d; max=%d", CONNECTION_TIMEOUT, CONNECTION_LIMIT);
		MHD_add_response_header(response, "Keep-Alive", keepalive);
	}
	MHD_add_response_header(response, MHD_HTTP_HEADER_CONNECTION, "keep-alive");
	MHD_add_response_header(response, MHD_HTTP_HEADER_SERVER, SERVER_NAME);
	
	/* Send the response */
	rc = MHD_queue_response(conn, status, response);
	MHD_destroy_response(response);
	return rc;
}

struct MHD_Daemon* http_init(uint16_t port)
{
	struct MHD_Daemon *d;
	struct MHD_OptionItem opts[] = {
		{ MHD_OPTION_CONNECTION_LIMIT,		CONNECTION_LIMIT,	NULL },
		{ MHD_OPTION_CONNECTION_TIMEOUT,	CONNECTION_TIMEOUT,	NULL },
		{ MHD_OPTION_END, 0, NULL }
	};
	
	FUNCTION_TRACE;

	d = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION,
		port,
		NULL, /* access control callback */
		NULL, /* argument to above */
 		&http_handler, /* default handler */
		NULL, /* argument to above */
		MHD_OPTION_ARRAY, opts,
		MHD_OPTION_END);
	if (d == NULL) {
		CRITICAL("Couldn't start http daemon\n");
		return NULL;
	}
	INFO("HTTP interface started on port %hu\n", port);
	return d;
}

void http_destroy(struct MHD_Daemon *d)
{
	FUNCTION_TRACE;
	
	MHD_stop_daemon(d);
	INFO("HTTP interface terminated\n");
}

static int http_iter_get_args(void *arg,
		enum MHD_ValueKind kind,
		const char *key, const char *value)
{
	sha2_context *sha = arg;

	DEBUG("%s=%s\n", key, value);
	sha2_hmac_update(sha, (unsigned char*)key, strlen(key));
	sha2_hmac_update(sha, (unsigned char*)"=", 1);
	sha2_hmac_update(sha, (unsigned char*)value, strlen(value));
	sha2_hmac_update(sha, (unsigned char*)"\n", 1);

	return MHD_YES;
}

int http_check_signature(struct MHD_Connection *conn, const unsigned char *key, size_t key_size,
		const char *method, const char *url, const char *req, size_t req_size)
{
	const char *signature;

	FUNCTION_TRACE;

	/* Get "Signature" header */
	signature = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Signature");
	if (signature) {
		sha2_context sha;

		DEBUG("Request signed as : %s\n", signature);

		/* Check signature */
		uint8_t their_mac[32 + 1], our_mac[32];
		size_t their_mac_length = sizeof(their_mac);

		/* Decode their MAC */
		if (base64_decode(their_mac, &their_mac_length, (unsigned char*)signature, strlen(signature)) ||
				their_mac_length != 32) {
			ERROR("Signature bad\n");
			return -1;
		}

		/* Determine expected request signature as HMAC-SHA256 of:
		 *
		 * <method> <entity>\n
		 * <payload>
		 */
		DEBUG("Signature data:\n");
		sha2_hmac_starts(&sha, key, key_size, 0);

		/* Request method */
		DEBUG("%s\n", method);
		sha2_hmac_update(&sha, (unsigned char*)method, strlen(method));
		sha2_hmac_update(&sha, (unsigned char*)"\n", 1);

		/* Request URL */
		DEBUG("%s\n", url);
		sha2_hmac_update(&sha, (unsigned char*)url, strlen(url));
		sha2_hmac_update(&sha, (unsigned char*)"\n", 1);

		/* Get URL query parameters */
		MHD_get_connection_values(conn, MHD_GET_ARGUMENT_KIND, http_iter_get_args, &sha);

		/* Add request body if any */
		if (req) {
			sha2_hmac_update(&sha, (unsigned char*)req, req_size);
		}
		sha2_hmac_finish(&sha, our_mac);

		if (memcmp(our_mac, their_mac, 32) != 0) {
			ERROR("Signature invalid\n");
			return -1;
		}
	} else {
		ERROR("Expected signature, none found\n");
		return -1;
	}
	DEBUG("Signature OK\n");

	return 0;
}
