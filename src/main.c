/* live-f1
 *
 * Copyright © 2010 Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */

#if HAVE_GETOPT_H
# include <getopt.h>
#else /* HAVE_GETOPT_H */
# include <unistd.h>
# define getopt_long(argc, argv, optstring, longopts, longindex) \
		getopt ((argc), (argv), (optstring))
#endif /* HAVE_GETOPT_H */

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <unistd.h>
#include <errno.h>

#include <event2/dns.h>
#include <event2/event.h>

#include "live-f1.h"
#include "cfgfile.h"
#include "display.h"
#include "http.h"
#include "packet.h"
#include "stream.h"


/* Forward prototypes */
static void info_packetcache (int, int, void *);
static void print_version (void);
static void print_usage (void);


/* Program name */
const char *program_name = NULL;

/* How verbose to be */
static int verbosity = 0;

/* Debug mode flag */
static int debug_mode = 0;

/* Command-line options */
static const char opts[] = "df:rv";
static const struct option longopts[] = {
	{ "verbose",	no_argument,       NULL, 'v' },
	{ "help",	no_argument,       NULL, 0400 + 'h' },
	{ "version",	no_argument,       NULL, 0400 + 'v' },
	{ "debug",      no_argument,       NULL, 'd' },
	{ "file",       required_argument, NULL, 'f' },
	{ "replay",     no_argument,       NULL, 'r' },
	{ NULL,		no_argument,       NULL, 0 }
};


/**
 * reader_destroy:
 * @r: stream reader structure.
 *
 * Destroys @r.
 **/
static void
reader_destroy (StateReader *r)
{
	destroy_packet_cache (r->encrypted_cnum);
	destroy_packet_cache (r->input_cnum);
	if (r->periodic) {
		event_free (r->periodic);
		r->periodic = NULL;
	}
	if (r->addr_head) {
		evutil_freeaddrinfo (r->addr_head);
		r->addr_head = NULL;
		r->addr = NULL;
	}
	if (r->gaireq) {
		evdns_getaddrinfo_cancel (r->gaireq);
		r->gaireq = NULL;
	}
	if (r->dnsbase) {
		evdns_base_free (r->dnsbase, 0);
		r->dnsbase = NULL;
	}
	if (r->base) {
		event_base_free (r->base);
		r->base = NULL;
	}
}

/**
 * reader_create:
 * @r: stream reader structure.
 * @replay_mode: replay mode flag.
 *
 * Constructs @r.
 *
 * Returns: 0 on success, non-zero on failure.
 **/
static int
reader_create (StateReader *r, char replay_mode)
{
	int res = 0;

	memset (r, 0, sizeof (*r));

	r->replay_mode = replay_mode;

	r->base = event_base_new ();
	if (r->base) {
		if (r->replay_mode) {
			init_packet_cache ();
			return 0;
		}
		r->dnsbase = evdns_base_new (r->base, 1);
		if (r->dnsbase) {
			r->input_cnum = init_packet_cache ();
			if (r->input_cnum >= 0) {
				r->encrypted_cnum = init_packet_cache ();
				if (r->encrypted_cnum >= 0) {
					init_packet_iterator (r->encrypted_cnum, &r->key_iter);
					reset_reverser (&r->key_rev);
					return 0;
				} else
					res = 3;
			} else
				res = 3;
		} else
			res = 2;
	} else
		res = 1;

	reader_destroy (r);
	return res;
}

/**
 * model_destroy:
 * @m: model structure.
 *
 * Destroys @m.
 **/
static void
model_destroy (StateModel *m)
{
	destroy_packet_iterator (&m->iter);
}

/**
 * model_create:
 * @m: model structure.
 * @r: stream reader structure associated with @m.
 *
 * Constructs m.
 *
 * Returns: 0 on success, non-zero on failure.
 **/
static int
model_create (StateModel *m, StateReader *r)
{
	if ((! m) || (! r))
		return 1;

	memset (m, 0, sizeof (*m));

	init_packet_iterator (r->encrypted_cnum, &m->iter);
	m->event_type = RACE_EVENT;
	clear_model (m);

	return 0;
}

/**
 * initialise_save:
 * @state: application state structure.
 * @name: packet cache file name.
 *
 * Initialises packet cache system.
 *
 * Returns: 0 on success, 1 on failure.
 **/
