/*
 * Logging macros
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
