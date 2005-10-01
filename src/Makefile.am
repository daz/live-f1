## Process this file with automake to produce Makefile.in

localedir = $(datadir)/locale
DEFS = -DLOCALEDIR="\"$(localedir)\""
INCLUDES = -I$(top_srcdir)/intl


bin_PROGRAMS = \
	live-f1

live_f1_SOURCES = \
	main.c


#TESTS = \
#	test_main
#
#check_PROGRAMS = $(TESTS)
#
#test_something_SOURCES = tests/test_main.c
#test_something_LDADD = main.o


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
