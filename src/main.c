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
#include <locale.h>

#include <ne_socket.h>
#include <ne_session.h>

#include "live-f1.h"
#include "http.h"


/* Program name */
const char *program_name = NULL;

/* How verbose to be */
static int verbosity = 10;


int
main (int   argc,
      char *argv[])
{
	char         *cookie;
	unsigned int  key;
	ne_session   *http_sess;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	program_name = argv[0];

	if (ne_sock_init ()) {
		fprintf (stderr, "%s: %s\n", program_name,
			 _("unable to initialise http library"));
		return 1;
	}

	http_sess = ne_session_create ("http", "live-timing.formula1.com", 80);
	ne_set_useragent (http_sess, PACKAGE_STRING);

	cookie = obtain_auth_cookie (http_sess, "scott-fia@netsplit.com",
				     "mka773624");
	if (! cookie) {
		printf ("Unable to obtain authorisation cookie.\n");
		return 1;
	}

	key = obtain_decryption_key (http_sess, 6241, cookie);
	if (! key) {
		printf ("Unable to obtain decryption key.\n");
		return 1;
	}

	obtain_key_frame (http_sess, 0, NULL);

	ne_session_destroy (http_sess);

	return 0;
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
		ret = vprintf (format, ap);
		va_end (ap);

		return ret;
	} else {
		return 0;
	}
}
