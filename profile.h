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
