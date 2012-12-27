#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "tsdb.h"
#include "http.h"
#include "logging.h"

#define PORT	8080

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
	INFO("%s took %lu.%06lu s\n", a, (unsigned long)pf_duration.tv_sec, (unsigned long)pf_duration.tv_usec); \
	}
	
#define NPOINTS_IN	1051200
#define NPOINTS_OUT	12
	
static void test(void)
{
	tsdb_ctx_t *ctx;
	uint64_t t = 0;
	int n;
	double value;
	double *out;
	PROFILE_STORE;
	
	ctx = tsdb_open(0xbeef, 1, TSDB_CREATE);
	if (!ctx)
		return;
#if 0	
	/* Add some values */
	PROFILE_START;
	for (n = 0; n < NPOINTS_IN; n++) {
		value = 100.0 * sin((double)n / 41825.9);
		tsdb_update(ctx, t, &value);
		t += ctx->meta->interval;
	}
	PROFILE_END("insert");
#endif	
	/* Dump current values */
	PROFILE_START;
//	tsdb_get_current(ctx, &t, &value);
//	INFO("current value = %f\n", value);
	PROFILE_END("current");
	
	/* Dump historical values */
	out = (double*)malloc(sizeof(double) * NPOINTS_OUT);
	PROFILE_START;
	tsdb_get_series(ctx, 0, 0, 4 * 604800 * NPOINTS_OUT, NPOINTS_OUT, 0, out);
	PROFILE_END("series");
	for (n = 0; n < NPOINTS_OUT; n++) {
		DEBUG("%f\n", out[n]);
	}
	free(out);
	
	tsdb_close(ctx);
}

int main(void)
{
	struct MHD_Daemon *d;
	
#if 0
	test();
	return 0;
#endif
	
	d = http_init(PORT);
	getchar();
	http_destroy(d);
	
	return 0;
}
