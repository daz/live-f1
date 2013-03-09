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

#include <event2/dns.h>
#include <event2/event.h>
#include <event2/util.h>

#include "keyrev.h"
#include "macros.h"
#include "packetcache.h"

/* Default hostname to contact */
#define DEFAULT_HOST      "live-timing.formula1.com"

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
 * ObtainingStatus:
 *
 * These flags indicate obtaining data from other source than
 * live timing server.
 **/
typedef enum {
	OBTAINING_AUTH      = 1,
	OBTAINING_CONNECT   = 2, /* reserved */
	OBTAINING_FRAME     = 4,
	OBTAINING_KEY       = 8,
	OBTAINING_TOTALLAPS = 16, /* reserved */
	OBTAINING_ALL       = 31,
	OBTAINING_LAST      = 32
} ObtainingStatus;

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
 * @periodic: periodic event.
 * @input_cnum: number of packet cache containing data received from
 * the data server.
 * @encrypted_cnum: number of packet cache containing encrypted data
 * received either from the data server or a key frame.
 * @key_iter: iterator pointed to USER_SYS_KEY packet
 * (in @encrypted_cnum cache) to save received or reversed decryption key to.
 * @key_rev: key reversing state structure.
 * @key_request_failure: true if last key request has failed.
 * @current_cipher: currently used cipher (0 for plaintext).
 * @valid_frame: false if frame loading is required.
 *
 * @host: hostname to contact.
 * @auth_host: authorisation host to contact.
 *
 * @email: user's e-mail address.
 * @password: user's password.
 * @cookie: user's authorisation cookie.
 * @stop_handling_reason: reason of suspension of moving from @input_cnum
 * to @encrypted_cnum cache (see ObtainingStatus).
 * @obtaining: obtaining status (see ObtainingStatus), includes blocking
 * (stop_handling_reason) and non-blocking (other) obtainings.
 * @pending: pending requests (see ObtainingStatus).
 * @saving_time: timestamp for parsing packets (packets received
 * from a key frame will get timestamp of this keyframe packet (@saving_time
 * freezes during querying key frame), packets received from the live timing
 * server will get current timestamp).
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
	struct event                     *periodic;
	int                               input_cnum;
	int                               encrypted_cnum;
	PacketIterator                    key_iter;
	KeyReverser                       key_rev;
	char                              key_request_failure; /*bool*/
	int                               current_cipher;
	char                              valid_frame; /*bool*/

	char                             *host, *auth_host;
	unsigned int                      port;
	char                             *email, *password, *cookie;

	int                               stop_handling_reason;
	int                               obtaining;
	int                               pending;

	time_t                            saving_time;

	unsigned int                      new_frame;
	unsigned int                      new_event_no;
	EventType                         new_event_type;
} StateReader;

/**
 * StateModel:
 * @iter: iterator pointed to current packet in the
 * StateReader::encrypted_cnum cache.
 * @decryption_key: current decryption key.
 * @salt: current decryption salt.
 * @decryption_failure: indicates if payload decryption has failed.
 * @event_type: current event type (practice/qualifying/race).
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
 * Model structure used for decryption and visualization.
 **/
typedef struct {
	PacketIterator     iter;
	unsigned int       decryption_key, salt;
	char               decryption_failure; /*bool*/

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
