#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "tsdb.h"
#include "http.h"
#include "logging.h"
#include "profile.h"

#define PORT	8080

int main(void)
{
	struct MHD_Daemon *d;
	
	d = http_init(PORT);
	getchar();
	http_destroy(d);
	
	return 0;
}
