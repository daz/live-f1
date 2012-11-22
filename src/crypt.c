/* live-f1
 *
 * crypt.c - encipherment and decrypted text checking functions
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
#include <regex.h>

#include "crypt.h"


/* Encryption seed */
#define CRYPTO_SEED 0x55555555


/**
 * decrypt_bytes:
 * @decryption_key: decryption key.
 * @salt: pointer to current decryption salt.
 * @buf: buffer to decrypt.
 * @len: number of bytes in @buf to decrypt.
 *
 * Decrypts the initial @len bytes of @buf modifying the buffer given,
 * rather than returning a new string.
 **/
void
decrypt_bytes (unsigned int   decryption_key,
	       unsigned int  *salt,
	       unsigned char *buf,
	       size_t         len)
{
	if ((! decryption_key) || (! salt))
		return;

	while (len--) {
		*salt = ((*salt >> 1) ^ (*salt & 0x01 ? decryption_key : 0));
		*(buf++) ^= (*salt & 0xff);
	}
}

/**
 * reset_decryption:
 * @salt: pointer to decryption salt.
 *
 * Resets the decryption salt to the initial seed; this begins the
 * cycle again.
 **/
void
reset_decryption (unsigned int *salt)
{
	if (salt)
		*salt = CRYPTO_SEED;
}

/**
 * is_valid_decrypted_data:
 * @packet: pointer to checked packet.
 * @payload: checked packet's payload
 *
 * Checks for decryption failure.
 * @payload may be equal to @packet->payload or point to another buffer
 * (e.g. @packet->payload stores encrypted text and @payload stores decrypted
 * text).
 *
 * Returns: 1 for successful checking result, 0 for decryption failure.
 **/
int
is_valid_decrypted_data (const Packet *packet, const unsigned char *payload)
{
	/* We use one-time regular expression compilation on demand */
	static regex_t *re = NULL;

	if (! packet)
		return 0;

	if (! packet->car)
		return 1;
	else if ((packet->type == 1) && (packet->len >= 0)) {
		if (! re) {
			re = malloc (sizeof (*re));
			if (! re)
				return 0;

#if ((! defined MAX_CAR_NUMBER) || (MAX_CAR_NUMBER >= 100))
	#error "Please define correct MAX_CAR_NUMBER and/or correct the regular expression below."
#endif

			regcomp(re, "^[1-9][0-9]?$|^$", REG_EXTENDED|REG_NOSUB);
		}

		if (regexec(re, (const char *) payload, 0, NULL, 0) != 0)
			return 0;
	}

	return 1;
}

/**
 * is_crypted:
 * @packet: pointer to checked packet.
 *
 * Checks if @packet is encrypted or not.
 * This function doesn't analyse packet payload, it looks at packet type only.
 *
 * Returns: 1 if @packet is encrypted, 0 otherwise.
 **/
int
is_crypted (const Packet *packet)
{
	if (! packet)
		return 0;
	if (! packet->car)
		switch ((SystemPacketType) packet->type) {
		case SYS_TIMESTAMP:
		case SYS_WEATHER:
		case SYS_TRACK_STATUS:
		case SYS_COMMENTARY:
		case SYS_NOTICE:
		case SYS_SPEED:
			return 1;
		default:
			return 0;
		}
	else if ((packet->car > 0) && (packet->car <= MAX_CAR_NUMBER))
		return (CarPacketType) packet->type != CAR_POSITION_UPDATE;
	return 0;
}
