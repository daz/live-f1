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
 * Known types of non-atom packets for cars.
 **/
typedef enum {
	CAR_POSITION_UPDATE = 0,
	CAR_POSITION_HISTORY = 15,
	LAST_CAR_PACKET
} CarPacketType;

/**
 * RaceAtomType:
 *
 * Known types of data atoms for cars during a race event.
 **/
typedef enum {
	RACE_POSITION = 1,
	RACE_NUMBER,
	RACE_DRIVER,
	RACE_GAP,
	RACE_INTERVAL,
	RACE_LAP_TIME,
	RACE_SECTOR_1,
	RACE_LAP_STOP,
	RACE_SECTOR_2,
	RACE_LAP_IN_PIT,
	RACE_SECTOR_3,
	RACE_LAP_OUT,
	RACE_NUM_PITS,
	LAST_RACE_ATOM
} RaceAtomType;

/**
 * PracticeAtomType:
 *
 * Known types of data atoms for cars during a race event.
 **/
typedef enum {
	PRACTICE_POSITION = 1,
	PRACTICE_NUMBER,
	PRACTICE_DRIVER,
	PRACTICE_BEST,
	PRACTICE_GAP,
	PRACTICE_SECTOR_1,
	PRACTICE_SECTOR_2,
	PRACTICE_SECTOR_3,
	PRACTICE_LAPS,
	PRACTICE_UNKNOWN,
	LAST_PRACTICE
} PracticeAtomType;


/**
 * SystemPacketType:
 *
 * Known types of packets that aren't related to cars, covering a wide
 * range of different formats and data.
 **/
typedef enum {
	SYS_EVENT_ID = 1,
	SYS_KEY_FRAME,
	SYS_COMMENTARY = 4,
	SYS_NOTICE = 6,
	SYS_TIMESTAMP = 7,
	SYS_WEATHER = 9,
	SYS_SPEED,
	SYS_TRACK_STATUS,
	SYS_COPYRIGHT,
	LAST_SYSTEM_PACKET
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
	int car, type, data, len;

	unsigned char payload[128];
} Packet;


SJR_BEGIN_EXTERN

void handle_car_packet    (CurrentState *state, const Packet *packet);
void handle_system_packet (CurrentState *state, const Packet *packet);

SJR_END_EXTERN

#endif /* LIVE_F1_PACKET_H */
