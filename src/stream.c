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
	struct addrinfo *res, hints;
	static char      service[6];
	int              sock, ret;

	info (2, _("Looking up %s ...\n"), hostname);

	sprintf (service, "%hu", port);

	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;

	ret = getaddrinfo (hostname, service, &hints, &res);
	if (ret != 0) {
		fprintf (stderr, "%s: %s: %s: %s\n", program_name,
			 _("failed to resolve host"), hostname,
			 gai_strerror (ret));
		return -1;
	}

	info (1, _("Connecting to data stream ...\n"));

	sock = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
	if (sock < 0) {
		fprintf (stderr, "%s: %s: %s\n", program_name,
			 _("failed to create data stream socket"),
			 strerror (errno));
		goto error;
	}

	ret = connect (sock, res->ai_addr, res->ai_addrlen);
	if (ret < 0) {
		fprintf (stderr, "%s: %s: %s\n", program_name,
			 _("failed to connect to data stream"),
			 strerror (errno));

		close (sock);
		sock = -1;
	}

	info (3, _("Connected.\n"));

error:
	freeaddrinfo (res);
	return sock;
}

void
parse_stream_block (void       *userdata,
		    const char *buf,
		    size_t      len)
{
	info (3, _("Received %zi bytes\n"), len);
}
