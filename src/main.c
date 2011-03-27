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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <unistd.h>
#include <errno.h>

#include <ne_socket.h>
#include <ne_utils.h>

#include "live-f1.h"
#include "cfgfile.h"
#include "display.h"
#include "http.h"
#include "stream.h"


/* Forward prototypes */
static void print_version (void);
static void print_usage (void);


/* Program name */
const char *program_name = NULL;

/* How verbose to be */
static int verbosity = 0;

/* Command-line options */
static const char opts[] = "v";
static const struct option longopts[] = {
	{ "verbose",	no_argument, NULL, 'v' },
	{ "help",	no_argument, NULL, 0400 + 'h' },
	{ "version",	no_argument, NULL, 0400 + 'v' },
	{ NULL,		no_argument, NULL, 0 }
};


int
main (int   argc,
      char *argv[])
{
	CurrentState *state;
	const char   *home_dir;
	char         *config_file;
	int           opt, sock;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	program_name = argv[0];

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

	if (ne_sock_init ()) {
		fprintf (stderr, "%s: %s\n", program_name,
			 _("unable to initialise http library"));
		return 1;
	}


	state = malloc (sizeof (CurrentState));
	memset (state, 0, sizeof (CurrentState));
	state->host = NULL;
	state->auth_host = NULL;
	state->email = NULL;
	state->password = NULL;
	state->cookie = NULL;
	state->car_position = NULL;
	state->car_info = NULL;

	config_file = malloc (strlen (home_dir) + 7);
	sprintf (config_file, "%s/.f1rc", home_dir);

	if (read_config (state, config_file))
		return 1;

	if ((! state->email) || (! state->password)) {
		if (get_config (state) || write_config (state, config_file))
			return 1;
	}

	if (! state->host)
		state->host = DEFAULT_HOST;
	if (! state->auth_host)
		state->auth_host = DEFAULT_HOST;

	free (config_file);

	do
	{
		state->cookie = obtain_auth_cookie (state->auth_host, state->email, state->password);
	}
	while (! state->cookie);

	for (;;) {
		int ret;

		sock = open_stream (state->host, 4321);
		if (sock < 0) {
			close_display ();
			fprintf (stderr, "%s: %s: %s\n", program_name,
				 _("unable to open data stream"),
				 strerror (errno));
			return 2;
		}

		state->key = 0;
		state->frame = 0;
		state->event_no = 0;
		state->event_type = RACE_EVENT;
		state->epoch_time = 0;
		state->remaining_time = 0;
		state->laps_completed = 0;
		state->total_laps = 0;
		state->flag = GREEN_FLAG;

		state->track_temp = 0;
		state->air_temp = 0;
		state->wind_speed = 0;
		state->humidity = 0;
		state->pressure = 0;
		state->wind_direction = 0;

		if (state->fl_car) free (state->fl_car);
		state->fl_car = calloc(3, sizeof(char));
		if (state->fl_driver) free (state->fl_driver);
		state->fl_driver = calloc(15, sizeof(char));
		if (state->fl_time) free (state->fl_time);
		state->fl_time = calloc(9, sizeof(char));
		if (state->fl_lap) free (state->fl_lap);
		state->fl_lap = calloc(3, sizeof(char));
		
		state->num_cars = 0;
		if (state->car_position) {
			free (state->car_position);
			state->car_position = NULL;
		}
		if (state->car_info) {
			free (state->car_info);
			state->car_info = NULL;
		}

		reset_decryption (state);

		while ((ret = read_stream (state, sock)) > 0) {
			if (handle_keys (state) < 0) {
				close_display ();
				close (sock);
				return 0;
			}
		}

		if (ret < 0) {
			close_display ();
			fprintf (stderr, "%s: %s: %s\n", program_name,
				 _("error reading from data stream"),
				 strerror (errno));
			return 2;
		}

		close (sock);
		info (1, _("Reconnecting ...\n"));
	}
}


/**
 * info:
 * @irrelevance: minimum verbosity level to output the message,
 * @format: format string for vprintf.
 *
 * Print the formatted message to standard output if verbosity is high
 * enough.
 **/
int
info (int         irrelevance,
      const char *format, ...)
{
	va_list ap;
	int     ret;

	if (verbosity >= irrelevance) {
		va_start (ap, format);
		if (cursed) {
			char msg[512];

			ret = vsnprintf (msg, sizeof (msg), format, ap);
			msg[sizeof (msg) - 1] = 0;

			popup_message (msg);
		} else {
			ret = vprintf (format, ap);
		}

		va_end (ap);
		return ret;
	} else {
		return 0;
	}
}


/**
 * print_version:
 *
 * Print the package name, version, copyright and licence preamble to
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
 * Print the program usage instructions to standard output.
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
		  "      --version              output version information and exit.\n"));
	printf ("\n");
	printf (_("Report bugs to <%s>\n"), PACKAGE_BUGREPORT);
}
