# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([live-f1], [0.1.0], [live-f1-bugs@netsplit.com])
AC_COPYRIGHT([[Copyright © 2005 Scott James Remnant <scott@netsplit.com>.]])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.9 gnu check-news dist-bzip2])

AM_GNU_GETTEXT_VERSION([0.14.5])
AM_GNU_GETTEXT()

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
PKG_CHECK_MODULES([NEON], [neon >= 0.24.0])

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

# Other checks
SJR_COMPILER_WARNINGS
SJR_COMPILER_OPTIMISATIONS
SJR_COMPILER_COVERAGE
SJR_LINKER_OPTIMISATIONS

AC_CONFIG_FILES([ Makefile po/Makefile.in intl/Makefile src/Makefile ])
AC_CONFIG_HEADERS([config.h])
AC_OUTPUT
