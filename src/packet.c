/* live-f1
 *
 * packet.c - individual packet handling
 *
 * Copyright © 2009 Scott James Remnant <scott@netsplit.com>.
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


#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "live-f1.h"
#include "display.h"
#include "stream.h"
#include "packet.h"


/**
 * pre_handle_car_packet:
 * @r: stream reader structure.
 * @packet: decoded packet structure.
 *
 * Pre-handling of the car-related packet.
 * Checks for decryption failure.
 **/
static void
pre_handle_car_packet (StateReader  *r,
		       const Packet *packet)
{
	switch ((CarPacketType) packet->type) {
	case CAR_POSITION_UPDATE:
		info (4, _("\tgot CAR_POSITION_UPDATE (rest = %u)\n"),
		      evbuffer_get_length (r->input));
		return;
	case CAR_POSITION_HISTORY:
		info (4, _("\tgot CAR_POSITION_HISTORY (rest = %u)\n"),
		      evbuffer_get_length (r->input));
		return;
	default:
		info (4, _("\tgot CAR_DATA (rest = %u)\n"),
		      evbuffer_get_length (r->input));

		/* Check for decryption failure */

		if ((packet->type == 1) && (packet->len >= 0)) {
			regex_t re;

			regcomp(&re, "^[1-9][0-9]?$|^$", REG_EXTENDED|REG_NOSUB);

			if (regexec(&re, packet->payload, (size_t)0, NULL, 0) != 0) {
				info (3, _("Decryption failure\n"));
				r->decryption_failure = 1;
			}

			regfree (&re);
		}
		break;
	}
}

/**
 * handle_car_packet:
 * @m: model structure.
 * @packet: decoded packet structure.
 *
 * Handle the car-related packet.
 **/
static void
handle_car_packet (StateModel   *m,
		   const Packet *packet)
{
	/* Check whether a new car joined the event; actually, this is
	 * because we never know in advance how many cars there are, and
	 * things like practice sessions can probably have more than the
	 * usual twenty.  (Or we might get another team in the future).
	 *
	 * Then increase the size of all the arrays and clear the board.
	 */
	if (packet->car > m->num_cars) {
		int i, j;

		m->car_position = realloc (m->car_position,
					   sizeof (int) * packet->car);
		m->car_info = realloc (m->car_info,
				       sizeof (CarAtom *) * packet->car);
		if ((! m->car_position) || (! m->car_info))
			abort ();

		for (i = m->num_cars; i < packet->car; i++) {
			m->car_position[i] = 0;
			m->car_info[i] = malloc (sizeof (CarAtom) * LAST_CAR_PACKET);
			if (! m->car_info[i])
				abort ();

			for (j = 0; j < LAST_CAR_PACKET; j++)
				memset (&m->car_info[i][j], 0,
					sizeof (CarAtom));
		}

		m->num_cars = packet->car;
		clear_board (m);
	}

	switch ((CarPacketType) packet->type) {
		CarAtom *atom;
		int      i;

	case CAR_POSITION_UPDATE:
		/* Position Update:
		 * Data: new position.
		 *
		 * This is a non-atom packet that indicates that the
		 * race position of a car has changed.  They often seem
		 * to come in pairs, the first one with a zero position,
		 * and the next with the new position, but not always
		 * sadly.
		 */
		info (4, _("\thandle CAR_POSITION_UPDATE\n"));
		clear_car (m, packet->car);
		for (i = 0; i < m->num_cars; i++)
			if (m->car_position[i] == packet->data)
				m->car_position[i] = 0;

		m->car_position[packet->car - 1] = packet->data;
		if (packet->data)
			update_car (m, packet->car);
		return;
	case CAR_POSITION_HISTORY:
		/* Currently unhandled */
		info (4, _("\thandle CAR_POSITION_HISTORY\n"));
		return;
	default:
		/* Data Atom:
		 * Format: string.
		 * Data: colour.
		 *
		 * Each of these events updates a particular piece of data
		 * for the car, some (with no length) only update the colour
		 * of a field.
		 */

		info (4, _("\thandle CAR_DATA\n"));

		/* Store the atom */

		atom = &m->car_info[packet->car - 1][packet->type];
		atom->data = packet->data;
		if (packet->len >= 0) {
			strncpy (atom->text, (const char *) packet->payload,
			         sizeof (atom->text));
			atom->text[sizeof (atom->text) - 1] = 0;
		}

		update_cell (m, packet->car, packet->type);

		/* This is the only way to grab this information, sadly */
		if ((m->event_type == RACE_EVENT)
		    && (m->car_position[packet->car - 1] == 1)
		    && (packet->type == RACE_INTERVAL)) {
			unsigned int number = 0;

			for (i = 0; i < packet->len; i++) {
				number *= 10;
				number += packet->payload[i] - '0';
			}

			m->laps_completed = number;
			update_status (m);
		}
		break;
	}
}

