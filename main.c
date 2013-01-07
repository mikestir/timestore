#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "tsdb.h"
#include "http.h"
#include "logging.h"
#include "profile.h"

#define PORT	8080

void test(void)
{
	tsdb_ctx_t *db;
	int64_t t = 0;
	double value;
	time_t now,last;
	int n;
	
	tsdb_create(0xcafe, 30, 1, (tsdb_pad_mode_t[]){0}, (tsdb_downsample_mode_t[]){0},
		    (unsigned int[]){20, 6, 6, 4, 7, 0});
	db = tsdb_open(0xcafe);
	
	/* Add a lot of random data */
	n = 0;
	last = time(NULL);
	while (1) {
		value = 10.0 + 15.0 * ((double)random() / (double)RAND_MAX);
		tsdb_update_values(db, &t, &value);
		t += 30;
		n++;
		now = time(NULL);
		if (now != last) {
			last = now;
			printf("%d updates per second\n", n);
			n = 0;
		}
	}
	
}

int main(void)
{
	struct MHD_Daemon *d;
	
	d = http_init(PORT);
	
	//test();
	
	getchar();
	http_destroy(d);
	
	return 0;
}
