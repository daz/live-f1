/* live-f1
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

#ifndef LIVE_F1_PACKET_H
#define LIVE_F1_PACKET_H

#include "live-f1.h"


/**
 * CarPacketType:
 *
 * Known types of packets for cars, mostly these consist of a table
 * cell update with the colour in the data field.
 **/
typedef enum {
	CAR_POSITION_UPDATE,
	CAR_POSITION,
	CAR_NUMBER,
	CAR_DRIVER,
	/* Everything else is short */
	CAR_POSITION_HISTORY = 15
} CarPacketType;

/**
 * SystemPacketType:
 *
 * Known types of packets that aren't related to cars, covering a wide
 * range of different formats and data.
 **/
typedef enum {
	SYS_EVENT_ID = 1,
	SYS_KEY_FRAME,
	SYS_UNKNOWN_SPECIAL_A,
	SYS_UNKNOWN_LONG_A,
	SYS_UNKNOWN_SPECIAL_B,
	SYS_NOTICE,
	SYS_STRANGE_A, /* Always two bytes */
	SYS_UNKNOWN_SHORT_A = 9,
	SYS_UNKNOWN_LONG_C,
	SYS_UNKNOWN_SHORT_B,
	SYS_COPYRIGHT
} SystemPacketType;

/**
 * Packet:
 * @car: index of car,
 * @type: type of packet,
 * @data: additional data in header,
 * @len: length of @payload,
 * @payload: (decrypted) data that followed the packet.
 *
 * This is the decoded packet structure, and is slightly easier to deal
 * with than the binary hideousness from the stream.  The @car index is
 * not the car's number, but the position on the grid at the start of the
 * race.
 **/
typedef struct {
	int  car, type, data, len;

	unsigned char payload[128];
} Packet;


SJR_BEGIN_EXTERN

void handle_car_packet    (CurrentState *state, const Packet *packet);
void handle_system_packet (CurrentState *state, const Packet *packet);

SJR_END_EXTERN

#endif /* LIVE_F1_PACKET_H */
