/* live-f1
 *
 * cfgfile.c - handling of configuration files
 *
 * Copyright © 2006 Scott James Remnant <scott@netsplit.com>.
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


#include <sys/types.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <curses.h>

#include <fcntl.h>
#include <termios.h>

#include "live-f1.h"
#include "cfgfile.h"


/* Forward prototypes */
static char *fgets_alloc (FILE *stream);


/**
 * read_config:
 * @state: application state structure,
 * @filename: configuration file to read from.
 *
 * Read configuration values from @filename and store them in @state.
 *
 * Returns: 0 on success, non-zero on failure.
 **/
int
read_config (CurrentState *state,
	     const char   *filename)
{
	FILE *cfgf;
	char *line;
	int   lineno = 0;

	cfgf = fopen (filename, "r");
	if (! cfgf) {
		if (errno == ENOENT) {
			return 0;
		} else {
			fprintf (stderr, "%s:%s: %s\n", program_name, filename,
				 strerror (errno));
			return 1;
		}
	}

	while ((line = fgets_alloc (cfgf)) != NULL) {
		char *ptr;

		++lineno;
		if ((line[0] == '#') || (line[0] == '\0'))
			continue;

		ptr = line + strcspn (line, " \t\r\n");
		if (! *ptr) {
			fprintf (stderr, "%s:%s:%d: %s\n", program_name,
				 filename, lineno, _("missing value"));
			return 1;
		}

		*(ptr++) = 0;
		ptr += strspn (ptr, " \t\r\n");

		if (! strcmp (line, "email")) {
			free (state->email);
			state->email = strdup (ptr);
		} else if (! strcmp (line, "password")) {
			free (state->password);
			state->password = strdup (ptr);
		} else if (! strcmp (line, "host")) {
			free (state->host);
			state->host = strdup (ptr);
		} else if (! strcmp (line, "auth-host")) {
			free (state->auth_host);
			state->auth_host = strdup (ptr);
		} else {
			fprintf (stderr, "%s:%s:%d: %s: %s\n", program_name,
				 filename, lineno, line,
				 _("unknown key name"));
			return 1;
		}
	}

	if (fclose (cfgf)) {
		fprintf (stderr, "%s:%s: %s\n", program_name, filename,
			 strerror (errno));
		return 1;
	}

	return 0;
}

/**
 * fgets_alloc:
 * @stream: stdio stream to read from.
 *
 * Reads from stream up to EOF or a newline, without any line-length
 * limitations.
 *
 * Returns: static string containing the entire line WITHOUT the
 * terminating newline, or NULL if end of file is reached and nothing
 * was read.
 **/
static char *
fgets_alloc (FILE *stream)
{
	static char   *buf = NULL;
	static size_t  buf_sz = 0;
	size_t         buf_len = 0;

	for (;;) {
		char *ret, *pos;

		if (buf_sz <= (buf_len + 1)) {
			buf_sz += BUFSIZ;
			buf = realloc (buf, buf_sz);
			if (! buf)
				abort();
		}

		ret = fgets (buf + buf_len, buf_sz - buf_len, stream);
		if ((! ret) && (! buf_len)) {
			return NULL;
		} else if (! ret) {
			return buf;
		}

		buf_len += strlen (ret);
		pos = strchr (ret, '\n');
		if (pos) {
			*pos = '\0';
			break;
		}
	}

	return buf;
}

/**
 * write_config:
 * @state: application state structure,
 * @filename: configuration file to read from.
 *
 * Write configuration values to @filename from @state.
 *
 * Returns: 0 on success, non-zero on failure.
 **/
int
write_config (CurrentState *state,
	      const char   *filename)
{
	FILE *cfgf;
	char *tmpfile, *ptr;

	tmpfile = malloc (strlen (filename) + 6);
	ptr = strrchr (filename, '/');
	if (ptr) {
		strncpy (tmpfile, filename, ptr - filename);
		strcpy (tmpfile + (ptr - filename), "/.");
		strcat (tmpfile, ptr + 1);
		strcat (tmpfile, ".tmp");
	} else {
		strcpy (tmpfile, ".");
		strcat (tmpfile, filename);
		strcat (tmpfile, ".tmp");
	}

	cfgf = fopen (tmpfile, "w");
	if (! cfgf) {
		free (tmpfile);
		fprintf (stderr, "%s:%s: %s\n", program_name, tmpfile,
			 strerror (errno));
		return 1;
	}
	if (fchmod (fileno (cfgf), S_IRUSR | S_IWUSR))
		fprintf (stderr, "%s:%s: %s\n", program_name, tmpfile,
			 _("couldn't change file permissions"));

	fprintf (cfgf, "email %s\n", state->email);
	fprintf (cfgf, "password %s\n", state->password);

	if (fclose (cfgf)) {
		fprintf (stderr, "%s:%s: %s\n", program_name, tmpfile,
			 strerror (errno));
		free (tmpfile);
		return 1;
	}

	if (rename (tmpfile, filename)) {
		fprintf (stderr, "%s:%s: %s\n", program_name, filename,
			 strerror (errno));
		free (tmpfile);
		return 1;
	}

	free (tmpfile);
	return 0;
}

/**
 * get_config:
 * @state: application state structure.
 *
 * Get the configuration values by prompting the user and fill @state
 * with them.
 *
 * Returns: 0 on success, non-zero on failure.
 **/
int
get_config (CurrentState *state)
{
	struct termios  oldterm;
	struct termios  newterm;
	int             termfd;
	char           *answer;

	printf (_("In order to connect to the Live Timing stream, you need to be registered;\n"
		  "if you've not yet done so, do so now by filling in the form at the URL:\n"));
	printf ("http://www.formula1.com/reg/registration\n");
	printf ("\n");
	printf (_("Enter your registered e-mail address: "));
	answer = fgets_alloc (stdin);
	if (answer) {
		free (state->email);
		state->email = strdup (answer);
	} else {
		return 1;
	}

	termfd = open ("/dev/tty", O_NONBLOCK);
	if (termfd >= 0) {
		if (! tcgetattr (termfd, &oldterm)) {
			newterm = oldterm;
			newterm.c_lflag &= ~(ECHO | ECHOE | ECHOK);
			newterm.c_lflag |= ECHONL;

			tcsetattr (termfd, TCSANOW, &newterm);
		} else {
			close (termfd);
			termfd = -1;
		}
	}

	printf (_("Enter your registered password: "));
	answer = fgets_alloc (stdin);

	if (termfd >= 0) {
		if (tcsetattr (termfd, TCSANOW, &oldterm) < 0) {
			fprintf (stderr, "%s: %s: %s\n", program_name,
				 _("cannot restore terminal information"),
				 strerror (errno));
		}

		close (termfd);
	}

	if (answer) {
		free (state->password);
		state->password = strdup (answer);
	} else {
		return 1;
	}

	return 0;
}
