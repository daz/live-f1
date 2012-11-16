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

#include <event2/buffer.h>
#include <event2/dns.h>
#include <event2/event.h>
#include <event2/util.h>

#include "macros.h"
#include "packetcache.h"

/* Default hostnames to contact */
#define DEFAULT_HOST      "live-timing.formula1.com"
#define WEBSERVICE_HOST   "live-f1.puseyuk.co.uk"

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
 * StopHandlingReason:
 *
 * Reason of parse stopping.
 * Parsing process stops when we wait for receiving data
 * from other source than live timing server.
 **/
typedef enum {
	STOP_HANDLING_REASON_AUTH      = 1,
	STOP_HANDLING_REASON_CONNECT   = 2, /*reserved*/
	STOP_HANDLING_REASON_FRAME     = 4,
	STOP_HANDLING_REASON_KEY       = 8,
	STOP_HANDLING_REASON_TOTALLAPS = 16
} StopHandlingReason;

/**
 * CarAtom:
 * @data: data associated with atom.
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
 * StateReader:
 * @replay_mode: replay mode flag (0 for live mode).
 * @base: event base (asynchronous event pool).
 * @dnsbase: DNS event base.
 * @gaireq: asynchronous getaddrinfo request.
 * @addr_head: pointer to first element in list returned by
 * asynchronous getaddrinfo request.
 * @addr: pointer to current element in list headed by @addr_head.
 * @input: buffer containing non-parsed data obtained
 * either from the data server or a key frame.
 *
 * @host: hostname to contact.
 * @auth_host: authorisation host to contact.
 *
 * @email: user's e-mail address.
 * @password: user's password.
 * @cookie: user's authorisation cookie.
 * @decryption_key: decryption key.
 * @salt: current decryption salt.
 * @decryption_failure: indicates if payload decryption has failed.
 * @stop_handling_reason: reason of parsing suspension
 * (see StopHandlingReason).
 * @saving_time: timestamp for parsing packets (packets received
 * from a key frame will get timestamp of this keyframe packet (@saving_time
 * freezes during querying key frame), packets received from the live timing
 * server will get current timestamp).
 * @frame: last seen key frame.
 * @new_frame: querying key frame.
 * @new_event_no: new event number (used for logging only).
 * @new_event_type: new event type (used for logging only).
 *
 * Stream reader state. Used for storing event loop and for network operations
 * (authenticating, connecting, receiving data, storing received data etc.).
 **/
typedef struct {
	char                              replay_mode; /*bool*/

	struct event_base                *base;
	struct evdns_base                *dnsbase;
	struct evdns_getaddrinfo_request *gaireq;
	struct evutil_addrinfo           *addr_head, *addr;
	struct evbuffer                  *input;
	struct event                     *periodic;

	char                             *host, *auth_host;
	unsigned int                      port;
	char                             *email, *password, *cookie;
	unsigned int                      decryption_key, salt;
	char                              decryption_failure; /*bool*/

	//TODO: name as stop_parsing_reason ? (also STOP_PARSING_REASON_...)
	int                               stop_handling_reason;
	time_t                            saving_time;

	unsigned int                      frame, new_frame;

	unsigned int                      new_event_no;
	EventType                         new_event_type;
} StateReader;

/**
 * StateModel:
 * @iter: iterator pointed to current packet in the cache.
 * @paused: indicates whether visualization is paused.
 * @replay_gap: time gap between starting time of saving the replay
 * and starting time of replaying (0 for live mode).
 * @time_gap: time gap between data reading and visualization
 * (for replay mode true time gap is equal to @time_gap + @replay_gap).
 * @last_time_gap: last value of @time_gap before resetting it to 0.
 * @paused_time: time when visualization was paused (0 if visualization
 * is not paused).
 * @model_time: current model time (program visualizes state at this
 * moment of the race).
 * @remaining_time: time remaining for the event (since @epoch_time moment).
 * @epoch_time: epoch time @remaining_time was updated.
 * @laps_completed: the number of laps completed during the race.
 * @total_laps: the total number of laps for the grand prix.
 * @flag: track status or flag.
 * @track_temp: current track temperature (degrees C).
 * @air_temp: current air temperature (degrees C).
 * @humidity: current humidity (percentage).
 * @wind_speed: current wind speed (meters per second).
 * @wind_direction: current wind direction (destination in degrees).
 * @pressure: current barometric pressure (millibars).
 * @fl_car: fastest lap (car number).
 * @fl_driver: fastest lap (driver's name).
 * @fl_time: fastest lap (lap time).
 * @fl_lap: fastest lap (lap number).
 * @num_cars: number of cars in the event.
 * @car_position: current position of each car.
 * @car_info: arrays of information about each car.
 *
 * Model structure used for visualization.
 **/
typedef struct {
	PacketIterator     iter;

	EventType          event_type;

	char               paused; /*bool*/
	time_t             replay_gap;
	time_t             time_gap, last_time_gap, paused_time;
	time_t             model_time, remaining_time, epoch_time;
	unsigned int       laps_completed, total_laps;
	FlagStatus         flag;

	int                track_temp, air_temp, humidity;
	int                wind_speed, wind_direction, pressure;

	char              *fl_car, *fl_driver, *fl_time, *fl_lap;
	
	int                num_cars;
	int               *car_position;
	CarAtom          **car_info;
} StateModel;

/**
 * CurrentState:
 * @r: stream reader structure.
 * @m: (race/practice/qualifying) model structure.
 *
 * Holds the current application state.
 **/
typedef struct {
	StateReader r;
	StateModel  m;
} CurrentState;


SJR_BEGIN_EXTERN

/* Program's name */
const char *program_name;


void start_loopexit (struct event_base *base);

int info (int irrelevance, const char *format, ...);

SJR_END_EXTERN

#endif /* LIVE_F1_H */
