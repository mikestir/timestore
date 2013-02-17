/*
 * Profiling macros
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

#ifndef PROFILE_H
#define PROFILE_H

#define PROFILE_STORE	\
	struct timeval pf_start; \
	struct timeval pf_stop; \
	struct timeval pf_duration;
#define PROFILE_START	{ \
	gettimeofday(&pf_start, NULL); \
	}
#define PROFILE_END(a)	{ \
	gettimeofday(&pf_stop, NULL); \
	timersub(&pf_stop, &pf_start, &pf_duration); \
	DEBUG("%s took %lu.%06lu s\n", a, (unsigned long)pf_duration.tv_sec, (unsigned long)pf_duration.tv_usec); \
	}

#endif
