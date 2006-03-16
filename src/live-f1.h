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

#ifndef LIVE_F1_H
#define LIVE_F1_H

#include <gettext.h>
#include <time.h>

#include "macros.h"


/* Default hostnames to contact */
#define DEFAULT_HOST      "live-timing.formula1.com"
#define DEFAULT_AUTH_HOST "secure.formula1.com"

/* Make gettext a little friendlier */
#define _(_str) gettext (_str)
#define N_(_str) gettext_noop (_str)


/**
 * EventType:
 *
 * Type of event, used to attach meaning to the data when displaying it
 * in a table.
 **/
typedef enum {
	RACE_EVENT = 1,
	PRACTICE_EVENT = 2,
	QUALIFYING_EVENT = 3
} EventType;

/**
 * FlagStatus:
 *
 * Current track status or flag.
 **/
typedef enum {
	GREEN_FLAG = 1,
	YELLOW_FLAG,
	SAFETY_CAR_STANDBY,
	SAFETY_CAR_DEPLOYED,
	RED_FLAG,
	LAST_FLAG
} FlagStatus;


/**
 * CarAtom:
 * @data: data associated with atom,
 * @text: content of atom.
 *
 * Used to hold the current information about a car, there is one CarAtom
 * for each car for each possible packet type that can be received from
 * the server.
 **/
typedef struct {
	int  data;
	char text[16];
} CarAtom;

/**
 * CurrentState:
 * @host: hostname to contact,
 * @auth_host: authorisation host to contact,
 * @email: user's e-mail address,
 * @password: user's password,
 * @cookie: user's authorisation cookie,
 * @key: decryption key,
 * @salt: current decryption salt,
 * @frame: last seen key frame,
 * @event_no: event number,
 * @event_type: event type,
 * @remaining_time: time remaining for the event,
 * @epoch_time: epoch time @remaining_time was updated,
 * @end_time: time the session will end,
 * @lap: current lap,
 * @flag: track status or flag,
 * @num_cars: number of cars in the event,
 * @car_position: current position of car,
 * @car_info: arrays of information about each car.
 *
 * Holds the current application state so we don't need to pass around
 * a lot of variables or keep them globally.
 **/
typedef struct {
	char          *host, *auth_host;
	char          *email, *password, *cookie;
	unsigned int   key, salt;
	unsigned int   frame;

	unsigned int   event_no;
	EventType      event_type;
	time_t         remaining_time, epoch_time;
	unsigned int   lap;
	FlagStatus     flag;

	int            num_cars;
	int           *car_position;
	CarAtom      **car_info;
} CurrentState;


SJR_BEGIN_EXTERN

/* Program's name */
const char *program_name;


int info (int irrelevance, const char *format, ...);

SJR_END_EXTERN

#endif /* LIVE_F1_H */
