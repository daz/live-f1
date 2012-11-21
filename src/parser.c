/* live-f1
 *
 * parser.c - data stream and key frame parsing
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
#include <string.h>

#include "parser.h"


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


/**
 * buffer_peekone:
 * @buf: libevent's evbuffer.
 * @chain: structure to save pointer to bytes and bytes count.
 *
 * Saves pointer to contiguous bytes and bytes count of first chain of @buf
 * (see evbuffer_peek).
 *
 * Returns: 0 if @buf has no data, 1 otherwise.
 **/
static int
buffer_peekone (struct evbuffer *buf, struct evbuffer_iovec *chain)
{
	int res = evbuffer_peek (buf, -1, NULL, chain, 1);
	if (res == 0) {
		chain->iov_base = NULL;
		chain->iov_len = 0;
	}
	return res;
}

/**
 * next_packet:
 * @r: stream reader structure.
 * @packet: packet structure to fill.
 *
 * Takes bytes from input buffer until a complete raw packet has been seen,
 * at which point it fills @packet with the decoded information about it.
 * Drains bytes taken from input buffer. The bytes are copied
 * into an internal buffer so there's no need to worry about packets
 * crossing block boundaries. This internal buffer is static, so this function
 * is not reentrant.
 *
 * Returns: 0 if the packet was not complete, 1 if it is complete.
 **/
static int
next_packet (struct evbuffer *input,
	     Packet          *packet)
{
	struct evbuffer_iovec chain;
	static unsigned char  pbuf[129];
	static size_t         pbuf_len = 0;

	if (buffer_peekone (input, &chain) == 0)
		return 0;

	/* We need a minimum of two bytes to figure out how long the rest
	 * of it's supposed to be; copy those now if we have room.
	 */
	if (pbuf_len < 2) {
		size_t needed;

		needed = MIN (chain.iov_len, 2 - pbuf_len);
		memcpy (pbuf + pbuf_len, chain.iov_base, needed);

		pbuf_len += needed;
		evbuffer_drain (input, needed);
		buffer_peekone (input, &chain);

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
			break;
		case CAR_POSITION_HISTORY:
			packet->len = LONG_PACKET_LEN (pbuf);
			packet->data = LONG_PACKET_DATA (pbuf);
			break;
		default:
			packet->len = SHORT_PACKET_LEN (pbuf);
			packet->data = SHORT_PACKET_DATA (pbuf);
			break;
		}
	} else {
		switch ((SystemPacketType) packet->type) {
		case SYS_EVENT_ID:
		case SYS_KEY_FRAME:
			packet->len = SHORT_PACKET_LEN (pbuf);
			packet->data = SHORT_PACKET_DATA (pbuf);
			break;
		case SYS_TIMESTAMP:
			packet->len = 2;
			packet->data = 0;
			break;
		case SYS_WEATHER:
		case SYS_TRACK_STATUS:
			packet->len = SHORT_PACKET_LEN (pbuf);
			packet->data = SHORT_PACKET_DATA (pbuf);
			break;
		case SYS_COMMENTARY:
		case SYS_NOTICE:
		case SYS_SPEED:
			packet->len = LONG_PACKET_LEN (pbuf);
			packet->data = LONG_PACKET_DATA (pbuf);
			break;
		case SYS_COPYRIGHT:
			packet->len = LONG_PACKET_LEN (pbuf);
			packet->data = LONG_PACKET_DATA (pbuf);
			break;
		case SYS_VALID_MARKER:
		case SYS_REFRESH_RATE:
			packet->len = 0;
			packet->data = 0;
			break;
		default:
			packet->len = 0;
			packet->data = 0;
			break;
		}
	}

	/* Copy as much as we can of the rest of the packet */
	if (packet->len > 0) {
		size_t needed;

		if (chain.iov_len == 0)
			return 0;
		needed = MIN (chain.iov_len, (packet->len + 2) - pbuf_len);
		memcpy (pbuf + pbuf_len, chain.iov_base, needed);

		pbuf_len += needed;
		evbuffer_drain (input, needed);
		buffer_peekone (input, &chain);

		if (pbuf_len < (packet->len + 2))
			return 0;
	}

	/* We have a full packet, reset our static cache length so we
	 * can re-use internal cache for the next packet.
	 */
	pbuf_len = 0;

	/* Copy the payload */
	if (packet->len > 0) {
		memcpy (packet->payload, pbuf + 2, packet->len);
		packet->payload[packet->len] = 0;
	} else {
		packet->payload[0] = 0;
	}

	return 1;
}

/**
 * parse_stream:
 * @input: input buffer.
 * @handler: new packet handler callback.
 * @r: @handler's second argument (stream reader structure).
 * @ct: @handler's third argument (new packet's timestamp).
 *
 * Parses @input (translates stream to packets). 
 * Every time when next_packet reaches end of packet,
 * calls @handler (&newpacket, r, ct).
 * @input flushes after parsing.
 **/
void
parse_stream (struct evbuffer *input,
	      void (* handler) (Packet *, void *, time_t),
	      void *r,
	      time_t ct)
{
	Packet packet;

	while (evbuffer_get_contiguous_space (input))
		while (next_packet (input, &packet))
			if (handler)
				handler (&packet, r, ct);
}
