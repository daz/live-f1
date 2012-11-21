/* live-f1
 *
 * stream.c - data stream and key frame parsing //TODO: description
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
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <event2/bufferevent.h>
#include <event2/util.h>

#include "live-f1.h"
#include "display.h"
#include "packet.h"
#include "stream.h"


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
static void start_open_stream ();
static int next_packet (struct evbuffer *input, Packet *packet);
static void parse_stream (StateReader *r, struct evbuffer *input, char from_frame);


/**
 * clear_reader:
 * @r: stream reader structure.
 *
 * Clears stream reader structure before reading.
 **/
static void
clear_reader (StateReader *r)
{
	r->decryption_key = 0;
	r->decryption_failure = 0;
	r->frame = 0;
}

/**
 * continue_pre_handle_stream:
 * @r: stream reader structure.
 *
 * Continues @r->input_cnum packets pre-handling if pre-handling is permitted.
 * Packets are written to @r->encrypted_cnum cache in
 * pre_handle_packet function.
 * Drops every pre-handled packet.
 **/
void
continue_pre_handle_stream (StateReader *r)
{
	const Packet *packet;

	while ((! r->stop_handling_reason) &&
	       ((packet = get_head_packet (r->input_cnum)) != NULL)) {
		pre_handle_packet (r, packet, 0);
		drop_head_packet (r->input_cnum);
	}
}

/**
 * read_stream:
 * @r: stream reader structure.
 * @input: input buffer.
 * @from_frame: true if @input was received from key frame.
 *
 * Parses @input, then calls continue_pre_handle_stream.
 **/
void
read_stream (StateReader *r, struct evbuffer *input, char from_frame)
{
	parse_stream (r, input, from_frame);
	continue_pre_handle_stream (r);
}

/**
 * do_read_stream:
 * @bev: libevent's bufferevent.
 * @r: stream reader structure.
 *
 * bufferevent reading callback. Delegates execution to read_stream.
 **/
static void
do_read_stream (struct bufferevent *bev, void *r)
{
	read_stream (r, bufferevent_get_input (bev), 0);
}

/**
 * do_write_stream:
 * @bev: libevent's bufferevent.
 * @r: stream reader structure.
 *
 * bufferevent writing callback.
 **/
static void
do_write_stream (struct bufferevent *bev, void *r)
{
	//TODO: write something to log or delete function
}

/**
 * do_reopen_stream:
 * @r: stream reader structure.
 *
 * Initiates reconnection to live timing server.
 **/
static void
do_reopen_stream (evutil_socket_t sock, short what, void *r)
{
	info (1, _("Reconnecting ...\n"));
	start_getaddrinfo (r);
}

/**
 * start_reopen_stream:
 * @r: stream reader structure.
 *
 * Deferred initiation of a reconnection to live timing server.
 **/
static void
start_reopen_stream (StateReader *r)
{
	event_base_once (r->base, -1, EV_TIMEOUT, do_reopen_stream, r, NULL);
}

/**
 * handle_reading_event:
 * @bev: libevent's bufferevent.
 * @what: event reason.
 * @r: stream reader structure.
 *
 * Handles bufferevent reading event/error (see bufferevent_event_cb).
 *
 * Returns: 1 if reading was finished, 0 otherwise.
 **/
static int
handle_reading_event (struct bufferevent *bev, short what, StateReader *r)
{
	int fin = 0;

	if (what & BEV_EVENT_TIMEOUT) {
		/* ping server (server won't actually send us data
		 * unless we ping it)*/
		unsigned char buf[1] = {0x10};

		bufferevent_enable (bev, EV_READ | EV_WRITE);
		evbuffer_add (bufferevent_get_output (bev), buf, sizeof (buf));
	}
	if (what & BEV_EVENT_EOF) {
		read_stream (r, bufferevent_get_input (bev), 0);
		fin = 1;
	}
	if (what & BEV_EVENT_ERROR) {
		fprintf (stderr, "%s: %s: %s\n", program_name,
			_("error reading from data stream"),
			evutil_socket_error_to_string (EVUTIL_SOCKET_ERROR ()));
		fin = 1;
	}
	if (fin) {
		bufferevent_disable (bev, EV_READ);
		start_reopen_stream (r);
	}
	return fin;
}

/**
 * handle_writing_event:
 * @bev: libevent's bufferevent.
 * @what: event reason.
 * @r: stream reader structure.
 *
 * Handles bufferevent writing event/error (see bufferevent_event_cb).
 *
 * Returns: 1 if error is detected, 0 otherwise.
 **/
static int
handle_writing_event (struct bufferevent *bev, short what, StateReader *r)
{
	if (what & BEV_EVENT_ERROR) {
		fprintf (stderr, "%s: %s: %s\n", program_name,
			_("error writing to data stream"),
			evutil_socket_error_to_string (EVUTIL_SOCKET_ERROR ()));
		bufferevent_disable (bev, EV_WRITE);
		return 1;
	}
	return 0;
}