static int
initialise_save (CurrentState *state, const char *name)
{
	int         cnums[2] = {state->r.input_cnum, state->r.encrypted_cnum};
	const char *names[2] = {"/dev/null",         name                   };
	char        modes[2] = {0,                   state->r.replay_mode   };
	char        fakes[2] = {1,                   0                      };
	size_t      i;

	for (i = 0; i != sizeof (cnums) / sizeof (cnums[0]); ++i) {
		int res = set_new_underlying_file (cnums[i], names[i], modes[i], fakes[i]);

		switch (res) {
		case 0:
			continue;
		case PACKETCACHE_ERR_FILE:
			fprintf (stderr, _("Unable to open file %s (reason: %s)\n"),
				 names[i], strerror (errno));
			break;
		case PACKETCACHE_ERR_VERSION:
			fprintf (stderr, _("Unable to open file %s (reason: %s)\n"),
				 names[i], _("unsupported timing file version"));
			break;
		default:
			fprintf (stderr, _("Unable to open file %s (cache errcode = %d)\n"),
				 names[i], res);
		}
		return 1;
	}
	return 0;
}

/**
 * currentstate_configurate:
 * @state: application state structure.
 * @config_dir: working directory.
 * @save_file_name: packet cache file name (use NULL for default value).
 *
 * Makes initial configuration.
 * Reads config parameters (email and password for live timing) from
 * config file or asks user for them and saves them to config file.
 * Calls initialise_save.
 *
 * Returns: 0 on success, 1 on config parameters failure,
 * 2 on initialise_save failure.
 **/
static int
currentstate_configurate (CurrentState *state,
                          const char *config_dir,
			  const char *save_file_name)
{
	char   *config_file;
	size_t  clen;
	int     res;

	clen = strlen (config_dir) + 7;
	config_file = malloc (clen);
	snprintf (config_file, clen, "%s/.f1rc", config_dir);

	if (read_config (state, config_file))
		return 1;

	if ((! state->r.email) || (! state->r.password)) {
		if (get_config (state) || write_config (state, config_file))
			return 1;
	}

	free (config_file);

	if (! state->r.host)
		state->r.host = DEFAULT_HOST;
	if (! state->r.auth_host)
		state->r.auth_host = DEFAULT_HOST;
	state->r.port = 4321;

	if (save_file_name)
		res = initialise_save (state, save_file_name);
	else {
		size_t slen = strlen (config_dir) + 9;
		char * save_file = malloc (slen);

		if (! save_file)
			return 2;
		snprintf (save_file, slen, "%s/.f1data", config_dir);
		res = initialise_save (state, save_file);
		free (save_file);
	}

	return res ? 2 : 0;
}

/**
 * do_loopexit:
 * @base: libevent's event loop.
 *
 * Terminates event loop.
 **/
static void
do_loopexit (evutil_socket_t sock, short what, void *base)
{
	event_base_loopexit (base, NULL);
}

/**
 * start_loopexit:
 * @base: libevent's event loop.
 *
 * Deferred event loop termination.
 **/
void
start_loopexit (struct event_base *base)
{
	event_base_once (base, -1, EV_TIMEOUT, do_loopexit, base, NULL); //TODO: lower priority ?
}

/**
 * update_reader_time:
 * @r: stream reader structure.
 * @ct: current time.
 *
 * Updates current time for @r.
 **/
static void
update_reader_time (StateReader *r, time_t ct)
{
	if (! r->stop_handling_reason)
		r->saving_time = ct;
}

/**
 * update_model_time:
 * @m: model structure.
 * @ct: current time (real).
 *
 * Updates time for model and calls update_time for screen if not paused.
 **/
static void
update_model_time (StateModel *m, time_t ct)
{
	time_t target;

	if (m->paused) {
		if (! m->paused_time)
			m->paused_time = ct;
		return;
	}
	if (m->paused_time) {
		m->time_gap += ct - m->paused_time;
		m->paused_time = 0;
	}

	target = ct - MAX (m->time_gap + m->replay_gap, 0);
	if (! m->model_time)
		m->model_time = target;

	/* Max time growing factor is
	 * 10 == 1s / 0.1s (interval between update_model_time calls).
	 */
	if (target > m->model_time)
		++m->model_time; /* increment by 1s */
	update_time (m);
}

/**
 * save_data:
 *
 * Saves unsaved cached packets to cache file.
 * Delegates execution to save_packets.
 **/
static void
save_data (StateReader *r)
{
	/* TODO: nonblocking IO does not work with regular files
	 * (see http://www.remlab.net/op/nonblock.shtml),
	 * should use aio_write or threads.
	 */
	if (save_packets (r->encrypted_cnum))
		fprintf (stderr, _("Unable to write data file (reason: %s)\n"),
		         strerror (errno));
}

