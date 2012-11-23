## Process this file with automake to produce Makefile.in

localedir = $(datadir)/locale
AM_CPPFLAGS = \
	-DLOCALEDIR="\"$(localedir)\"" -I$(top_srcdir)/intl -Wall \
	$(LIBEVENT_CFLAGS)
LDADD = $(LIBEVENT_LIBS)


bin_PROGRAMS = \
	live-f1

live_f1_SOURCES = \
	main.c live-f1.h \
	macros.h gettext.h \
	cfgfile.c cfgfile.h \
	display.c display.h \
	http.c http.h \
	packet.c packet.h \
	crypt.c crypt.h \
	keyrev.c keyrev.h \
	stream.c stream.h \
	parser.c parser.h \
	packetcache.c packetcache.h \
	packetdef.h


clean-local:
	rm -f *.gcno *.gcda

maintainer-clean-local:
	rm -f *.gcov
