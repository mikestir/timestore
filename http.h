#ifndef HTTP_H
#define HTTP_H

/* Headers needed for microhttpd - latest version does this automatically */
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <microhttpd.h>

/*! 
 * \brief Type used for URL method handlers
 * \param conn			Pointer to the microhttpd connection object
 * \param url			Full URL
 * \param mimetype		Pointer to return a buffer for setting Content-type: header
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
	char **mime_type,
	char **location,
	const char *req_data,
	size_t req_data_size,
	char **resp_data,
	size_t *resp_data_size,
	void *arg
);

#define HTTP_HANDLER(a)		unsigned short a(\
	struct MHD_Connection *conn,\
	const char *url,\
	char **mime_type,\
	char **location,\
	const char *req_data,\
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

#endif

