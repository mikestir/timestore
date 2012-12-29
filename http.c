#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

/* Headers needed for microhttpd - latest version does this automatically */
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <microhttpd.h>

#include "http.h"
#include "http_tsdb.h"
#include "logging.h"

#define DEFAULT_MIME_TYPE		"text/plain"
#define SERVER_NAME			"LightTSDB/0.1 (Linux)"
#define CONNECTION_LIMIT		100
#define CONNECTION_TIMEOUT		10

typedef struct {
	const char *upload_data;
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
				}},
			}},
		}},
		
		.next = (http_entity_t[]) {{
		.name = "test",
		.arg = (void*)"main.c",
		.get_handler = http_get_file,
		}},
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
	
	/* FIXME: Add MIME type */
	
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
	char *mime_type = NULL, *location = NULL;
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
		/* FIXME: May need to allocate a buffer in the context structure
		 * and join chunks from multiple calls - at the moment we will only
		 * act upon the last chunk in the request, which should be OK for
		 * small requests */
		ctx->upload_data = upload_data;
		ctx->upload_data_size = *upload_data_size;
		*upload_data_size = 0 ;
		return MHD_YES;
	}

#if 0
	/* FIXME: Check Accept header (for GET) - return 406 Not Acceptable,
	 * Check Content-type header (for POST) - return 415 Unsupported Media Type */
	MHD_get_connection_values(conn, MHD_HEADER_KIND, &http_get_headers, NULL);
#endif

	/* Split URL on slashes and walk the entity tree for a suitable handler */
	DEBUG("%s %s\n", method, url);
	DEBUG("Request data:\n%.*s\n", (int)ctx->upload_data_size, ctx->upload_data);
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
			conn, url, &mime_type, &location,
			ctx->upload_data, ctx->upload_data_size,
			&resp_data, &resp_data_size,
			ent->arg);
	}

response:
	/* Free request context */
	free(*ptr);
	*ptr = NULL;
	
	/* Build response */
	DEBUG("status = %u\n", status);
	response = MHD_create_response_from_data(resp_data_size, resp_data, 
		resp_data ? MHD_YES /* free after use */ : MHD_NO, MHD_NO);
	
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
		if (mime_type) {
			/* Handler supplied a custom MIME type */
			DEBUG("Handler supplied content-type: %s\n", mime_type);
			MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, mime_type);
			free(mime_type); /* Handlers expect us to clean this up */		
		} else {
			/* Fall back to default */
			MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, DEFAULT_MIME_TYPE);
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
