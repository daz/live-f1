# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([live-f1], [0.2.11], [https://bugs.launchpad.net/live-f1])
AC_COPYRIGHT([[Copyright (C) 2011, Dave Pusey <dave@puseyuk.co.uk>]])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.9 gnu check-news dist-bzip2])

AM_GNU_GETTEXT_VERSION([0.14.5])
AM_GNU_GETTEXT()

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
PKG_CHECK_MODULES([NEON], [neon >= 0.24.0])
AC_CHECK_LIB([neon], [ne_has_support],
             [AC_DEFINE(HAVE_NE_HAS_SUPPORT, 1,
                        [Define to 1 if libneon is >= 0.25])])
AC_CHECK_LIB([neon], [ne_get_response_header],
             [AC_DEFINE(HAVE_NE_GET_RESPONSE_HEADER, 1,
                        [Define to 1 if libneon is >= 0.25])])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([getopt.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_SIZE_T

# Checks for library functions.
AC_CHECK_LIB([ncurses], [initscr])

# Other checks
SJR_COMPILER_WARNINGS
SJR_COMPILER_OPTIMISATIONS
SJR_COMPILER_COVERAGE
SJR_LINKER_OPTIMISATIONS

AC_CONFIG_FILES([ Makefile po/Makefile.in intl/Makefile man/Makefile src/Makefile ])
AC_CONFIG_HEADERS([config.h])
AC_OUTPUT