/**
 * clear_model:
 * @m: model structure.
 *
 * Clears model structure at start of a new event
 * (practice, qualifying or race).
 **/
void
clear_model (StateModel *m)
{
	m->remaining_time = 0;
	m->epoch_time = 0;
	m->laps_completed = 0;
	m->flag = GREEN_FLAG;

	m->track_temp = 0;
	m->air_temp = 0;
	m->humidity = 0;
	m->wind_speed = 0;
	m->wind_direction = 0;
	m->pressure = 0;

	if (m->fl_car) free (m->fl_car);
	m->fl_car = calloc(3, sizeof(char));
	if (m->fl_driver) free (m->fl_driver);
	m->fl_driver = calloc(15, sizeof(char));
	if (m->fl_time) free (m->fl_time);
	m->fl_time = calloc(9, sizeof(char));
	if (m->fl_lap) free (m->fl_lap);
	m->fl_lap = calloc(3, sizeof(char));
	
	if (m->car_position) {
		free (m->car_position);
		m->car_position = NULL;
	}
	if (m->car_info) {
		int i;

		for (i = 0; i < m->num_cars; i++)
			if (m->car_info[i]) {
				free (m->car_info[i]);
				m->car_info[i] = NULL;
			}
		free (m->car_info);
		m->car_info = NULL;
	}
	m->num_cars = 0;
}

/**
 * pre_handle_system_packet:
 * @r: stream reader structure.
 * @packet: decoded packet structure.
 *
 * Pre-handling of the system packet.
 * Initiates querying a decryption key, total laps or new key frame
 * if required.
 **/
static void
pre_handle_system_packet (StateReader  *r,
		          const Packet *packet)
{
	switch ((SystemPacketType) packet->type) {
		unsigned int number, i;
		char decryption_failure;

	case SYS_EVENT_ID:
		/* Event Start:
		 * Format: odd byte, then decimal.
		 * Data: type of event.
		 *
		 * Indicates the start of an event, we use this to
		 * obtain the decryption key for the event.
		 */
		info (4, _("\tgot SYS_EVENT_ID (rest = %u)\n"),
		      evbuffer_get_length (r->input));
		number = 0;
		for (i = 1; i < packet->len; i++) {
			number *= 10;
			number += packet->payload[i] - '0';
		}
		r->new_event_no = number;
		r->new_event_type = packet->data;
		start_get_decryption_key (r);
		start_get_total_laps (r);

		break;
	case SYS_KEY_FRAME:
		/* Key Frame Marker:
		 * Format: little-endian integer.
		 *
		 * If we've not yet encountered a key frame, we need to
		 * load this to get up to date.  Otherwise we just set
		 * our counter and carry on.
		 */
		info (4, _("\tgot SYS_KEY_FRAME (rest = %u)\n"),
		      evbuffer_get_length (r->input));
		number = 0;
		i = packet->len;
		while (i) {
			number <<= 8;
			number |= packet->payload[--i];
		}

		decryption_failure = r->decryption_failure;
		reset_decryption (r);
		if (! r->decryption_key)
			start_get_decryption_key (r);
		if ((! r->frame) || decryption_failure) {
			r->new_frame = number;
			start_get_key_frame (r);
		} else
			r->frame = number;

		break;
	case SYS_VALID_MARKER:
		info (4, _("\tgot SYS_VALID_MARKER (rest = %u)\n"),
		      evbuffer_get_length (r->input));
		break;
	case SYS_COMMENTARY:
		info (4, _("\tgot SYS_COMMENTARY (rest = %u)\n"),
		      evbuffer_get_length (r->input));
		break;
	case SYS_REFRESH_RATE:
		info (4, _("\tgot SYS_REFRESH_RATE (rest = %u)\n"),
		      evbuffer_get_length (r->input));
		break;
	case SYS_TIMESTAMP:
		info (4, _("\tgot SYS_TIMESTAMP (rest = %u)\n"),
		      evbuffer_get_length (r->input));
		break;
	case SYS_WEATHER:
		info (4, _("\tgot SYS_WEATHER (rest = %u)\n"),
		      evbuffer_get_length (r->input));
		break;
	case SYS_SPEED:
		info (4, _("\tgot SYS_SPEED (rest = %u)\n"),
		      evbuffer_get_length (r->input));
		break;
	case SYS_TRACK_STATUS:
		info (4, _("\tgot SYS_TRACK_STATUS (rest = %u)\n"),
		      evbuffer_get_length (r->input));
		break;
	case SYS_COPYRIGHT:
		info (4, _("\tgot SYS_COPYRIGHT (rest = %u)\n"),
		      evbuffer_get_length (r->input));
		break;
	case SYS_NOTICE:
		info (4, _("\tgot SYS_NOTICE (rest = %u)\n"),
		      evbuffer_get_length (r->input));
		break;
	default:
		info (4, _("\tgot SYS_UNKNOWN (rest = %u)\n"),
		      evbuffer_get_length (r->input));
		break;
	}
}

