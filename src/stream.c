/* live-f1
 *
 * stream.c - data stream management
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
#include "parser.h"
#include "stream.h"


/* Forward prototypes */
static void start_connect_stream ();


/**
 * clear_reader:
 * @r: stream reader structure.
 *
 * Clears stream reader structure before reading.
 **/
static void
clear_reader (StateReader *r)
{
	destroy_packet_iterator (&r->key_iter);
	init_packet_iterator (r->encrypted_cnum, &r->key_iter);
	r->key_request_failure = 0;
	r->current_cipher = -1;
	r->valid_frame = 0;
	r->new_frame = 0;
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
 * got_newpacket_frame:
 * @packet: pointer to new packet.
 * @r: stream reader structure.
 * @ct: new packet's timestamp.
 *
 * New packet parsing callback.
 * It is called immediately after parsing key frame (see parse_stream).
 * Pre-handles @packet (pre_handle_packet pushes @packet to
 * @r->encrypted_cnum).
 **/
static void
got_newpacket_frame (Packet *packet, void *r, time_t ct)
{
	packet->at = ct;
	pre_handle_packet (r, packet, 1);
}

/**
 * got_newpacket_stream:
 * @packet: pointer to new packet.
 * @arg: stream reader structure.
 * @ct: new packet's timestamp.
 *
 * New packet parsing callback.
 * It is called immediately after parsing live stream data (see parse_stream).
 * Pushes @packet to @r->input_cnum.
 **/
static void
got_newpacket_stream (Packet *packet, void *arg, time_t ct)
{
	StateReader *r = arg;

	packet->at = ct;
	push_packet (r->input_cnum, packet); //TODO: check errors
}

/**
 * read_stream:
 * @r: stream reader structure.
 * @input: input buffer.
 * @from_frame: true if @input was received from key frame.
 *
 * Parses @input (by using callbacks), then calls continue_pre_handle_stream.
 **/
void
read_stream (StateReader *r, struct evbuffer *input, char from_frame)
{
	parse_stream (input,
		      from_frame ? got_newpacket_frame : got_newpacket_stream,
		      r,
		      from_frame ? r->saving_time : time (NULL));
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
		info (0, "%s: %s: %s\n", program_name,
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
		info (0, "%s: %s: %s\n", program_name,
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
 * Calls handle_reading_event/handle_writing_event.
 **/
static void
do_event_stream (struct bufferevent *bev, short what, void *arg)
{
	StateReader *r = arg;
	int          fin = 0;

	if (what & BEV_EVENT_READING)
		fin = handle_reading_event (bev, what, r) || fin;
	if (what & BEV_EVENT_WRITING)
		fin = handle_writing_event (bev, what, r) || fin;

	if (fin) {
		bufferevent_free (bev);
		info (3, _("bufferevent was destroyed\n"));
	}
}

/**
 * do_connect_stream:
 * @bev: libevent's bufferevent.
 * @what: event reason.
 * @arg: stream reader structure.
 *
 * bufferevent connect callback (see bufferevent_event_cb).
 * Enables reading and writing on successful connection,
 * calls start_connect_stream for next attempt on failed connection.
 **/
static void
do_connect_stream (struct bufferevent *bev, short what, void *arg)
{
	StateReader *r = arg;
	int          fin = 0;

	if (what & BEV_EVENT_CONNECTED) {
		/* Ping interval (we shall ping server after last
		 * server data receiving this time later).
		 */
		const struct timeval intrv = {1, 0};

		bufferevent_setcb (bev, do_read_stream, do_write_stream, do_event_stream, r);
		fin = (bufferevent_enable (bev, EV_READ | EV_WRITE) != 0) ||
		      (bufferevent_set_timeouts (bev, &intrv, NULL) != 0);
	} else if (what & BEV_EVENT_ERROR)
		fin = 1;

	if (! fin) {
		info (2, _("Connected to %s.\n"), r->addr->ai_canonname);
		clear_reader (r);
	} else {
		bufferevent_free (bev);
		info (3, _("bufferevent was destroyed\n"));
		start_connect_stream (r);
	}
}

/**
 * start_connect_stream:
 * @r: stream reader structure.
 *
 * Attempts to make a connection to next address in addresses list.
 * Creates bufferevent, but does not enable reading and writing
 * (they will be enabled at do_connect_stream after successful connection).
 **/
static void
start_connect_stream (StateReader *r)
{
	struct bufferevent *bev;

	r->addr = r->addr ? r->addr->ai_next : r->addr_head;
	for ( ; r->addr; r->addr = r->addr->ai_next) {
		info (3, _("Trying %s ...\n"), r->addr->ai_canonname);

		bev = bufferevent_socket_new (r->base, -1,
					      BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
		if (bev) {
			info (3, _("bufferevent was created\n"));

			bufferevent_setcb (bev, NULL, NULL, do_connect_stream, r);
			if (bufferevent_socket_connect (bev, r->addr->ai_addr,
			                                r->addr->ai_addrlen) == 0)
				return;

			bufferevent_free (bev);
			info (3, _("bufferevent was destroyed\n"));
		}
	}
	info (0, "%s: %s\n", program_name,
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
 * Calls start_connect_stream for first address in @res on success.
 **/
static void
do_getaddrinfo (int errcode, struct evutil_addrinfo *res, void *arg)
{
	StateReader *r = arg;

	r->gaireq = NULL;
	if (errcode) {
		info (0, "%s: %s: %s: %s\n", program_name,
		      _("failed to resolve host"), r->host,
		      evutil_gai_strerror (errcode));
		start_loopexit (r->base);
	} else {
		info (1, _("Connecting to data stream ...\n"));

		if (r->addr_head)
			evutil_freeaddrinfo (r->addr_head);
		r->addr_head = res;
		r->addr = NULL;
		start_connect_stream (r);
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
