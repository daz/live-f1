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


#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "crypt.h"
#include "display.h"
#include "http.h"
#include "packet.h"


/* Forward prototypes */
static int read_decryption_key (unsigned int *decryption_key, const Packet *packet);


/**
 * move_payload:
 * @res[out]: buffer to write result to.
 * @m[in]: model structure.
 * @packet[in]: current packet.
 * @decrypt[in]: true if decryption needed.
 *
 * Moves and optionally decrypts @packet->payload.
 * If @packet->payload is encrypted, checks it after decryption.
 * @res must have space to contain MAX_PACKET_LEN + 1 characters.
 **/
static void
move_payload (unsigned char *res, StateModel *m, const Packet *packet, char decrypt)
{
	int len;

	assert (res && m && packet);

	len = MAX (0, packet->len);
	len = MIN (len, MAX_PACKET_LEN);
	if (len > 0)
		memcpy (res, packet->payload, len);
	res[len] = 0;
	if ((len > 0) && decrypt) {
		decrypt_bytes (m->decryption_key, &m->salt, res, len);
		if (! is_valid_decrypted_data (packet, res)) {
			info (3, _("Decryption failure (car = %d, type = %d, "
			      "data = %d, len = %d, payload = \"%s\")\n"),
			      packet->car, packet->type, packet->data, packet->len,
			      (const char *) res);
			m->decryption_failure = 1;
		}
	}
}

/**
 * pre_handle_car_packet:
 * @r: stream reader structure.
 * @packet: encoded packet structure.
 * @from_frame: true if @packet was received from key frame.
 *
 * Pre-handling of the car-related packet.
 *
 * Returns: 1 if @packet must be pushed to the @r->encrypted_cnum cache,
 * 0 otherwise.
 **/
static int
pre_handle_car_packet (StateReader  *r,
		       const Packet *packet,
		       char from_frame)
{
	assert (r && packet);

	switch ((CarPacketType) packet->type) {
	case CAR_POSITION_UPDATE:
		info (4, _("\tgot CAR_POSITION_UPDATE\n"));
		break;
	case CAR_POSITION_HISTORY:
		info (4, _("\tgot CAR_POSITION_HISTORY\n"));
		break;
	default:
		info (4, _("\tgot CAR_DATA\n"));
		break;
	}
	return 1;
}

/**
 * handle_car_packet:
 * @m: model structure.
 * @packet: encoded packet structure.
 *
 * Decrypts and handles the car-related packet.
 * Checks for decryption failure.
 **/