/**
 * handle_system_packet:
 * @m: model structure.
 * @packet: decoded packet structure.
 *
 * Handle the system packet.
 **/
static void
handle_system_packet (StateModel   *m,
		      const Packet *packet)
{
	switch ((SystemPacketType) packet->type) {
		unsigned int number, i;

	case SYS_EVENT_ID:
		/* Event Start:
		 * Format: odd byte, then decimal.
		 * Data: type of event.
		 *
		 * Indicates the start of an event, we use this to set up
		 * the board properly.
		 */
		info (4, _("\thandle SYS_EVENT_ID\n"));
		m->event_type = packet->data;
		clear_model (m);

		break;
	case SYS_KEY_FRAME:
		info (4, _("\thandle SYS_KEY_FRAME\n"));
		break;
	case SYS_VALID_MARKER:
		/* Currently unhandled */
		info (4, _("\thandle SYS_VALID_MARKER\n"));
		break;
	case SYS_COMMENTARY:
		/* Currently unhandled */
		info (4, _("\thandle SYS_COMMENTARY\n"));
		break;
	case SYS_REFRESH_RATE:
		/* Currently unhandled */
		info (4, _("\thandle SYS_REFRESH_RATE\n"));
		break;
	case SYS_TIMESTAMP:
		/* Currently unhandled */
		info (4, _("\thandle SYS_TIMESTAMP\n"));
		break;
	case SYS_WEATHER:
		/* Weather Information:
		 * Format: decimal or string.
		 * Data: field to be updated.
		 *
		 * Indicates a change in the weather; the data field
		 * indicates which piece of information to change, the
		 * payload always contains the printed value.  This can be
		 * combined with the SYS_TIMESTAMP packet to record the
		 * changing of the data over time.
		 */
		info (4, _("\thandle SYS_WEATHER\n"));
		switch (packet->data) {
		case WEATHER_SESSION_CLOCK:
			/* Session time remaining.
			 * This is only sent once a minute in H:MM:SS format,
			 * we therefore parse it and use it to update our
			 * own record of the amount of time remaining in the
			 * session.
			 *
			 * It also appears to be sent with a -1 length to
			 * indicate the passing of the minute; we use the
			 * first one of these to indicate the start of a
			 * session.
			 */
			if (packet->len > 0) {
				unsigned int total;

				total = number = 0;
				for (i = 0; i < packet->len; i++) {
					if (packet->payload[i] == ':') {
						total *= 60;
						total += number;
						number = 0;
					} else {
						number *= 10;
						number += (packet->payload[i]
							   - '0');
					}
				}
				total *= 60;
				total += number;

				m->epoch_time = m->model_time;
				m->remaining_time = total;
			} else {
				m->epoch_time = m->model_time;
			}

			update_time (m);
			break;
		case WEATHER_TRACK_TEMP:
			number = 0;
			for (i = 0; i < packet->len; i++) {
				number *= 10;
				number += packet->payload[i] - '0';
			}
			m->track_temp = number;
			update_status (m);
			break;
		case WEATHER_AIR_TEMP:
			number = 0;
			for (i = 0; i < packet->len; i++) {
				number *= 10;
				number += packet->payload[i] - '0';
			}
			m->air_temp = number;
			update_status (m);
			break;
		case WEATHER_WIND_SPEED:
			number = 0;
			for (i = 0; i < packet->len; i++) {
				number *= 10;
				if (packet->payload[i] != '.')
					number += packet->payload[i] - '0';
			}
			m->wind_speed = number;
			update_status (m);
			break;
		case WEATHER_HUMIDITY:
			number = 0;
			for (i = 0; i < packet->len; i++) {
				number *= 10;
				number += packet->payload[i] - '0';
			}
			m->humidity = number;
			update_status (m);
			break;
		case WEATHER_PRESSURE:
			number = 0;
			for (i = 0; i < packet->len; i++) {
				number *= 10;
				if (packet->payload[i] != '.')
					number += packet->payload[i] - '0';
			}
			m->pressure = number;
			update_status (m);
			break;
		case WEATHER_WIND_DIRECTION:
			number = 0;
			for (i = 0; i < packet->len; i++) {
				number *= 10;
				number += packet->payload[i] - '0';
			}
			m->wind_direction = number;
			update_status (m);
			break;
		default:
			/* Unhandled field */
			break;
		}
		break;
	case SYS_SPEED:
		/* Speed and Fastest Lap data:
		 * Format: single byte, then string.
		 *
		 * The first payload byte indicates which piece of
		 * information to change.
		 */
		info (4, _("\thandle SYS_SPEED\n"));
		switch (packet->payload[0]) {
		case FL_CAR:
			memcpy(m->fl_car, packet->payload+1, 2);
			update_status (m);
			break;
		case FL_DRIVER:
			memcpy(m->fl_driver, packet->payload+1, 14);
			update_status (m);
			break;
		case FL_TIME:
			memcpy(m->fl_time, packet->payload+1, 8);
			update_status (m);
			break;
		case FL_LAP:
			memcpy(m->fl_lap, packet->payload+1, 2);
			update_status (m);
			break;
		default:
			/* Unhandled field */
			break;
		}
		break;
	case SYS_TRACK_STATUS:
		/* Track Status:
		 * Format: decimal.
		 * Data: field to be updated.
		 *
		 * Indicates a change in the status of the track; the data
		 * field indicates which piece of information to change.
		 */
		info (4, _("\thandle SYS_TRACK_STATUS\n"));
		switch (packet->data) {
		case 1:
			/* Flag currently in effect.
			 * Decimal enum value.
			 */
			m->flag = packet->payload[0] - '0';
			update_status (m);
			break;
		default:
			/* Unhandled field */
			break;
		}
		break;
	case SYS_COPYRIGHT:
		/* Copyright Notice:
		 * Format: string.
		 *
		 * Plain text copyright notice in the start of the feed.
		 */
		info (4, _("\thandle SYS_COPYRIGHT\n"));
		info (2, "%s\n", packet->payload);
		break;
	case SYS_NOTICE:
		/* Important System Notice:
		 * Format: string.
		 *
		 * Various important system notices get displayed this way.
		 */
		info (4, _("\thandle SYS_NOTICE\n"));
		info (0, "%s\n", packet->payload);
		break;
	case USER_SYS_TOTAL_LAPS:
		info (4, _("\thandle USER_SYS_TOTAL_LAPS\n"));
		m->total_laps = packet->data;
		break;
	default:
		/* Unhandled event */
		info (4, _("\thandle SYS_UNKNOWN (type = %d)\n"), packet->type);
		break;
	}
}

/**
 * pre_handle_packet:
 * @r: stream reader structure.
 * @packet: decoded packet structure.
 *
 * Pre-handling of the @packet.
 * This function is triggered immediately after parsing and
 * getting new packet. It isn't called in replay mode.
 * Delegates execution to pre_handle_car_packet or pre_handle_system_packet.
 **/
void
pre_handle_packet (StateReader  *r,
                   const Packet *packet)
{
	if (packet->car)
		pre_handle_car_packet (r, packet);
	else
		pre_handle_system_packet (r, packet);
}

/**
 * handle_packet:
 * @m: model structure.
 * @packet: decoded packet structure.
 *
 * Handle the @packet.
 * This function is called for @m modification and further visualization
 * and may be delayed (see StateModel::time_gap and StateModel::replay_gap).
 * Delegates execution to handle_car_packet or handle_system_packet.
 **/
void
handle_packet (StateModel   *m,
               const Packet *packet)
{
	if (packet->car)
		handle_car_packet (m, packet);
	else
		handle_system_packet (m, packet);
}