/**
 * do_event_stream:
 * @bev: libevent's bufferevent.
 * @what: event reason.
 * @arg: stream reader structure.
 *
 * bufferevent event/error callback (see bufferevent_event_cb).
 * Enables reading (EV_READ) and writing (EV_WRITE) on successful connection,
 * calls start_open_stream for next attempt on failed connection,
 * calls handle_reading_event/handle_writing_event during
 * data transfer process.
 **/
static void
do_event_stream (struct bufferevent *bev, short what, void *arg)
{
	StateReader *r = arg;
	int          fin = 0, finconnect = 0;

	if (what & BEV_EVENT_CONNECTED) {
		/* Ping interval (we shall ping server after last
		 * server data receiving this time later).
		 */
		const struct timeval intrv = {1, 0};

		finconnect = (bufferevent_enable (bev, EV_READ | EV_WRITE) != 0) ||
		             (bufferevent_set_timeouts (bev, &intrv, NULL) != 0);
		if (! finconnect) {
			info (2, _("Connected to %s.\n"), r->addr->ai_canonname);
			clear_reader (r);
		}
	} else if (what & BEV_EVENT_ERROR)
		finconnect = ! (bufferevent_get_enabled (bev) & EV_READ);

	if (what & BEV_EVENT_READING)
		fin = handle_reading_event (bev, what, r) || fin;
	if (what & BEV_EVENT_WRITING)
		fin = handle_writing_event (bev, what, r) || fin;

	if (fin || finconnect) {
		bufferevent_free (bev);
		info (3, _("bufferevent was destroyed\n"));
		if (finconnect)
			start_open_stream (r);
	}
}

/**
 * start_open_stream:
 * @r: stream reader structure.
 *
 * Attempts to make a connection to next address in addresses list.
 * Creates bufferevent, but does not enable reading and writing
 * (they will be enabled at do_event_stream after successful connection).
 **/
static void
start_open_stream (StateReader *r)
{
	struct bufferevent *bev;

	r->addr = r->addr ? r->addr->ai_next : r->addr_head;
	for ( ; r->addr; r->addr = r->addr->ai_next) {
		info (3, _("Trying %s ...\n"), r->addr->ai_canonname);

		bev = bufferevent_socket_new (r->base, -1,
					      BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
		if (bev) {
			info (3, _("bufferevent was created\n"));

			bufferevent_setcb (bev, do_read_stream, do_write_stream,
			                   do_event_stream, r);
			if (bufferevent_socket_connect (bev, r->addr->ai_addr,
			                                r->addr->ai_addrlen) == 0)
				return;

			bufferevent_free (bev);
			info (3, _("bufferevent was destroyed\n"));
		}
	}
	fprintf (stderr, "%s: %s\n", program_name,
		 _("unable to open data stream"));
	start_loopexit (r->base);
}

/**
 * do_getaddrinfo:
 * @errcode: evdns_getaddrinfo error code.
 * @res: addresses list.
 * @arg: stream reader structure.
 *
 * evdns_getaddrinfo callback.
 * Calls start_open_stream for first address in @res on success.
 **/
static void
do_getaddrinfo (int errcode, struct evutil_addrinfo *res, void *arg)
{
	StateReader *r = arg;

	r->gaireq = NULL;
	if (errcode) {
		fprintf (stderr, "%s: %s: %s: %s\n", program_name,
		         _("failed to resolve host"), r->host,
			 evutil_gai_strerror (errcode));
		start_loopexit (r->base);
	} else {
		info (1, _("Connecting to data stream ...\n"));

		if (r->addr_head)
			evutil_freeaddrinfo (r->addr_head);
		r->addr_head = res;
		r->addr = NULL;
		start_open_stream (r);
	}
}

/**
 * start_getaddrinfo:
 * @r: stream reader structure.
 *
 * Starts asynchronous getaddrinfo (evdns_getaddrinfo).
 **/
void
start_getaddrinfo (StateReader *r)
{
	struct evutil_addrinfo hints;
	char   serv[6];

	info (2, _("Looking up %s ...\n"), r->host);

	snprintf (serv, 6, "%u", r->port);

	memset (&hints, 0, sizeof (hints));
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = EVUTIL_AI_CANONNAME;

	r->gaireq = evdns_getaddrinfo (r->dnsbase, r->host, serv, &hints, do_getaddrinfo, r);
}

/**
 * parse_stream:
 * @r: stream reader structure.
 * @input: input buffer.
 * @from_frame: true if @input was received from key frame.
 *
 * Parses @input (translates stream to packets). 
 * Every time when next_packet reaches end of packet, pushes packet to
 * @r->input_cnum (if from_frame == 0), or pre-handles packet
 * (from_frame != 0) and pushes packet to @r->encrypted_cnum in that function.
 * @input flushes after parsing.
 **/
static void
parse_stream (StateReader *r, struct evbuffer *input, char from_frame)
{
	Packet packet;
	time_t ct = from_frame ? r->saving_time : time (NULL);

	while (evbuffer_get_contiguous_space (input))
		while (next_packet (input, &packet)) {
			packet.at = ct;
			if (from_frame)
				pre_handle_packet (r, &packet, from_frame);
			else
				push_packet (r->input_cnum, &packet); //TODO: check errors
		}
}

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
			info (3, _("Unknown system packet type: %d\n"),
			      packet->type);
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
