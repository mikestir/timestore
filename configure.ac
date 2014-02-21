AC_PREREQ([2.65])
AC_INIT([timestore], [0.1], [timestore@mikestirling.co.uk], [timestore], [http://mike-stirling.com/timestore/])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_SRCDIR([src/main.c])

AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([config.h])
AC_PROG_CC
AM_PROG_CC_C_O
AC_LANG(C)

# Check for non-pkg-config libs
AC_CHECK_LIB([m], [sinf], [])

# Check for pkg-config libs
PKG_CHECK_MODULES([libmicrohttpd], [libmicrohttpd])

# Check for requested features
AC_ARG_WITH([double],
	[AS_HELP_STRING([--with-double],
		[use 64-bit double-precision for databases @<:@default=no@:>@])],
	[],
	[with_double=no])
AS_IF([test "x$with_double" != xno],
	[AC_DEFINE(TSDB_DOUBLE_TYPE, [], [Use 64-bit floats])], [])

AC_CONFIG_FILES([Makefile src/Makefile src/cJSON/Makefile])
AC_OUTPUT