//TODO: description
static int
wait_for_decryption_key (StateModel *m, const Packet *packet)
{
	assert (m);
	assert (packet);

	if (packet->car || (packet->type != USER_SYS_KEY))
		return 1;
	//TODO: without magic codes
	return (packet->data == 1) || (packet->data == 3);
}

/**
 * do_periodic:
 * @arg: application state structure.
 *
 * Periodically triggered timeout callback.
 * Checks keys pressing, updates time for stream reader and model,
 * handles cached packets with appropriate timestamp
 * with the regard for delay,
 * saves unsaved cached packets.
 **/
static void
do_periodic (evutil_socket_t sock, short what, void *arg)
{
	CurrentState *state = arg;
	time_t        ct = time (NULL);
	const Packet *packet;

	info (6, _("do_periodic (time=%d)\n"), ct);
	if (handle_keys (&state->m) < 0) {
		start_loopexit (state->r.base);
		return;
	}

	update_reader_time (&state->r, ct);
	update_model_time (&state->m, ct);

	while (((packet = get_packet (&state->m.iter)) != NULL) &&
	       (packet->at <= state->m.model_time) &&
	       wait_for_decryption_key (&state->m, packet)) {
		PacketIterator it;
		Packet pc;

		init_packet_iterator (state->m.iter.cnum, &it);
		if (copy_packet_iterator (&it, &state->m.iter) != 0) {
			pc = *packet;
			packet = &pc;
		}
		if (to_next_packet (&state->m.iter) != 0) {
			destroy_packet_iterator (&it);
			break;
		}
		handle_packet (&state->m, packet);
		destroy_packet_iterator (&it);
	}
	save_data (&state->r);
}

/**
 * start_periodic:
 * @state: application state structure.
 *
 * Initiates periodical do_periodic calls.
 **/
static void
start_periodic (CurrentState *state)
{
	const struct timeval intrv = {0, 100000l};

	/* immediate call to initialise CurrentState time_t fields */
	do_periodic (-1, EV_TIMEOUT, state);
	state->r.periodic = event_new (state->r.base, -1, EV_PERSIST, do_periodic, state);
	event_add (state->r.periodic, &intrv);
}

/**
 * start_reading_process:
 * @r: stream reader structure.
 * @m: model structure.
 *
 * Starts reading process.
 * Initiates querying authentication cookie for live mode,
 * initialises StateModel::replay_gap for replay mode.
 **/
static void
start_reading_process (StateReader *r, StateModel *m)
{
	if (! r->replay_mode)
		start_get_auth_cookie (r);
	else if (to_start_packet (&m->iter) == 0) {
		const Packet *packet = get_packet (&m->iter);

		if (packet) {
			m->replay_gap = time (NULL) - packet->at;
			info (4, _("set replay_gap to %d\n"), m->replay_gap);
		}
	}
}

int
main (int   argc,
      char *argv[])
{
	CurrentState       state;
	const char        *home_dir;
	char              *save_file = NULL;
	int                opt, res = 0;
	char               replay_mode = 0;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	program_name = argv[0];

	memset (&state, 0, sizeof (state));

	while ((opt = getopt_long (argc, argv, opts, longopts, NULL)) != -1) {
		switch (opt) {
		case 'v':
			verbosity++;
			break;
		case 0400 + 'h':
			print_usage ();
			return 0;
		case 0400 + 'v':
			print_version ();
			return 0;
		case 'd':
			debug_mode = 1;
			break;
		case 'f':
			save_file = strdup (optarg);
			break;
		case 'r':
			replay_mode = 1;
			break;
		case '?':
			fprintf (stderr,
				 _("Try `%s --help' for more information.\n"),
				 program_name);
			return 1;
		}
	}

	home_dir = getenv ("HOME");
	if (! home_dir) {
		fprintf (stderr, "%s: %s\n", program_name,
			 _("unable to find HOME in environment"));
		return 1;
	}

	print_version ();
	printf ("\n");

	set_packet_cache_system_logger (info_packetcache);
	res = reader_create (&state.r, replay_mode);
	switch (res) {
	case 0:
		break;
	case 1:
		fprintf (stderr, "%s: %s\n", program_name,
			 _("unable to initialise libevent event_base"));
		return 1;
	case 2:
		fprintf (stderr, "%s: %s\n", program_name,
			 _("unable to initialise libevent evdns_base"));
		return 1;
	default:
		fprintf (stderr, "%s: %s\n", program_name,
			 _("unable to initialise StateReader"));
		return 1;
	}

	if (model_create (&state.m, &state.r)) {
		fprintf (stderr, "%s: %s\n", program_name,
			 _("unable to initialise StateModel"));
		reader_destroy (&state.r);
		return 1;
	}

	if (currentstate_configurate (&state, home_dir, save_file) == 0) {
		start_reading_process (&state.r, &state.m);
		start_periodic (&state);
		event_base_dispatch (state.r.base);
	} else
		res = 1;

	free (save_file);
	close_display ();
	model_destroy (&state.m);
	reader_destroy (&state.r);
	return res;
}

