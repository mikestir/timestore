#ifndef LOGGING_H
#define LOGGING_H

#include "ansi.h"

/* FIXME: convert to runtime */
typedef enum {
	LL_CRITICAL,
	LL_ERROR,
	LL_WARNING,
	LL_INFO,
	LL_DEBUG,
	LL_TRACE
} log_level_t;

#if VERBOSITY > 0
#define CRITICAL(a,...)	{ fprintf(stderr, ATTR_RESET FG_RED __FILE__ "(%d): " ATTR_RESET a, __LINE__, ##__VA_ARGS__); }
#else
#define CRITICAL(...)
#endif
#if VERBOSITY > 1
#define ERROR(a,...)	{ fprintf(stderr, ATTR_RESET FG_YELLOW __FILE__ "(%d): " ATTR_RESET a, __LINE__, ##__VA_ARGS__); }
#else
#define ERROR(...)
#endif
#if VERBOSITY > 2
#define WARNING(a,...)	{ fprintf(stderr, ATTR_RESET FG_BLUE __FILE__ "(%d): " ATTR_RESET a, __LINE__, ##__VA_ARGS__); }
#else
#define WARNING(...)
#endif
#if VERBOSITY > 3
#define INFO(a,...)	{ fprintf(stderr, ATTR_RESET FG_WHITE __FILE__ "(%d): " ATTR_RESET a, __LINE__, ##__VA_ARGS__); }
#else
#define INFO(...)
#endif
#if VERBOSITY > 4
#define DEBUG(a,...)	{ fprintf(stderr, ATTR_RESET FG_GREEN __FILE__ "(%d): " ATTR_RESET a, __LINE__, ##__VA_ARGS__); }
#else
#define DEBUG(...)
#endif
#if VERBOSITY > 5
#define TRACE(a,...)	{ fprintf(stderr, ATTR_RESET FG_GREEN __FILE__ "(%d): " ATTR_RESET a, __LINE__, ##__VA_ARGS__); }
#else
#define TRACE(...)
#endif

#define FUNCTION_TRACE	TRACE("%s()\n", __FUNCTION__)

#endif
