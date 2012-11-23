/* live-f1
 *
 * keyrev.c - key reversing
 *
 * Copyright © 2012 Yuriy Mishkov <ymishkov@gmail.com>.
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
#include <regex.h>
#include <stdlib.h>
#include <string.h>

#include "crypt.h"
#include "keyrev.h"


/**
 * reset_reverser:
 * @krev: key reverser state.
 *
 * Resets @krev.
 **/
void
reset_reverser (KeyReverser *krev)
{
	if (! krev)
		return;
	krev->key = 0x80000000;
	reset_decryption (&krev->salt);
	krev->mask = 0;
	krev->status = KR_STATUS_START;
	krev->pos = 0;
}

/**
 * replay_reverser:
 * @krev: key reverser state.
 *
 * Replays @krev->salt for @krev->pos characters
 * if @krev->key approximation was corrected.
 **/
static void
replay_reverser (KeyReverser *krev)
{
	size_t i;

	assert (krev);
	reset_decryption (&krev->salt);
	for (i = 0; i <= krev->pos; ++i)
		krev->salt = (krev->salt >> 1) ^ (krev->salt & 0x01 ? krev->key : 0);
}

/**
 * first_character:
 * @krev: key reverser state.
 * @diff: XOR difference between first encrypted character and
 * first known-plaintext character.
 *
 * Makes first @krev->key and @krev->salt approximation
 * (8 least significant bits).
 **/
static void
first_character (KeyReverser *krev, unsigned char diff)
{
	assert (krev);
	assert (krev->salt & 0x01);
	krev->salt >>= 1;
	krev->key = diff ^ krev->salt ^ krev->key;
	krev->mask = 0xff;
	krev->salt = krev->salt ^ krev->key;
}

/**
 * next_character:
 * @krev: key reverser state.
 * @diff: XOR difference between encrypted character and
 * known-plaintext character.
 * @strict: make strict checking.
 *
 * Reversing method bases on the fact that every decrypted symbol
 * has zero most significant bit (except SYS_TIMESTAMP payload and
 * some characters in SYS_COMMENTARY payload).
 * If @strict == 0 then this function ignores all bits in @diff except msb,
 * otherwise strict checking is maked.
 **/
static void
next_character (KeyReverser *krev, unsigned char diff, char strict)
{
	unsigned char last;

	assert (krev);
	last = krev->salt & 0x01;
	krev->salt >>= 1;
	if (last)
		krev->salt = krev->salt ^ krev->key;
	if (strict && ((diff & 0x7f) != (krev->salt & 0x7f))) {
		krev->status = KR_STATUS_FAILURE;
		return;
	}
	if ((diff & 0x80) != (krev->salt & 0x80)) {
		krev->key = krev->key ^ (krev->mask + 1);
		replay_reverser (krev);
	}
	if ((diff & 0x80) != (krev->salt & 0x80)) {
		krev->status = KR_STATUS_FAILURE;
		return;
	}
	krev->mask = (krev->mask << 1) | 0x01;
	if ((krev->mask & krev->key) == krev->key)
		krev->status = KR_STATUS_SUCCESS;
}

#if ((! defined MAX_CAR_NUMBER) || (MAX_CAR_NUMBER >= 128))
	#error "If MAX_CAR_NUMBER >= 128 then msb can be equal to 1 in some car packets."
#endif

/**
 * act_reverser:
 * @krev: key reverser state.
 * @p: current packet.
 *
 * Makes real reversing action.
 **/
static void
act_reverser (KeyReverser *krev, const Packet *p)
{
	static const char *start_phrase = "Please Wait ...";
	static regex_t    *re = NULL;
	size_t             count = 0;

	assert (krev);
	assert (p);
	if ((p->len <= 0) ||
	    ((krev->status != KR_STATUS_START) && (krev->status != KR_STATUS_IN_PROGRESS)))
		return;

	count = MIN (p->len, MAX_PACKET_LEN);
	if (krev->status == KR_STATUS_START) {
		if (! re) {
			re = malloc (sizeof (*re));
			if (! re) {
				krev->status = KR_STATUS_FAILURE;
				return;
			}
			regcomp(re, "^img:", REG_EXTENDED|REG_NOSUB);
		}
		if (p->car || (p->type != SYS_NOTICE))
			krev->status = KR_STATUS_FAILURE;
		else if (count != strlen (start_phrase)) {
			if (regexec(re, (const char *) p->payload, 0, NULL, 0) == 0)
				krev->status = KR_STATUS_PLAINTEXT;
			else
				krev->status = KR_STATUS_FAILURE;
		} else {
			size_t i = 0;

			if (krev->salt & 0x01) {
				first_character (krev, p->payload[i] ^ start_phrase[i]);
				++i;
				++krev->pos;
			} else
				//TODO: implement this variant ?
				krev->status = KR_STATUS_FAILURE;
			for ( ; (i < count) && (krev->status == KR_STATUS_START); ++i, ++krev->pos)
				next_character (krev, p->payload[i] ^ start_phrase[i], 1);
			if (krev->status == KR_STATUS_START)
				krev->status = KR_STATUS_IN_PROGRESS;
			else if ((krev->status == KR_STATUS_FAILURE) &&
			         (regexec(re, (const char *) p->payload, 0, NULL, 0) == 0))
				krev->status = KR_STATUS_PLAINTEXT;
		}
	} else if (krev->status == KR_STATUS_IN_PROGRESS) {
		size_t i = 0;

		if ((! p->car) && ((p->type == SYS_COMMENTARY) || (p->type == SYS_NOTICE)))
			krev->status = KR_STATUS_FAILURE;
		for ( ; (i < count) && (krev->status == KR_STATUS_IN_PROGRESS); ++i, ++krev->pos)
			next_character (krev, p->payload[i], 0);
	}
	if (krev->status == KR_STATUS_PLAINTEXT)
		krev->key = 0;
}

/**
 * reverse_key:
 * @krev: key reverser state.
 * @p: current packet in packet stream.
 *
 * Tries to reverse key from packet stream.
 * This function saves state in @krev.
 **/
void
reverse_key (KeyReverser *krev, const Packet *p)
{
	if ((! krev) || (! p))
		return;
	if (is_reset_decryption_packet (p))
		reset_reverser (krev);
	if (is_crypted (p))
		act_reverser (krev, p);
}