/**
 * info_packetcache:
 * @type: packet cache event type.
 * @cnum: packet cache number.
 * @arg: @type-dependent argument.
 *
 * Packet cache system logger.
 * Translates calls to info.
 **/
static void
info_packetcache (int type, int cnum, void *arg)
{
	switch (type) {
	case 1:
		info (5, _("get_file_size (cnum = %d)\n"),
		      cnum);
		break;
	case 2:
		info (5, _("seek_func (cnum = %d, packet_offset = %d)\n"),
		      cnum, *((long *) arg));
		break;
	case 3:
		info (5, _("read_func (cnum = %d, packet_count = %u)\n"),
		      cnum, *((size_t *) arg));
		break;
	case 4:
		info (5, _("write_func (cnum = %d, packet_count = %u)\n"),
		      cnum, *((size_t *) arg));
		break;
	}
}

/**
 * info:
 * @irrelevance: minimum verbosity level to output the message.
 * @format: format string for vprintf.
 *
 * Prints the formatted message to stderr or to screen
 * if verbosity is high enough.
 **/
int
info (int         irrelevance,
      const char *format, ...)
{
	va_list ap;
	int     ret;

	if (verbosity >= irrelevance) {
		char msg[512];

		va_start (ap, format);

		ret = vsnprintf (msg, sizeof (msg), format, ap);
		msg[sizeof (msg) - 1] = 0;

		info_message (msg);
		if (debug_mode || (! cursed))
			ret = vfprintf (stderr, format, ap);

		va_end (ap);
		return ret;
	} else {
		return 0;
	}
}

/**
 * print_version:
 *
 * Prints the package name, version, copyright and licence preamble to
 * standard output.
 **/
static void
print_version (void)
{
	printf ("%s\n", PACKAGE_STRING);
	printf ("Copyright (C) 2011, Dave Pusey <dave@puseyuk.co.uk>\n");
	printf ("\n");
	printf (_("This is free software, covered by the GNU General Public License; see the\n"
		  "source for copying conditions.  There is NO warranty; not even for\n"
		  "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"));
}

/**
 * print_usage:
 *
 * Prints the program usage instructions to standard output.
 **/
static void
print_usage (void)
{
	printf (_("Usage: %s [OPTION]...\n"), program_name);
	printf (_("Displays live timing data from Formula 1 race, practice and qualifying\n"
		  "sessions.\n"));
	printf ("\n");
	printf (_("Options:\n"
		  "  -v, --verbose              increase verbosity for each time repeated.\n"
		  "      --help                 display this help and exit.\n"
		  "      --version              output version information and exit.\n"
		  "  -d, --debug                debug mode, information messages will be\n"
		  "                             written into stderr also\n"
		  "                             (program should be invoked as\n"
		  "                             \"live-f1 -d 2> logfilename\").\n"
		  "  -f, --file <file>          use <file> for packet streaming\n"
		  "                             (default is ~/.f1data).\n"
		  "  -r, --replay               replay stream from file instead of\n"
		  "                             getting it from live timing servers.\n"));
	printf ("\n");
	printf (_("Keys (case insensitive):\n"
		  "  'i'                        increment time gap by 1 second.\n"
		  "  'k'                        decrement time gap by 1 second.\n"
		  "  'u'                        increment time gap by 1 minute.\n"
		  "  'j'                        decrement time gap by 1 minute.\n"
		  "  'p'                        pause/unpause live timing or replay.\n"
		  "  '0'                        set time gap to 0 (press '0' again\n"
		  "                             if you want to restore last gap).\n"
		  "  LEFT, RIGHT, UP, DOWN      move screen.\n"
		  "  'q', ESC, ENTER            quit.\n"));
	printf ("\n");
	printf (_("Report bugs to <%s>\n"), PACKAGE_BUGREPORT);
}
