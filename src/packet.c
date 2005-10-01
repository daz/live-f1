/* live-f1
 *
 * packet.c - individual packet handling
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
#include <stdlib.h>
#include <string.h>

#include "live-f1.h"
#include "http.h"
#include "stream.h"
#include "packet.h"


/**
 * handle_car_packet:
 * @state: application state structure,
 * @packet: decoded packet structure.
 *
 * Handle the car-related packet.
 **/
void
handle_car_packet (CurrentState *state,
		   const Packet *packet)
{
}

/**
 * handle_system_packet:
 * @state: application state structure,
 * @packet: decoded packet structure.
 *
 * Handle the system packet.
 **/
void
handle_system_packet (CurrentState *state,
		      const Packet *packet)
{
	switch ((SystemPacketType) packet->type) {
	case SYS_EVENT_ID:
		/* Event Start:
		 * Format: odd byte, then decimal.
		 * Data: type of event.
		 *
		 * Indicates the start of an event, we use this to set up
		 * the table properly and obtain the decryption key for
		 * the event.
		 */
	{
		unsigned int event_no = 0, i;

		for (i = 1; i < packet->len; i++) {
			event_no *= 10;
			event_no += packet->payload[i] - '0';
		}

		state->key = obtain_decryption_key (event_no, state->cookie);
		state->event_no = event_no;
		state->event_type = packet->data;
		reset_decryption (state);

		info (3, _("Begin new event #%d (type: %d)\n"),
		      state->event_no, state->event_type);
		break;
	}
	case SYS_KEY_FRAME:
		/* Key Frame Marker.
		 * Format: little-endian integer.
		 *
		 * If we've not yet encountered a key frame, we need to
		 * load this to get up to date.  Otherwise we just set
		 * our counter and carry on
		 */
	{
		unsigned int frame = 0, i;

		i = packet->len;
		while (i) {
			frame <<= 8;
			frame |= packet->payload[--i];
		}

		reset_decryption (state);
		if (! state->frame) {
			state->frame = frame;
			obtain_key_frame (frame, state);
			reset_decryption (state);
		} else {
			state->frame = frame;
		}

		break;
	}
	case SYS_COPYRIGHT:
		/* Copyright Notice
		 * Format: string
		 *
		 * Plain text copyright notice in the start of the feed.
		 */
	{
		char *message;

		message = malloc (packet->len + 1);
		strncpy (message, (char *)packet->payload, packet->len);
		message[packet->len] = 0;

		info (2, "%s\n", message);

		free(message);
		break;
	}
	case SYS_NOTICE:
		/* Important System Notice
		 * Format: string
		 *
		 * Various important system notices get displayed this
		 * way.
		 */
	{
		char *message;

		message = malloc (packet->len + 1);
		strncpy (message, (char *)packet->payload, packet->len);
		message[packet->len] = 0;

		info (0, "%s\n", message);

		free(message);
		break;
	}
	default:
		/*
		printf ("Unhandled system packet:\n");
		printf ("    type: %d\n", packet->type);
		printf ("    data: %d\n", packet->data);
		printf ("    len: %d\n", packet->len);
		printf ("    payload: ");
		{
			int i;
			for (i = 0; i < packet->len; i++)
				if (packet->payload[i] > ' ' && packet->payload[i] <= '~')
					printf ("%c", packet->payload[i]);
			        else
					printf (".");
		}
		printf ("\n");
		*/
		break;
	}
}
