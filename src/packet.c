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

#include "live-f1.h"
#include "http.h"
#include "stream.h"
#include "packet.h"


/**
 * handle_car_packet:
 * @state: application state structure,
 * @packet: packet data.
 *
 * Handle the car-related packet.
 **/
void
handle_car_packet (CurrentState        *state,
		   const unsigned char *packet)
{
}

/**
 * handle_system_packet:
 * @state: application state structure,
 * @packet: packet data.
 *
 * Handle the system packet.
 **/
void
handle_system_packet (CurrentState        *state,
		      const unsigned char *packet)
{
	int len;

	switch ((SystemPacketType) PACKET_TYPE (packet)) {
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
		unsigned int event_no = 0;
		int          i;

		len = SHORT_PACKET_LEN (packet);
		for (i = 1; i < len; i++) {
			event_no *= 10;
			event_no += packet[2 + i] - '0';
		}

		state->key = obtain_decryption_key (event_no, state->cookie);
		state->event_no = event_no;
		state->event_type = SHORT_PACKET_DATA (packet);
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
		unsigned int frame = 0;

		len = SHORT_PACKET_LEN (packet);
		while (--len >= 0) {
			frame <<= 8;
			frame |= packet[2 + len];
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
	default:
/*		info (3, _("Unhandled system packet %d\n"), PACKET_TYPE (packet)); */
		break;
	}
}
