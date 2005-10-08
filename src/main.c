/* live-f1
 *
 * Copyright © 2005 Scott James Remnant <scott@netsplit.com>.
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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <unistd.h>

#include <ne_socket.h>

#include "live-f1.h"
#include "cfgfile.h"
#include "display.h"
#include "http.h"
#include "stream.h"


/* Forward prototypes */
static void print_version (void);


/* Program name */
const char *program_name = NULL;

/* How verbose to be */
static int verbosity = 3;


int
main (int   argc,
      char *argv[])
{
	CurrentState *state;
	const char   *home_dir;
	char         *config_file;
	int           sock;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	program_name = argv[0];

	home_dir = getenv ("HOME");
	if (! home_dir) {
		fprintf (stderr, "%s: %s\n", program_name,
			 _("unable to find HOME in environment"));
		return 1;
	}

	print_version ();

	if (ne_sock_init ()) {
		fprintf (stderr, "%s: %s\n", program_name,
			 _("unable to initialise http library"));
		return 1;
	}


	state = malloc (sizeof (CurrentState));
	memset (state, 0, sizeof (state));
	state->host = NULL;
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

	free (config_file);


	state->cookie = obtain_auth_cookie (state->host, state->email,
					    state->password);
	if (! state->cookie)
		return 1;

	for (;;) {
		sock = open_stream (state->host, 4321);
		if (sock < 0)
			return 1;

		reset_decryption (state);

		while (read_stream (state, sock) > 0) {
			if (should_quit (0)) {
				close_display ();
				close (sock);
				return 0;
			}
		}
	}

	return 1;
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
			msg[sizeof (msg)] = 0;

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
	printf ("Copyright (C) 2005 Scott James Remnant <scott@netsplit.com>.\n");
	printf ("\n");
	printf (_("This is free software, covered by the GNU General Public License; see the\n"
		  "source for copying conditions.  There is NO warranty; not even for\n"
		  "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"));
	printf ("\n");
}
