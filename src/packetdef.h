/* live-f1
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

#ifndef LIVE_F1_PACKETDEF_H
#define LIVE_F1_PACKETDEF_H

#include <time.h>

/**
 * Packet:
 * @car: index of car.
 * @type: type of packet.
 * @data: additional data in header.
 * @len: length of @payload.
 * @at: packet receiving timestamp.
 * @payload: (decrypted) data that followed the packet.
 *
 * This is the decoded packet structure, and is slightly easier to deal
 * with than the binary hideousness from the stream.  The @car index is
 * not the car's number, but the position on the grid at the start of the
 * race.
 **/
typedef struct {
	int car, type, data, len;
	time_t at;

	unsigned char payload[128];
} Packet;

#endif /* LIVE_F1_PACKETDEF_H */
