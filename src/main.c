/*
 * TimeStore, a lightweight time-series database engine
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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <pwd.h>

#include "tsdb.h"
#include "http.h"
#include "logging.h"
#include "profile.h"

#define DEFAULT_PORT		8080
#define DEFAULT_USER		"timestore"
#define DEFAULT_DB_PATH		"/var/lib/timestore"
#define DEFAULT_LOG_FILE	"/var/log/timestore.log"
#define DEFAULT_LOG_LEVEL	1

static int terminate = 0;

/* Signal handler */
void sigint_handler(int signum)
{
	terminate = 1;
}

static void usage(const char *name)
{
	fprintf(stderr,
		"Usage: %s [-d] [-v <log level>] [-p <HTTP port>] [-u <run as user>] [-D <db path>]\n\n"
		"-d Don't daemonise - logs to stderr\n"
		"-D Path to database tree\n\n"
		"-p Override HTTP listen port\n"
		"-u Run as specified user (not when -d specified)\n"
		"-v Set logging verbosity\n"
		, name);
	exit(EXIT_FAILURE);
}

static void daemonise(void)
{
	pid_t pid, sid;

	if (getppid() == 1) {
		/* Already a daemon */
		return;
	}

	/* Fork */
	pid = fork();
	if (pid < 0) {
		ERROR("Fork failed\n");
		exit(EXIT_FAILURE);
	}
	if (pid > 0) {
		/* Child process forked - terminate parent */
		INFO("Daemon started in process %d\n", pid);
		exit(EXIT_SUCCESS);
	}

	/* Clean up child's resources */
	umask(0);
	sid = setsid();
	if (sid < 0) {
		ERROR("setsid failed\n");
		exit(EXIT_FAILURE);
	}

	/* Redirect io */
	freopen("/dev/null", "r", stdin);
	freopen("/dev/null", "w", stdout);
	freopen(DEFAULT_LOG_FILE, "w+", stderr);
}

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

int main(int argc, char **argv)
{
	struct MHD_Daemon *d;
	int opt, debug = 0;
	int log_level = DEFAULT_LOG_LEVEL;
	unsigned short port = DEFAULT_PORT;
	char *path = NULL, *user = NULL;
	struct sigaction newsa, oldsa;

	/* Parse options */
	while ((opt = getopt(argc, argv, "dD:p:u:v:")) != -1) {
		switch (opt) {
			case 'd':
				debug = 1;
				break;
			case 'D':
				path = strdup(optarg);
				break;
			case 'p':
				port = atoi(optarg);
				break;
			case 'u':
				user = strdup(optarg);
				break;
			case 'v':
				log_level = atoi(optarg);
				break;
			default:
				usage(argv[0]);
		}
	}

	/* Adjust log level according to selected verbosity */
	logging_set_log_level(log_level);

	/* Daemonise - all initialisation must be performed in the
	 * child only, so this is done before anything else */
	if (!debug) {
		daemonise();
	}

	/* Assume defaults for strings */
	if (path == NULL)
		path = strdup(DEFAULT_DB_PATH);
	if (user == NULL)
		user = strdup(DEFAULT_USER);

	/* Change user if we are root */
	if (getuid() == 0 || geteuid() == 0) {
		struct passwd *pw = getpwnam(user);
		if (pw) {
			INFO("Switching user to: %s\n", user);
			setuid(pw->pw_uid);
		}
	} else {
		INFO("Skipping user change - not root\n");
	}

	/* Change working directory */
	INFO("Changing working directory to: %s\n", path);
	if (chdir(path) < 0) {
		ERROR("Failed changing working directory to: %s\n", path);
		exit(EXIT_FAILURE);
	}

	/* Install signal handler for quit */
	newsa.sa_handler = sigint_handler;
	sigemptyset(&newsa.sa_mask);
	newsa.sa_flags = 0;
	sigaction(SIGINT, &newsa, &oldsa);

	/* Start web server */
	INFO("Starting web server on port %d\n", port);
	d = http_init(port);
	if (!d)
		terminate = 1;

	/* Spin - the web server runs its own thread */
	while (!terminate) {
		sleep(1);
	}
	INFO("Terminating\n");
	http_destroy(d);

	/* Uninstall signal handler */
	sigaction(SIGINT, &oldsa, NULL);

	/* Free strings */
	free(user);
	free(path);

	return 0;
}
