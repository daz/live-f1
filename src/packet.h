/* live-f1
 *
 * Copyright © 2010 Scott James Remnant <scott@netsplit.com>.
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
	CAR_POSITION_UPDATE	= 0,
	CAR_POSITION_HISTORY	= 15,
	LAST_CAR_PACKET
} CarPacketType;

/**
 * RaceAtomType:
 *
 * Known types of data atoms for cars during a race event.
 **/
typedef enum {
	RACE_POSITION	= 1,
	RACE_NUMBER	= 2,
	RACE_DRIVER	= 3,
	RACE_GAP	= 4,
	RACE_INTERVAL	= 5,
	RACE_LAP_TIME	= 6,
	RACE_SECTOR_1	= 7,
	RACE_PIT_LAP_1	= 8,
	RACE_SECTOR_2	= 9,
	RACE_PIT_LAP_2	= 10,
	RACE_SECTOR_3	= 11,
	RACE_PIT_LAP_3	= 12,
	RACE_NUM_PITS	= 13,
	LAST_RACE_ATOM
} RaceAtomType;

/**
 * PracticeAtomType:
 *
 * Known types of data atoms for cars during a practice event.
 **/
typedef enum {
	PRACTICE_POSITION	= 1,
	PRACTICE_NUMBER		= 2,
	PRACTICE_DRIVER		= 3,
	PRACTICE_BEST		= 4,
	PRACTICE_GAP		= 5,
	PRACTICE_SECTOR_1	= 6,
	PRACTICE_SECTOR_2	= 7,
	PRACTICE_SECTOR_3	= 8,
	PRACTICE_LAP		= 9,
	LAST_PRACTICE
} PracticeAtomType;

/**
 * QualifyingAtomType:
 *
 * Known types of data atoms for cars during a qualifying event.
 **/
typedef enum {
	QUALIFYING_POSITION	= 1,
	QUALIFYING_NUMBER	= 2,
	QUALIFYING_DRIVER	= 3,
	QUALIFYING_PERIOD_1	= 4,
	QUALIFYING_PERIOD_2	= 5,
	QUALIFYING_PERIOD_3	= 6,
	QUALIFYING_SECTOR_1	= 7,
	QUALIFYING_SECTOR_2	= 8,
	QUALIFYING_SECTOR_3	= 9,
	QUALIFYING_LAP		= 10,
	LAST_QUALIFYING
} QualifyingAtomType;


/**
 * SystemPacketType:
 *
 * Known types of packets that aren't related to cars, covering a wide
 * range of different formats and data.
 **/
typedef enum {
	SYS_EVENT_ID		= 1,
	SYS_KEY_FRAME		= 2,
	SYS_VALID_MARKER	= 3,
	SYS_COMMENTARY		= 4,
	SYS_REFRESH_RATE	= 5,
	SYS_NOTICE		= 6,
	SYS_TIMESTAMP		= 7,
	SYS_WEATHER		= 9,
	SYS_SPEED		= 10,
	SYS_TRACK_STATUS	= 11,
	SYS_COPYRIGHT		= 12,
	LAST_SYSTEM_PACKET
} SystemPacketType;
	
/**
 * WeatherPacketType:
 *
 * Sub-types of the SYS_WEATHER packet.
 **/
typedef enum {
	WEATHER_SESSION_CLOCK	= 0,
	WEATHER_TRACK_TEMP	= 1,
	WEATHER_AIR_TEMP	= 2,
	WEATHER_WET_TRACK	= 3,
	WEATHER_WIND_SPEED	= 4,
	WEATHER_HUMIDITY	= 5,
	WEATHER_PRESSURE	= 6,
	WEATHER_WIND_DIRECTION	= 7
} WeatherPacketType;

/**
 * SpeedPacketType:
 *
 * Sub-types of the SYS_SPEED packet.
 **/
typedef enum {
	SPEED_SECTOR1		= 1,
	SPEED_SECTOR2		= 2,
	SPEED_SECTOR3		= 3,
	SPEED_TRAP		= 4,
	FL_CAR			= 5,
	FL_DRIVER		= 6,
	FL_TIME			= 7,
	FL_LAP			= 8
} SpeedPacketType;

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
