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
#include <stdlib.h>
#include <string.h>

#include "crypt.h"
#include "keyrev.h"


typedef struct {
	unsigned int  key;
	unsigned int  salt;
	unsigned int  mask;
	int           status;
	size_t        pos;
} KeyReverser;


static void
reset_reverser (KeyReverser *krev)
{
	assert (krev);
	krev->key = 0x80000000;
	reset_decryption (&krev->salt);
	krev->mask = 0;
	krev->status = 0;
	krev->pos = 0;
}

static int
replay_reverser (KeyReverser *krev)
{
	size_t i;

	assert (krev);
	reset_decryption (&krev->salt);
	for (i = 0; i <= krev->pos; ++i)
		krev->salt = (krev->salt >> 1) ^ (krev->salt & 0x01 ? krev->key : 0);
	return 0;
}

static void
first_character (KeyReverser *krev, unsigned char diff)
{
	assert (krev);
	assert (krev->salt & 0x01);
	krev->salt >>= 1;
	krev->key = diff ^ krev->salt;
	krev->mask = 0xff;
	krev->salt = krev->salt ^ krev->key;
}

#if ((! defined MAX_CAR_NUMBER) || (MAX_CAR_NUMBER >= 128))
	#error "If MAX_CAR_NUMBER >= 128 then msb can be equal to 1 in some car packets."
#endif
/* Reversing method bases on the fact that every decrypted symbol
 * has zero most significant bit (except SYS_TIMESTAMP payload and
 * some characters in SYS_COMMENTARY payload).
 */
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
		krev->status = -1;
		return;
	}
	if ((diff & 0x80) != (krev->salt & 0x80)) {
		krev->key = krev->key ^ (krev->mask + 1);
		if (replay_reverser (krev) != 0) {
			krev->status = -1;
			return;
		}
	}
	if ((diff & 0x80) != (krev->salt & 0x80)) {
		krev->status = -1;
		return;
	}
	krev->mask = (krev->mask << 1) | 0x01;
	if ((krev->mask + 1) == 0)
		krev->status = 2;
}

static int
act_reverser (KeyReverser *krev, const Packet *p)
{
	static const char *start_phrase = "Please Wait ...";
	static size_t      splen = 0;
	size_t             count = 0;

	assert (krev);
	assert (p);
	if (! splen)
		splen = strlen (start_phrase);
	if ((krev->status < 0) || (krev->status > 1) || (p->len <= 0))
		return krev->status;
	count = MIN (p->len, MAX_PACKET_LEN);
	if (krev->status == 0) {
		if (p->car || (p->type != SYS_NOTICE) || (p->len != splen))
			krev->status = -1;
		else {
			size_t i = 0;

			if (krev->salt & 0x01) {
				first_character (krev, p->payload[i] ^ start_phrase[i]);
				++i;
				++krev->pos;
			} else
				krev->status = -1; //TODO: implement this variant ?
			for ( ; (i < count) && (krev->status == 0); ++i, ++krev->pos)
				next_character (krev, p->payload[i] ^ start_phrase[i], 1);
			if (krev->status == 0)
				krev->status = 1;
		}
	} else if (krev->status == 1) {
		size_t i;

		if ((! p->car) && ((p->type == SYS_COMMENTARY) || (p->type == SYS_NOTICE)))
			krev->status = -1;
		for (i = 0; (i < count) && (krev->status == 1); ++i, ++krev->pos)
			next_character (krev, p->payload[i], 0);
	}

	return krev->status;
}

/**
 * reverse_key:
 * @key: pointer to save newly reversed key.
 * @p: next packet in packet stream.
 *
 * Tries to reverse key from packet stream.
 * This function saves state between calls.
 *
 * Returns: 0 on success, < 0 on failure,
 * 1 if key was not reversed (not enough data).
 **/
int
reverse_key (unsigned int *key, const Packet *p)
{
	static KeyReverser *krev = NULL;

	if ((! key) || (! p))
		return -1;
	if (! krev) {
		krev = malloc (sizeof (*krev));
		if (! krev)
			return -1;
		reset_reverser (krev);
	}

	if (is_reset_decryption_packet (p))
		reset_reverser (krev);
	if (is_crypted (p) && (act_reverser (krev, p) == 2)) {
		*key = krev->key;
		return 0;
	}

	return 1;
}

/*
 * @from: start iterator to collect data from.
 * @to: final iterator to collect data from.
 *
 * Iterator @from can be changed during this function execution.
 *
int
reverse_key (unsigned int *key, PacketIterator *from, const PacketIterator *to)
{
	PacketIterator it;
	KeyReverser krev;
	int res = 1;

	if ((! key) || (! from) || (! to) || (from->cnum != to->cnum))
		return -1;
	init_packet_iterator (from->cnum, &it);
	if (copy_packet_iterator (&it, from) != 0)
		res = -1;
	reset_reverser (&krev);

	while ((res >= 0) && (it != *to)) {
		const Packet *packet = get_packet (&it);

		res = -1;
		if (! packet)
			break;
		if (is_reset_decryption_packet (packet)) {
			if (copy_packet_iterator (from, &it) != 0)
				break;
			reset_reverser (&krev);
		}
		if (is_crypted (packet) && (act_reverser (&krev, packet) == 2)) {
			res = 0;
			break;
		}
		if (to_next_packet (&it) != 0)
			break;
		res = 1;
	}

	if (res == 0)
		*key = krev.key;
	destroy_packet_iterator (&it);
	return res;
}
*/
