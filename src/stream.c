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
#include <errno.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "live-f1.h"
#include "display.h"
#include "packet.h"
#include "stream.h"


/* Encryption seed */
#define CRYPTO_SEED 0x55555555

/* Which car the packet is for */
#define PACKET_CAR(_p) ((_p)[0] & 0x1f)

/* Which type of packet it is */
#define PACKET_TYPE(_p) (((_p)[0] >> 5) | (((_p)[1] & 0x01) << 3))

/* Data from a long packet */
#define LONG_PACKET_DATA(_p) 0

/* Data from a short packet */
#define SHORT_PACKET_DATA(_p) (((_p)[1] & 0x0e) >> 1)

/* Data from a special packet */
#define SPECIAL_PACKET_DATA(_p) ((_p)[1] >> 1)

/* Length of the packet if it's one of the long ones */
#define LONG_PACKET_LEN(_p) ((_p)[1] >> 1)

/* Length of the packet if it's one of the short ones */
#define SHORT_PACKET_LEN(_p) (((_p)[1] & 0xf0) == 0xf0 ? -1 : ((_p)[1] >> 4))

/* Length of the packet if it's a special one */
#define SPECIAL_PACKET_LEN(_p) 0


/* Forward prototypes */
static int next_packet (CurrentState *state, Packet *packet,
			const unsigned char **buf, size_t *buf_len);


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
		info (3, _("Trying %s ...\n"), addr->ai_canonname);

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

	if (sock >= 0)
		info (2, _("Connected to %s.\n"), addr->ai_canonname);

	freeaddrinfo (res);
	return sock;
}

/**
 * read_stream:
 * @state: application state structure,
 * @sock: socket to read from.
 *
 * Read a block of data from the stream, this isn't quite as simple as it
 * seems because the server won't actually send us data unless we ping it;
 * but we don't want to ping as often as we need to check for things like
 * key presses from the user.
 *
 * Returns: 0 if socket closed, > 0 on success, < 0 on error.
 **/
int
read_stream (CurrentState *state, int sock)
{
	struct pollfd poll_fd;
	static int    timer = 0;
	int           numr, len;

	poll_fd.fd = sock;
	poll_fd.events = POLLIN;
	poll_fd.revents = 0;

	numr = poll (&poll_fd, 1, 100);
	if (numr > 0) {
		unsigned char buf[512];

		len = read (sock, buf, sizeof (buf));
		if (len > 0) {
			parse_stream_block (state, buf, len);
			timer = 0;
			return len;
		} else if ((len < 0) && (errno != ECONNRESET)) {
			if (errno == EINTR)
				return 1;

			return -1;
		} else {
			return 0;
		}
	} else if (numr < 0) {
		if (errno == EINTR)
			return 1;

		return -1;
	} else {
		char buf[1];

		if (timer++ < 10)
			return 1;

		/* Wake the server up */
		buf[0] = 0x10;
		len = write (sock, buf, sizeof (buf));
		if (len > 0) {
			update_time (state);
			timer = 0;
			return len;
		} else if ((len < 0) && (errno != EPIPE)) {
			if (errno == EINTR)
				return 1;

			return -1;
		} else {
			return 0;
		}
	}
}

/**
 * parse_stream_block:
 * @state: application state structure,
 * @buf: data read from server or key frame,
 * @buf_len: length of @buf.
 *
 * Parse a data stream block obtained either from the data server or a
 * key frame.  Calls either handle_car_packet() or handle_system_packet(),
 * and is safe for those to result in further stream parsing calls.
 **/
int
parse_stream_block (CurrentState        *state,
		    const unsigned char *buf,
		    size_t               buf_len)
{
	Packet packet;

	while (next_packet (state, &packet, &buf, &buf_len)) {
		if (packet.car) {
			handle_car_packet (state, &packet);
		} else {
			handle_system_packet (state, &packet);
		}
	}

	return 0;
}

/**
 * next_packet:
 * @state: application state structure,
 * @packet: packet structure to fill,
 * @buf: buffer to copy packet from,
 * @buf_len: length of @buf.
 *
 * Takes bytes from @buf until a complete raw packet has been seen,
 * at which point if fills @packet with the decoded information about
 * it.
 *
 * @buf_len is decreased and @buf moved upwards each time bytes are
 * taken from it.  The bytes are copied into an internal buffer so
 * there's no need to worry about packets crossing block boundaries.
 *
 * Returns: 0 if the packet was not complete, 1 if it is complete
 **/