static void
handle_car_packet (StateModel   *m,
		   const Packet *packet)
{
	unsigned char payload[MAX_PACKET_LEN + 1];

	assert (m && packet);

	move_payload (payload, m, packet, is_crypted (packet));

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
		break;
	case CAR_POSITION_HISTORY:
		/* Currently unhandled */
		info (4, _("\thandle CAR_POSITION_HISTORY\n"));
		break;
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
			strncpy (atom->text, (const char *) payload,
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
				number += payload[i] - '0';
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
	if (! m)
		return;

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

//TODO: description
static int
push_key_packet (StateReader *r)
{
	const Packet *oldp;
	Packet kp;
	int res;

	assert (r);

	kp.car = 0;
	kp.type = USER_SYS_KEY;
	kp.data = 0;
	kp.len = 0;
	kp.at = r->saving_time;

	oldp = get_packet (&r->key_iter);
	if (oldp && (read_decryption_key (NULL, oldp) == -1)) {
		info (5, _("Decryption key packet pushing was skipped\n"));
		return 0;
	}
	to_end_packet (&r->key_iter);
	res = push_packet (r->encrypted_cnum, &kp); //TODO: check errors in caller's code
	info (5, "%s\n",
	      res == 0 ? _("Decryption key packet was pushed to packet cache") :
			 _("Decryption key packet pushing failed"));
	return res;
}

/**
 * pre_handle_system_packet:
 * @r: stream reader structure.
 * @packet: encoded packet structure.
 * @from_frame: true if @packet was received from key frame.
 *
 * Pre-handling of the system packet.
 * Initiates querying a decryption key, total laps or new key frame
 * if required.
 *
 * Returns: 1 if @packet must be pushed to the @r->encrypted_cnum cache,
 * 0 otherwise.
 **/
static int
pre_handle_system_packet (StateReader  *r,
		          const Packet *packet,
			  char from_frame)
{
	assert (r && packet);

	switch ((SystemPacketType) packet->type) {
		unsigned int number, i;

	case SYS_EVENT_ID:
		/* Event Start:
		 * Format: odd byte, then decimal.
		 * Data: type of event.
		 *
		 * Indicates the start of an event, we use this to
		 * obtain the decryption key for the event.
		 */
		info (4, _("\tgot SYS_EVENT_ID\n"));

		push_key_packet (r);

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
		info (4, _("\tgot SYS_KEY_FRAME\n"));
		number = 0;
		i = packet->len;
		while (i) {
			number <<= 8;
			number |= packet->payload[--i];
		}

		/* Decryption key should be obtained only after key frame
		 * receiving. Server returns null key otherwise.
		 */
		/* We shouldn't load already loaded packets
		 * (clear_reader is called on (re)connection and resets
		 * new_frame to 0).
		 */
		//TODO: add loaded_frame field and
		// s/number > r->new_frame/number > r->loaded_frame/
		if ((! r->valid_frame) && (number > r->new_frame)) {
			r->new_frame = number;
			start_get_key_frame (r);
		}
		break;
	case SYS_VALID_MARKER:
		info (4, _("\tgot SYS_VALID_MARKER\n"));
		break;
	case SYS_COMMENTARY:
		info (4, _("\tgot SYS_COMMENTARY\n"));
		break;
	case SYS_REFRESH_RATE:
		info (4, _("\tgot SYS_REFRESH_RATE\n"));
		break;
	case SYS_TIMESTAMP:
		info (4, _("\tgot SYS_TIMESTAMP\n"));
		break;
	case SYS_WEATHER:
		info (4, _("\tgot SYS_WEATHER\n"));
		break;
	case SYS_SPEED:
		info (4, _("\tgot SYS_SPEED\n"));
		break;
	case SYS_TRACK_STATUS:
		info (4, _("\tgot SYS_TRACK_STATUS\n"));
		break;
	case SYS_COPYRIGHT:
		info (4, _("\tgot SYS_COPYRIGHT\n"));
		break;
	case SYS_NOTICE:
		info (4, _("\tgot SYS_NOTICE\n"));
		break;
	default:
		info (3, _("\tgot unknown system packet (type = %d)\n"),
		      packet->type);
		break;
	}
	return 1;
}

//TODO: description
static int
read_decryption_key (unsigned int *decryption_key, const Packet *packet)
{
	assert (packet);

	if (packet->car || (packet->type != USER_SYS_KEY))
		return -2;
	//TODO: without magic codes
	if (packet->data == 1) {
		if (decryption_key)
			*decryption_key = 0;
	} else if (packet->data == 3) {
		if (decryption_key) {
			int i;

			*decryption_key = 0;
			for (i = packet->len; i; --i)
				*decryption_key = (*decryption_key << 8) | packet->payload[i - 1];
		}
	} else
		return -1;
	return 0;
}

//TODO: description
int
write_decryption_key (unsigned int decryption_key, StateReader *r, int cipher)
{
	const Packet *oldp;
	unsigned int old_key, new_key;
	Packet p;
	int i, res;

	if ((! r) || ((cipher == 1) && (! decryption_key)))
		return -1;
	oldp = get_packet (&r->key_iter);
	if (! oldp)
		return -1;
	res = read_decryption_key (&old_key, oldp);
	if (res < -1)
		return -1;
	if (res == 0) {
		int oldcipher = oldp->data >> 1;

		if ((oldcipher == cipher) && (old_key == decryption_key)) {
			info (5, _("Got equal decryption key (%08x)\n"),
			      decryption_key);
			if (oldp->data & 1)
				return 0;
		} else if (oldcipher != cipher) {
			info (0, _("Got different cipher type (old = %d, new = %d)\n"),
			      oldcipher, cipher);
		} else {
			info (0, _("Got different decryption key (old = %08x, new = %08x)\n"),
			      old_key, decryption_key);
		}
	}
	p = *oldp;
	p.data = (cipher << 1) | 1;
	p.len = sizeof (decryption_key);
	for (i = 0, new_key = decryption_key; i < p.len; ++i, new_key >>= 8)
		p.payload[i] = new_key & 0xff;
	res = write_packet (&r->key_iter, &p);
	info (5, _("Decryption key (%08x, cipher = %d) %s\n"),
	      decryption_key, cipher,
	      res == 0 ? _("was written to packet cache") : _("writing failed"));
	return res;
}

/**
 * handle_system_packet:
 * @m: model structure.
 * @packet: encoded packet structure.
 *
 * Decrypts and handles the system packet.
 **/
static void
handle_system_packet (StateModel   *m,
		      const Packet *packet)
{
	unsigned char payload[MAX_PACKET_LEN + 1];

	assert (m && packet);

	move_payload (payload, m, packet, is_crypted (packet));

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
		/* Salt must be resetted before and after every key frame;
		 * every key frame ends with SYS_KEY_FRAME packet,
		 * so we don't need to mark end of frame specially.
		 */
		reset_decryption (&m->salt);
		break;
	case SYS_VALID_MARKER:
		/* Currently unhandled */
		info (4, _("\thandle SYS_VALID_MARKER\n"));
		break;
	case SYS_COMMENTARY:
		info (4, _("\thandle SYS_COMMENTARY\n"));
		/* Commentary can be divided among several packets;
		 * each packet has two-byte prefix:
		 * 0x01 0x00 for first and intermediate chunks,
		 * 0x01 0x01 for last commentary chunk. */
		if (packet->len >= 2)
			add_commentary_chunk ((const char *) &payload[2],
					      (payload[0] != 1) || (payload[1] != 0));
		break;
	case SYS_REFRESH_RATE:
		/* Currently unhandled */
		info (4, _("\thandle SYS_REFRESH_RATE\n"));
		break;
	case SYS_TIMESTAMP:
		info (4, _("\thandle SYS_TIMESTAMP\n"));
		/* Timestamp is stored as little-endian binary number of
		 * seconds from the start of an event */
		if (packet->len > 0) {
			time_t ts = 0;

			for (i = packet->len; i; --i)
				ts = (ts << 8) | payload[i - 1];
			add_timestamp (ts);
		}
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
					if (payload[i] == ':') {
						total *= 60;
						total += number;
						number = 0;
					} else {
						number *= 10;
						number += (payload[i]
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
				number += payload[i] - '0';
			}
			m->track_temp = number;
			update_status (m);
			break;
		case WEATHER_AIR_TEMP:
			number = 0;
			for (i = 0; i < packet->len; i++) {
				number *= 10;
				number += payload[i] - '0';
			}
			m->air_temp = number;
			update_status (m);
			break;
		case WEATHER_WIND_SPEED:
			number = 0;
			for (i = 0; i < packet->len; i++) {
				number *= 10;
				if (payload[i] != '.')
					number += payload[i] - '0';
			}
			m->wind_speed = number;
			update_status (m);
			break;
		case WEATHER_HUMIDITY:
			number = 0;
			for (i = 0; i < packet->len; i++) {
				number *= 10;
				number += payload[i] - '0';
			}
			m->humidity = number;
			update_status (m);
			break;
		case WEATHER_PRESSURE:
			number = 0;
			for (i = 0; i < packet->len; i++) {
				number *= 10;
				if (payload[i] != '.')
					number += payload[i] - '0';
			}
			m->pressure = number;
			update_status (m);
			break;
		case WEATHER_WIND_DIRECTION:
			number = 0;
			for (i = 0; i < packet->len; i++) {
				number *= 10;
				number += payload[i] - '0';
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
		switch (payload[0]) {
		case FL_CAR:
			memcpy(m->fl_car, payload+1, 2);
			update_status (m);
			break;
		case FL_DRIVER:
			memcpy(m->fl_driver, payload+1, 14);
			update_status (m);
			break;
		case FL_TIME:
			memcpy(m->fl_time, payload+1, 8);
			update_status (m);
			break;
		case FL_LAP:
			memcpy(m->fl_lap, payload+1, 2);
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
			m->flag = payload[0] - '0';
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
		info (2, "%s\n", payload);
		break;
	case SYS_NOTICE:
		/* Important System Notice:
		 * Format: string.
		 *
		 * Various important system notices get displayed this way.
		 */
		info (4, _("\thandle SYS_NOTICE\n"));
		info (0, "%s\n", payload);
		break;
	case USER_SYS_TOTAL_LAPS:
		info (4, _("\thandle USER_SYS_TOTAL_LAPS\n"));
		m->total_laps = packet->data;
		break;
	case USER_SYS_KEY:
		info (4, _("\thandle USER_SYS_KEY\n"));
		if (read_decryption_key (&m->decryption_key, packet) == 0)
			info (3, _("Decryption key (%08x, cipher = %d, confidence = %d)"
			      " was loaded from packet cache\n"),
			      m->decryption_key, packet->data >> 1, packet->data & 1);
		break;
	default:
		/* Unhandled event */
		info (4, _("\thandle unknown system packet (type = %d)\n"),
		      packet->type);
		break;
	}
}

//TODO: description
static void
check_keyrev_status (StateReader *r)
{
	int cipher;

	assert (r);

	if (r->key_rev.considered)
		return;
	cipher =
	     r->key_rev.status == KR_STATUS_SUCCESS   ? 1 :
	    (r->key_rev.status == KR_STATUS_PLAINTEXT ? 0 : -1);
	if ((cipher < 0) || (write_decryption_key (r->key_rev.key, r, cipher) != 0))
		return;
	r->key_rev.considered = 1;
	r->current_cipher = cipher;

	info (3, _("Decryption key (%08x, cipher = %d) was reversed\n"),
	      r->key_rev.key, r->current_cipher);
}

//TODO: description
static void
check_cipher_switch (StateReader *r, const Packet *p, char start)
{
	assert (r && p);

	if (! is_crypted (p))
		return;
	if (start && (p->car == 0) && (p->type == SYS_NOTICE))
		return;
	if (r->current_cipher > 0) {
		int cipher =
		     r->key_rev.status == KR_STATUS_SUCCESS   ? 1 :
		    (r->key_rev.status == KR_STATUS_PLAINTEXT ? 0 : -1);

		if (cipher == r->current_cipher)
			return;
		if ((cipher < 0) && (r->current_cipher > 0))
			return;
	}
	/* We suspect every crypted non-start packet in plaintext mode
	 * of switching to encryption mode.
	 */
	r->valid_frame = 0;
	push_key_packet (r);
}

/**
 * pre_handle_packet:
 * @r: stream reader structure.
 * @packet: decoded packet structure.
 * @from_frame: true if @packet was received from key frame.
 *
 * Pre-handling of the @packet.
 * This function is triggered immediately after receiving and parsing data.
 * It isn't called in replay mode.
 * Delegates execution to pre_handle_car_packet or pre_handle_system_packet.
 * If called function returns 1 or from_frame is set,
 * writes @packet to @r->encrypted_cnum cache.
 **/
void
pre_handle_packet (StateReader  *r,
                   const Packet *packet,
		   char from_frame)
{
	static int start = 0; //TODO: make non-static (move to StateReader).
	int crypted;
	int (* pre_handler) (StateReader *, const Packet *, char);

	if ((! r) || (! packet))
		return;

	if (is_reset_decryption_packet (packet))
		start = 1;
	crypted = is_crypted (packet);
	reverse_key (&r->key_rev, packet);
	check_keyrev_status (r);
	check_cipher_switch (r, packet, start);
	if (start && crypted)
		start = 0;

	pre_handler = packet->car ? pre_handle_car_packet : pre_handle_system_packet;
	if (pre_handler (r, packet, from_frame) || from_frame)
		push_packet (r->encrypted_cnum, packet); //TODO: check errors
}

/**
 * handle_packet:
 * @m: model structure.
 * @packet: decoded packet structure.
 *
 * Handles the @packet.
 * This function is called for @m modification and further visualization
 * and may be delayed (see StateModel::time_gap and StateModel::replay_gap).
 * Delegates execution to handle_car_packet or handle_system_packet.
 **/
void
handle_packet (StateModel   *m,
               const Packet *packet)
{
	if ((! m) || (! packet))
		return;
	if (packet->car)
		handle_car_packet (m, packet);
	else
		handle_system_packet (m, packet);
}
