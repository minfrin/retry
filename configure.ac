#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT(retry, 1.0.7, minfrin@sharp.fm)
AC_CONFIG_AUX_DIR(build-aux)
AC_CONFIG_MACRO_DIRS([m4])
AM_INIT_AUTOMAKE([dist-bzip2])
LT_INIT
AC_CONFIG_FILES([Makefile retry.spec])
AC_CONFIG_SRCDIR([retry.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC


# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT32_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([getopt])

AC_OUTPUT