static int
next_packet (CurrentState         *state,
	     Packet               *packet,
	     const unsigned char **buf,
	     size_t               *buf_len)
{
	static unsigned char pbuf[129];
	static size_t        pbuf_len = 0;
	int                  decrypt = 0;

	/* We need a minimum of two bytes to figure out how long the rest
	 * of it's supposed to be; copy those now if we have room.
	 */
	if (pbuf_len < 2) {
		size_t needed;

		needed = MIN (*buf_len, 2 - pbuf_len);
		memcpy (pbuf + pbuf_len, *buf, needed);

		pbuf_len += needed;
		*buf += needed;
		*buf_len -= needed;

		if (pbuf_len < 2)
			return 0;
	}

	/* We now have the packet header, this is enough information to
	 * figure out how long the rest of it is and whether we need to
	 * decrypt it or not.
	 *
	 * Fill in some of the fields now, ok we'll rewrite these every
	 * time we come through, but that's not really that bad.
	 */
	packet->car = PACKET_CAR (pbuf);
	packet->type = PACKET_TYPE (pbuf);

	if (packet->car) {
		switch ((CarPacketType) packet->type) {
		case CAR_POSITION_UPDATE:
			packet->len = SPECIAL_PACKET_LEN (pbuf);
			packet->data = SPECIAL_PACKET_DATA (pbuf);
			decrypt = 0;
			break;
		case CAR_POSITION_HISTORY:
			packet->len = LONG_PACKET_LEN (pbuf);
			packet->data = LONG_PACKET_DATA (pbuf);
			decrypt = 1;
			break;
		default:
			packet->len = SHORT_PACKET_LEN (pbuf);
			packet->data = SHORT_PACKET_DATA (pbuf);
			decrypt = 1;
			break;
		}
	} else {
		switch ((SystemPacketType) packet->type) {
		case SYS_EVENT_ID:
		case SYS_KEY_FRAME:
			packet->len = SHORT_PACKET_LEN (pbuf);
			packet->data = SHORT_PACKET_DATA (pbuf);
			decrypt = 0;
			break;
		case SYS_TIMESTAMP:
			packet->len = 2;
			packet->data = 0;
			decrypt = 1;
			break;
		case SYS_WEATHER:
		case SYS_TRACK_STATUS:
			packet->len = SHORT_PACKET_LEN (pbuf);
			packet->data = SHORT_PACKET_DATA (pbuf);
			decrypt = 1;
			break;
		case SYS_COMMENTARY:
		case SYS_NOTICE:
		case SYS_SPEED:
			packet->len = LONG_PACKET_LEN (pbuf);
			packet->data = LONG_PACKET_DATA (pbuf);
			decrypt = 1;
			break;
		case SYS_COPYRIGHT:
			packet->len = LONG_PACKET_LEN (pbuf);
			packet->data = LONG_PACKET_DATA (pbuf);
			decrypt = 0;
			break;
		case SYS_VALID_MARKER:
		case SYS_REFRESH_RATE:
			packet->len = 0;
			packet->data = 0;
			decrypt = 0;
			break;
		default:
			info (3, _("Unknown system packet type: %d\n"),
			      packet->type);
			packet->len = 0;
			packet->data = 0;
			decrypt = 0;
			break;
		}
	}

	/* Copy as much as we can of the rest of the packet */
	if (packet->len > 0) {
		size_t needed;

		needed = MIN (*buf_len, (packet->len + 2) - pbuf_len);
		memcpy (pbuf + pbuf_len, *buf, needed);

		pbuf_len += needed;
		*buf += needed;
		*buf_len -= needed;

		if (pbuf_len < (packet->len + 2))
			return 0;
	}

	/* We have a full packet, reset our static cache length so we
	 * can re-use it for the next packet (which might happen before
	 * this one has finished being handled when key frames are being
	 * fetched).
	 */
	pbuf_len = 0;

	/* Copy the payload and decrypt it */
	if (packet->len > 0) {
		memcpy (packet->payload, pbuf + 2, packet->len);
		packet->payload[packet->len] = 0;

		if (decrypt)
			decrypt_bytes (state, packet->payload, packet->len);
	} else {
		packet->payload[0] = 0;
	}

	return 1;
}

/**
 * reset_decryption:
 * @state: application state structure.
 *
 * Resets the encryption salt to the initial seed; this begins the
 * cycle again.
 **/
void
reset_decryption (CurrentState *state)
{
	state->salt = CRYPTO_SEED;
}

/**
 * decrypt_bytes:
 * @state: application state structure,
 * @buf: buffer to decrypt,
 * @len: number of bytes in @buf to decrypt.
 *
 * Decrypts the initial @len bytes of @buf modifying the buffer given,
 * rather than returning a new string.
 **/
void
decrypt_bytes (CurrentState  *state,
	       unsigned char *buf,
	       size_t         len)
{
	if (! state->key)
		return;

	while (len--) {
		state->salt = ((state->salt >> 1)
			       ^ (state->salt & 0x01 ? state->key : 0));
		*(buf++) ^= (state->salt & 0xff);
	}
}
