AUTOMAKE_OPTIONS = subdir-objects
ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}

AM_CFLAGS = \
	-I. -IcJSON \
	$(libmicrohttpd_CFLAGS)
#	-Wall -Werror

bin_PROGRAMS = timestore
timestore_SOURCES = \
	main.c \
	logging.c \
	tsdb.c \
	http.c \
	http_tsdb.c \
	http_csv.c \
	base64.c \
	sha2.c \
	cJSON/cJSON.c

timestore_LDADD = -lm -lpthread -lrt \
	$(libmicrohttpd_LIBS)

