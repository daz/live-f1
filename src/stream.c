/* live-f1
 *
 * stream.c - data stream and key frame parsing
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


#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "live-f1.h"
#include "stream.h"


/**
 * open_stream:
 * @hostname: hostname of timing server,
 * @port: port of timing server.
 *
 * Creates a socket for the data stream and connects to the live timing
 * server so data can be received.
 *
 * Returns: connected socket or -1 on failure.
 **/
int
open_stream (const char   *hostname,
	     unsigned int  port)
{
	struct addrinfo *res, *addr, hints;
	static char      service[6];
	int              sock, ret;

	info (2, _("Looking up %s ...\n"), hostname);

	sprintf (service, "%hu", port);

	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_CANONNAME;

	ret = getaddrinfo (hostname, service, &hints, &res);
	if (ret != 0) {
		fprintf (stderr, "%s: %s: %s: %s\n", program_name,
			 _("failed to resolve host"), hostname,
			 gai_strerror (ret));
		return -1;
	}

	info (1, _("Connecting to data stream ...\n"));

	sock = -1;
	for (addr = res; addr; addr = addr->ai_next) {
		info(3, _("Trying %s ...\n"), addr->ai_canonname);

		sock = socket (addr->ai_family, addr->ai_socktype,
			       addr->ai_protocol);
		if (sock < 0)
			continue;

		ret = connect (sock, addr->ai_addr, addr->ai_addrlen);
		if (ret < 0) {
			close (sock);
			sock = -1;
			continue;
		}

		break;
	}

	if (sock < 0) {
		fprintf (stderr, "%s; %s: %s\n", program_name,
			 _("failed to connect to data stream"),
			 strerror (errno));

		freeaddrinfo (res);
		return -1;
	}

	info (3, _("Connected to %s.\n"), addr->ai_canonname);
	return sock;
}

/**
 * read_stream:
 * @http_sess: neon http session to use for key frames,
 * @sock: data stream socket to read from.
 *
 * Read a block of data from the stream, this isn't quite as simple as it
 * seems because the server won't actually send us data unless we ping it;
 * but we don't want to ping as often as we need to check for things like
 * key presses from the user.
 *
 * Returns: 0 if socket closed, > 0 on success, < 0 on error.
 **/
int
read_stream (ne_session *http_sess,
	     int         sock)
{
	struct pollfd poll_fd;
	static int    timer = 0;
	int           ret;

	poll_fd.fd = sock;
	poll_fd.events = POLLIN;
	poll_fd.revents = 0;

	ret = poll (&poll_fd, 1, 100);
	if (ret < 0) {
		goto error;
	} else if (ret == 0) {
		char buf[1];

		if (timer++ < 10)
			return 1;

		info (3, _("Sending ping ...\n"));

		/* Wake the server up */
		buf[0] = 0x10;
		ret = write (sock, buf, sizeof (buf));
		if (ret < 0)
			goto error;

		timer = 0;
		return 1;
	}

	/* Error condition */
	if (poll_fd.revents & (POLLERR | POLLNVAL))
		goto error;

	/* Server went away */
	if (poll_fd.revents & POLLHUP) {
		close (sock);
		return 0;
	}

	/* Yay, data! */
	if (poll_fd.revents & POLLIN) {
		unsigned char buf[512];

		ret = read (sock, buf, sizeof (buf));
		if (ret < 0) {
			goto error;
		} else if (ret == 0) {
			close (sock);
			return 0;
		}

		parse_stream_block (http_sess, buf, ret);
	}

	timer = 0;
	return ret;
error:
	close (sock);
	return -1;
}

void
parse_stream_block (ne_session          *http_sess,
		    const unsigned char *buf,
		    size_t               len)
{
	info (3, _("Received %zi bytes\n"), len);
}
