/*
 * Logging functions
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

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "logging.h"

#define LOG_TO_SYSLOG
#define LOG_TO_STREAM
#define SYSLOG_FACILITY		LOG_DAEMON
#define SYSLOG_NAME			"timestore"

#define TIMESTAMP_MAX		20
#define TIMESTAMP_FORMAT	"%Y%m%d-%H:%M:%S"

#ifdef LOG_TO_SYSLOG
#include <syslog.h>
#endif

static log_level_t g_log_level = LL_CRITICAL;

void logging_set_log_level(log_level_t log_level)
{
	g_log_level = log_level;
	INFO("Log level set to %d\n", log_level);

#ifdef LOG_TO_SYSLOG
	openlog(SYSLOG_NAME, LOG_CONS | LOG_PID | LOG_NDELAY, SYSLOG_FACILITY);
#endif
}

void logging_log(log_level_t log_level, const char *file, int line, const char *fmt, ...)
{
	va_list args;
#ifdef LOG_TO_SYSLOG
	int priority;
#endif
#ifdef LOG_TO_STREAM
	const char *llstr[] = {
			"CRITICAL",
			"ERROR",
			"WARNING",
			"INFO",
			"DEBUG",
			"TRACE",
	};
	char timestamp[TIMESTAMP_MAX];
	time_t now;
#endif

	if (log_level > g_log_level)
		return; /* mute */

#ifdef LOG_TO_SYSLOG
	/* Log to syslog */
	switch (log_level) {
	case LL_CRITICAL: priority = LOG_CRIT; break;
	case LL_ERROR: priority = LOG_ERR; break;
	case LL_WARNING: priority = LOG_WARNING; break;
	case LL_INFO: priority = LOG_INFO; break;
	default: priority = LOG_DEBUG; break;
	}
	va_start(args, fmt);
	vsyslog(priority, fmt, args);
	va_end(args);
#endif

#ifdef LOG_TO_STREAM
	/* Log to stdout - in daemon mode this should have been diverted to a file */
	now = time(NULL);
	strftime(timestamp, TIMESTAMP_MAX, TIMESTAMP_FORMAT, localtime(&now));
	fprintf(stderr, "%s:%s:%s(%d): ", timestamp, llstr[(int)log_level], file, line);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fflush(stderr);
#endif
}
