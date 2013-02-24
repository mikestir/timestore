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

#ifndef LOGGING_H
#define LOGGING_H

typedef enum {
	LL_CRITICAL,
	LL_ERROR,
	LL_WARNING,
	LL_INFO,
	LL_DEBUG,
	LL_TRACE
} log_level_t;

#define CRITICAL(a,...)	logging_log(LL_CRITICAL, __FILE__, __LINE__, a, ##__VA_ARGS__)
#define ERROR(a,...)	logging_log(LL_ERROR, __FILE__, __LINE__, a, ##__VA_ARGS__)
#define WARNING(a,...)	logging_log(LL_WARNING, __FILE__, __LINE__, a, ##__VA_ARGS__)
#define INFO(a,...)		logging_log(LL_INFO, __FILE__, __LINE__, a, ##__VA_ARGS__)
#define DEBUG(a,...)	logging_log(LL_DEBUG, __FILE__, __LINE__, a, ##__VA_ARGS__)
#define TRACE(a,...)	logging_log(LL_TRACE, __FILE__, __LINE__, a, ##__VA_ARGS__)

#define FUNCTION_TRACE	TRACE("%s()\n", __FUNCTION__)

/*!
 * \brief Set the log level.  Messages with log level above the set value are muted.
 */
void logging_set_log_level(log_level_t log_level);

/*!
 * \brief Log messages to stderr
 * \param log_level		Message is muted if the selected log level below this value
 * \param file			Source file in which the message originates
 * \param line			Line number at which the message originates
 * \param fmt			Format string followed by variable number of arguments
 */
void logging_log(log_level_t log_level, const char *file, int line, const char *fmt, ...);

#endif
