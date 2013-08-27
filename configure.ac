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

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
