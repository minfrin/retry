#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT(repeat, 1.0.0, minfrin@sharp.fm)
AC_CONFIG_AUX_DIR(build-aux)
AM_INIT_AUTOMAKE([dist-bzip2])
AC_CONFIG_FILES([Makefile repeat.spec])
AC_CONFIG_SRCDIR([repeat.c])
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
AC_CHECK_FUNCS([clock_gettime])

AC_OUTPUT

