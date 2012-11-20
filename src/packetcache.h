/* live-f1
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

#ifndef LIVE_F1_PACKETCACHE_H
#define LIVE_F1_PACKETCACHE_H

#include <stddef.h>

#include "macros.h"

#include "packetdef.h"


/* Packet cache error codes */
/* file operation fails */
#define PACKETCACHE_ERR_FILE     -1
/* unsupported underlying file version */
#define PACKETCACHE_ERR_VERSION  -2
/* not enough memory */
#define PACKETCACHE_ERR_NOMEM    -3
/* integer overflow */
#define PACKETCACHE_ERR_OVERFLOW -4
/* invalid cache number */
#define PACKETCACHE_ERR_CNUM     -5


/**
 * PacketIterator:
 * @cnum: packet cache number.
 * @index: chunk index in array of chunks.
 * @pos: packet number in @index-th chunk.
 *
 * Points to packet in cache.
 **/
typedef struct {
	int    cnum;
	size_t index;
	size_t pos;
} PacketIterator;


SJR_BEGIN_EXTERN

void init_packet_iterator    (int cnum, PacketIterator *it);
void destroy_packet_iterator (PacketIterator *it);

int to_start_packet (PacketIterator *it);
int to_next_packet  (PacketIterator *it);

int            push_packet      (int cnum, const Packet *packet);
const Packet * get_packet       (PacketIterator *it);
int            save_packets     (int cnum);
const Packet * get_head_packet  (int cnum);
int            drop_head_packet (int cnum);

int  init_packet_cache    ();
void destroy_packet_cache (int cnum);

int set_new_underlying_file (int cnum, const char *name, char replay_mode, char fake);

SJR_END_EXTERN

#endif /* LIVE_F1_PACKETCACHE_H */
