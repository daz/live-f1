/* live-f1
 *
 * packetcache.c - caching packets at memory and saving them to file
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

//TODO: avoid reinventing the wheel (use sqlite ?)

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "live-f1.h"
#include "macros.h"

/* file version signature */
static const char *version_signature = "live-f1 version 2012.0 timing";

/* size of every ChunkHolder::data chunk in Packets */
static const size_t packet_chunk_size  = 1024;
/* interception of ChunkHolder::data ownership is maked when count of
 * unused chunks is more then this value */
static const size_t min_chunks_cache_size = 4;

/* Packet cache error codes */
static const int
	/* file operation fails */
	cache_err_file     = PACKETCACHE_ERR_FILE,
	/* unsupported underlying file version */
	cache_err_version  = PACKETCACHE_ERR_VERSION,
	/* not enough memory */
	cache_err_nomem    = PACKETCACHE_ERR_NOMEM,
	/* integer overflow */
	cache_err_overflow = PACKETCACHE_ERR_OVERFLOW,
	/* invalid cache number */
	cache_err_cnum     = PACKETCACHE_ERR_CNUM;


/* chunk smart pointer */
typedef struct {
	union {
		/* count of PacketIterators referenced to this chunk
		 * (index of this ChunkHolder in PacketCache != 0)
		 * Chunk is loaded but not used when ref_count == 0
		 * and data != 0 and it was already written to file;
		 * data field can be intercepted by another chunk
		 * in such case.
		 */
		size_t ref_count;
		/* count of unused chunks (index == 0)
		 * (term "unused" means ref_count == 0 and data != 0 and
		 * chunk was already written to file).
		 */
		size_t unused_count;
	};
	/* index of previous and next unused chunk in chunks array.
	 * This chunk is first or last unused one if prev == 0 or next == 0
	 * respectively (0th chunk is not used to store data, it is used as
	 * unused list head only). */
	size_t prev, next;
	/* pointer to loaded packets chunk (size == packet_chunk_size) */
	Packet *data;
} ChunkHolder;

/* packet cache */
typedef struct {
	/* array of chunk smart pointers */
	ChunkHolder    *array;
	/* capacity of array */
	size_t          capacity;
	/* position to push new packet */
	PacketIterator  itpush;
	/* position of first unsaved packet */
	PacketIterator  itwrite;
	/* underlying file */
	FILE           *f;
} PacketCache;

/**
 * caches:
 *
 * Array of packet caches.
 * Every cache is identified by index in this array (cnum parameter).
 **/
static PacketCache *caches = NULL;

static int
	/* caches count */
	caches_count = 0,
	/* capacity of @caches array */
	caches_capacity = 0;


/**
 * get_file_size:
 * @cnum: cache number.
 *
 * Returns: size of cache.f data (without signature) in bytes
 * or -1 on failure.
 **/
static long
get_file_size (int cnum)
{
	long res;

	info (5, _("get_file_size (cnum = %u)\n"), cnum);
	assert ((cnum >= 0) && (cnum < caches_count));
	if (fseek (caches[cnum].f, 0, SEEK_END) == -1)
		return -1;
	res = ftell (caches[cnum].f);
	return res >= sizeof (Packet) ? res - sizeof (Packet) : -1;
}

/**
 * seek_func:
 * @cnum: cache number.
 * @packet_offset: file offset in packets.
 *
 * Sets the file position indicator to @packet_offset packets
 * from the beginning of file data (skips signature).
 *
 * Returns: 0 on success, -1 on failure.
 **/
static int
seek_func (int cnum, long packet_offset)
{
	size_t bytes = (packet_offset + 1) * sizeof (Packet);

	info (5, _("seek_func (cnum = %u, packet_offset = %d)\n"), cnum, packet_offset);
	assert ((cnum >= 0) && (cnum < caches_count));
	if (bytes / sizeof (Packet) != (packet_offset + 1))
		return -1;
	return fseek (caches[cnum].f, bytes, SEEK_SET) == -1 ? -1 : 0;
}

/**
 * read_func:
 * @cnum[in]: cache number.
 * @dest[out]: pointer to start packet to save result into.
 * @packet_count[in]: count of packets to read.
 *
 * Reads @packet_count packets from the file.
 *
 * Returns: 0 on success, -1 on failure.
 **/
static int
read_func (int cnum, Packet *dest, size_t packet_count)
{
	size_t bytes = packet_count * sizeof (Packet);

	info (5, _("read_func (cnum = %u, packet_count = %u)\n"), cnum, packet_count);
	assert ((cnum >= 0) && (cnum < caches_count));
	assert (packet_count > 0);
	assert (bytes / sizeof (Packet) == packet_count);
	return fread (dest, 1, bytes, caches[cnum].f) == bytes ? 0 : -1;
}

/**
 * write_func:
 * @cnum: cache number.
 * @src: pointer to start packet which will be saved to the file.
 * @packet_count: count of packets to write.
 *
 * Writes @packet_count packets to the file.
 *
 * Returns: count of packets were written.
 **/
static size_t
write_func (int cnum, Packet *src, size_t packet_count)
{
	size_t bytes = packet_count * sizeof (Packet);

	info (5, _("write_func (cnum = %u, packet_count = %u)\n"), cnum, packet_count);
	assert ((cnum >= 0) && (cnum < caches_count));
	assert (packet_count > 0);
	assert (bytes / sizeof (Packet) == packet_count);
	return fwrite (src, 1, bytes, caches[cnum].f) / sizeof (Packet);
}

/**
 * reserve_space_for_holder:
 * @cnum: cache number.
 * @newindex: index of loaded or newly creating chunk.
 *
 * Grows cache.array if @newindex >= cache.capacity.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
static int
reserve_space_for_holder (int cnum, size_t newindex)
{
	assert ((cnum >= 0) && (cnum < caches_count));
	if (newindex >= caches[cnum].capacity) {
		size_t       newcap   = MAX ((size_t) caches[cnum].capacity * 2, newindex) + 1;
		size_t       newbytes = newcap * sizeof (ChunkHolder);
		ChunkHolder *newarray;

		if ((newindex >= newcap) || ((newbytes / sizeof (ChunkHolder)) != newcap))
			return cache_err_overflow;
		newarray = realloc (caches[cnum].array, newbytes);
		if (! newarray)
			return cache_err_nomem;
		caches[cnum].array = newarray;
		memset (&caches[cnum].array[caches[cnum].capacity], 0,
		        (newcap - caches[cnum].capacity) * sizeof (ChunkHolder));
		caches[cnum].capacity = newcap;
	}
	return 0;
}

/**
 * push_to_unused:
 * @cnum: cache number.
 * @index: chunk index.
 *
 * Pushes @index-th chunk to unused list.
 **/
static void
push_to_unused (int cnum, size_t index)
{
	ChunkHolder *curr, *head;

	assert ((cnum >= 0) && (cnum < caches_count));
	curr = &caches[cnum].array[index];
	head = &caches[cnum].array[0];

	assert (index > 0);
	assert (index < caches[cnum].capacity);
	caches[cnum].array[head->next].prev = index;
	curr->next = head->next;
	head->next = index;
	curr->prev = 0;
	++head->unused_count;
}

/**
 * release_chunk:
 * @cnum: cache number.
 * @index: chunk index.
 *
 * Releases @index-th chunk (decrements ref_count and pushes chunk
 * to unused list if chunk becomes unused).
 **/
static void
release_chunk (int cnum, size_t index)
{
	ChunkHolder *curr;

	assert ((cnum >= 0) && (cnum < caches_count));
	curr = &caches[cnum].array[index];

	assert (index > 0);
	assert (index < caches[cnum].capacity);
	assert (curr->ref_count > 0);
	--curr->ref_count;
	if ((curr->ref_count == 0) && curr->data && (index < caches[cnum].itwrite.index))
		push_to_unused (cnum, index);
}

/**
 * pop_from_unused:
 * @cnum: cache number.
 * @index: chunk index.
 *
 * Pops @index-th chunk from unused list.
 **/
static void
pop_from_unused (int cnum, size_t index)
{
	ChunkHolder *curr;

	assert ((cnum >= 0) && (cnum < caches_count));
	curr = &caches[cnum].array[index];

	assert (index > 0);
	assert (index < caches[cnum].capacity);
	caches[cnum].array[curr->prev].next = curr->next;
	caches[cnum].array[curr->next].prev = curr->prev;
	curr->prev = curr->next = 0;
	--caches[cnum].array[0].unused_count;
}

/**
 * intercept_ownership:
 * @cnum: cache number.
 * @curr: chunk holder.
 * @unused: index of unused chunk.
 *
 * Intercepts ChunkHolder::data from @unused chunk to @curr.
 **/
static void
intercept_ownership (int cnum, ChunkHolder *curr, size_t unused)
{
	if (! curr)
		return;
	assert ((cnum >= 0) && (cnum < caches_count));
	assert (unused >= 0);
	assert (unused < caches[cnum].capacity);
	curr->data = caches[cnum].array[unused].data;
	caches[cnum].array[unused].data = NULL;
	if (unused)
		pop_from_unused (cnum, unused);
}

/**
 * lock_chunk:
 * @cnum: cache number.
 * @index: chunk index.
 *
 * Locks @index-th chunk (reads data from the file if not loaded yet or
 * allocates new chunk if file has no this one, then increments ref_count).
 *
 * If ChunkHolder::data is not NULL, then we assert that this field was already
 * stores pointer to correct loaded packet chunk. If data is NULL, we can
 * intercept ownership of data field from another chunk (instead of allocating
 * memory) and then load packets from underlying file.
 *
 * If loading fails, we cannot leave or return ownership of data field
 * because of possibility of partial loading and invalidating of it,
 * we must free this field (but we can pass ownership to 0-th chunk -
 * this isn't implemented now).
 *
 * Returns: 0 on success, < 0 on failure.
 **/
static int
lock_chunk (int cnum, size_t index)
{
	ChunkHolder *curr;
	size_t       unused;

	assert ((cnum >= 0) && (cnum < caches_count));
	curr   = &caches[cnum].array[index];
	unused =  caches[cnum].array[0].prev;

	assert (index > 0);
	assert (index < caches[cnum].capacity);
	if (curr->ref_count + 1 == 0)
		return cache_err_overflow;
	if (curr->data) {
		++curr->ref_count;
		return 0;
	}
	if (unused && (caches[cnum].array[0].unused_count > min_chunks_cache_size))
		intercept_ownership (cnum, curr, unused);
	if (! curr->data) {
		curr->data = malloc (packet_chunk_size * sizeof (Packet));
		while ((! curr->data) && unused) {
			intercept_ownership (cnum, curr, unused);
			unused = caches[cnum].array[0].prev;
		}
		if (! curr->data)
			return cache_err_nomem;
	}
	if (index < caches[cnum].itwrite.index) {
		if ((seek_func (cnum, (long)(index - 1) * packet_chunk_size) != 0) ||
		    (read_func (cnum, curr->data, packet_chunk_size) != 0)) {
			free (curr->data);
			curr->data = NULL;
			return cache_err_file;
		}
	}
	++curr->ref_count;
	return 0;
}

/**
 * change_chunk:
 * @cnum: cache number.
 * @newindex: index of new chunk.
 * @oldindex: index of old chunk.
 *
 * Reserves space for and locks @newindex chunk,
 * then releases @oldindex chunk.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
static int
change_chunk (int cnum, size_t newindex, size_t oldindex)
{
	int res;

	assert ((cnum >= 0) && (cnum < caches_count));
	assert ((oldindex < caches[cnum].capacity) || (oldindex == 0));
	res = reserve_space_for_holder (cnum, newindex);
	if (res)
		return res;
	if (newindex)
		res = lock_chunk (cnum, newindex);
	if (res)
		return res;
	if (oldindex)
		release_chunk (cnum, oldindex);
	return 0;
}

/**
 * init_packet_iterator:
 * @cnum: cache number.
 * @it: packet iterator.
 *
 * Initialises @it.
 **/
void
init_packet_iterator (int cnum, PacketIterator *it)
{
	if ((! it) || (cnum < 0) || (cnum >= caches_count))
		return;
	it->cnum = cnum;
	it->index = it->pos = 0;
}

/**
 * destroy_packet_iterator:
 * @it: packet iterator.
 *
 * Destroys @it. Releases @it->index chunk.
 **/
void
destroy_packet_iterator (PacketIterator *it)
{
	if ((! it) || (it->cnum < 0) || (it->cnum >= caches_count))
		return;
	change_chunk (it->cnum, 0, it->index);
	it->index = it->pos = 0;
}

/**
 * to_start_packet:
 * @it: packet_iterator.
 *
 * Sets @it to point to start packet of the cache.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
int
to_start_packet (PacketIterator *it)
{
	int            res;
	PacketIterator other;

	if (! it)
		return 0;
	if ((it->cnum < 0) || (it->cnum >= caches_count))
		return cache_err_cnum;
	other.cnum = it->cnum;
	other.index = 1;
	other.pos = 0;
	res = change_chunk (it->cnum, other.index, it->index);
	if (res == 0)
		*it = other;
	return res;
}

/**
 * load_final_packet:
 * @cnum: cache number.
 *
 * Sets cache.itwrite and cache.itpush to point to past-the-end packet
 * and locks appropriate chunk.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
static int
load_final_packet (int cnum)
{
	int            res;
	long           endpos;
	size_t         count, newindex;
	PacketIterator oiw, oip;

	assert ((cnum >= 0) && (cnum < caches_count));
	endpos = get_file_size (cnum);
	if (endpos == -1)
		return cache_err_file;
	count = endpos / sizeof (Packet);
	newindex = 1 + count / packet_chunk_size;

	res = change_chunk (cnum, newindex, caches[cnum].itwrite.index);
	if (res)
		return res;
	res = change_chunk (cnum, newindex, caches[cnum].itpush.index);
	if (res) {
		change_chunk (cnum, caches[cnum].itwrite.index, newindex);
		return res;
	}
	oiw.cnum = oip.cnum = cnum;
	oiw.index = oip.index = newindex;
	oiw.pos = oip.pos = count % packet_chunk_size;

	if (oiw.pos) {
		ChunkHolder *hw = &caches[cnum].array[oiw.index];

		if ((seek_func (cnum, (long)(oiw.index - 1) * packet_chunk_size) != 0) ||
		    (read_func (cnum, hw->data, oiw.pos) != 0)) {
			free (hw->data);
			hw->data = NULL;
			/* change chunks after freeing hw->data only
			 * (to avoid push_to_unused)
			 */
			change_chunk (cnum, caches[cnum].itpush.index, oip.index);
			change_chunk (cnum, caches[cnum].itwrite.index, oiw.index);
			return cache_err_file;
		}
	}
	caches[cnum].itwrite = oiw;
	caches[cnum].itpush = oip;
	return 0;
}

/**
 * to_next_chunk:
 * @it: packet iterator.
 *
 * Sets @it to point to start packet in next chunk.
 * Releases old chunk, locks new chunk (calls change_chunk).
 *
 * Returns: 0 on success, < 0 on failure.
 **/
static int
to_next_chunk (PacketIterator *it)
{
	int            res;
	PacketIterator other;

	if (! it)
		return 0;
	if ((it->cnum < 0) || (it->cnum >= caches_count))
		return cache_err_cnum;
	other.cnum = it->cnum;
	other.index = it->index + 1;
	other.pos = 0;
	res = change_chunk (it->cnum, other.index, it->index);
	if (res == 0)
		*it = other;
	return res;
}

/**
 * to_next_packet:
 * @it: packet iterator.
 *
 * Sets @it to point to next packet.
 * Calls to_next_chunk when @it crosses chunk boundary.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
int
to_next_packet (PacketIterator *it)
{
	if (! it)
		return 0;
	if ((it->cnum < 0) || (it->cnum >= caches_count))
		return cache_err_cnum;
	if (it->pos + 1 < packet_chunk_size) {
		++it->pos;
		return 0;
	}
	return to_next_chunk (it);
}

/**
 * push_packet:
 * @cnum: cache number.
 * @packet: new packet.
 * @saving_time: @packet timestamp.
 *
 * Pushes @packet to the end of the cache (cache.itpush),
 * then corrects cache.itpush.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
int
push_packet (int cnum, const Packet *packet)
{
	Packet *dest;

	if ((cnum < 0) || (cnum >= caches_count))
		return cache_err_cnum;
	assert (caches[cnum].itpush.cnum == cnum);
	if (! caches[cnum].itpush.index) {
		int res = to_start_packet (&caches[cnum].itpush);

		if (res)
			return res;
	}
	dest = &caches[cnum].array[caches[cnum].itpush.index].data[caches[cnum].itpush.pos];
	*dest = *packet;

	//TODO: remove unrelated functionality ?
	if (packet->len < sizeof (packet->payload)) {
		int start = MAX (packet->len, 0);

		memset (&dest->payload[start], 0, sizeof (packet->payload) - start);
	}

	return to_next_packet (&caches[cnum].itpush);
}

/**
 * get_packet:
 * @it: packet iterator.
 *
 * Gets packet under @it.
 *
 * Returns: pointer to the packet in the cache or NULL on failure.
 * Returned pointer may become invalid after @it changing.
 **/
const Packet *
get_packet (PacketIterator *it)
{
	if (! it)
		return NULL;
	if ((it->cnum < 0) || (it->cnum >= caches_count))
		return NULL;
	if ((! it->index) && (to_start_packet (it) != 0))
		return NULL;
	if ((it->index < caches[it->cnum].itpush.index) ||
	   ((it->index == caches[it->cnum].itpush.index) &&
	    (it->pos < caches[it->cnum].itpush.pos)))
		return &caches[it->cnum].array[it->index].data[it->pos];
	return NULL;
}

/**
 * save_packets:
 * @cnum: cache number.
 *
 * Saves unsaved packets to underlying file. Moves cache.itwrite.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
int
save_packets (int cnum)
{
	PacketIterator *ip, *iw;
	int psaved, err;

	if ((cnum < 0) || (cnum >= caches_count))
		return cache_err_cnum;
	ip = &caches[cnum].itpush;
	assert (ip->cnum == cnum);
	iw = &caches[cnum].itwrite;
	assert (iw->cnum == cnum);
	{
		PacketIterator  oip = *ip, oiw = *iw;

		if (! oip.index) {
			int res = to_start_packet (&oip);

			if (res)
				return res;
		}
		if (! oiw.index) {
			int res = to_start_packet (&oiw);

			if (res) {
				change_chunk (cnum, ip->index, oip.index);
				return res;
			}
		}
		*ip = oip;
		*iw = oiw;
	}

	if ((iw->index > ip->index) || ((iw->index == ip->index) && (iw->pos >= ip->pos)))
		return 0;
	if (seek_func (cnum, (long)(iw->index - 1) * packet_chunk_size + iw->pos) != 0)
		return cache_err_file;
	for (psaved = err = 0; (! err) && (iw->index < ip->index); ) {
		Packet *sp     = &caches[cnum].array[iw->index].data[iw->pos];
		size_t  wcount = packet_chunk_size - iw->pos;
		size_t  count  = write_func (cnum, sp, wcount);

		iw->pos += count;
		if (count == wcount) {
			if (to_next_chunk (iw) != 0) {
				--iw->pos;
				--count;
			}
		}
		err = count != wcount;
		psaved = psaved || count;
	}
	if ((! err) && (iw->index == ip->index) && (iw->pos < ip->pos)) {
		Packet *sp     = &caches[cnum].array[iw->index].data[iw->pos];
		size_t  wcount = ip->pos - iw->pos;
		size_t  count  = write_func (cnum, sp, wcount);

		iw->pos += count;
		err = count != wcount;
		psaved = psaved || count;
	}
	return psaved ? 0 : cache_err_file;
}

/**
 * get_head_packet:
 * @cnum: cache number.
 *
 * Gets packet under cache.itwrite.
 *
 * Returns: pointer to the packet in the cache or NULL on failure.
 * Returned pointer may become invalid after cache.itwrite changing.
 **/
const Packet *
get_head_packet (int cnum)
{
	if ((cnum < 0) || (cnum >= caches_count))
		return NULL;
	return get_packet (&caches[cnum].itwrite);
}

/**
 * drop_head_packet:
 * @cnum: cache number.
 *
 * Drops packet under cache.itwrite without saving it. Moves cache.itwrite.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
int
drop_head_packet (int cnum)
{
	if ((cnum < 0) || (cnum >= caches_count))
		return cache_err_cnum;
	return to_next_packet (&caches[cnum].itwrite);
}

/**
 * init_packet_cache:
 *
 * Initialises new cache.
 *
 * Returns: new cache number (>= 0) on success, < 0 on failure.
 **/
int
init_packet_cache ()
{
	int res = caches_count;

	assert ((caches_capacity >= 0) && (caches_count >= 0));
	if (res >= caches_capacity) {
		size_t       newcap   = MAX (caches_capacity * 2, res) + 1;
		size_t       newbytes = newcap * sizeof (PacketCache);
		PacketCache *newcaches;

		if ((res < 0) || (res >= newcap) || ((newbytes / sizeof (PacketCache)) != newcap))
			return cache_err_overflow;
		newcaches = realloc (caches, newbytes);
		if (! newcaches)
			return cache_err_nomem;
		caches = newcaches;
		caches_capacity = newcap;
	}
	caches_count = res + 1;
	memset (&caches[res], 0, sizeof (PacketCache));
	return res;
}

/**
 * free_packet_cache:
 * @cnum: cache number.
 *
 * Frees cache. Frees allocated memory and closes underlying file.
 **/
static void
free_packet_cache (int cnum)
{
	assert ((cnum >= 0) && (cnum < caches_count));
	if (caches[cnum].array) {
		size_t i;

		for (i = 0; i != caches[cnum].capacity; ++i)
			free (caches[cnum].array[i].data);
		free (caches[cnum].array);
		caches[cnum].array = NULL;
	}
	if (caches[cnum].f) {
		fclose (caches[cnum].f);
		caches[cnum].f = NULL;
	}
	memset (&caches[cnum], 0, sizeof (PacketCache));
}

/**
 * destroy_packet_cache:
 * @cnum: cache number.
 *
 * Destroys cache. Delegates execution to free_packet_cache.
 **/
void
destroy_packet_cache (int cnum)
{
	if ((cnum >= 0) && (cnum < caches_count))
		free_packet_cache (cnum);
}

/**
 * check_signature:
 * @cnum: cache number.
 *
 * Checks underlying file version signature.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
static int
check_signature (int cnum)
{
	Packet sign;
	size_t count;
	const char *sign_text = (const char*) (&sign);

	assert ((cnum >= 0) && (cnum < caches_count));
	assert (caches[cnum].f);
	if (fseek (caches[cnum].f, 0, SEEK_SET) == -1)
		return cache_err_file;
	count = fread (&sign, 1, sizeof (sign), caches[cnum].f);
	if (count != sizeof (sign))
		return cache_err_version;
	return strncmp (sign_text, version_signature, sizeof (sign)) != 0 ?
	       cache_err_version : 0;
}

/**
 * write_signature:
 * @cnum: cache number.
 *
 * Writes underlying file version signature.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
static int
write_signature (int cnum)
{
	Packet sign;
	size_t count;
	char *sign_text = (char*) (&sign);

	assert ((cnum >= 0) && (cnum < caches_count));
	assert (caches[cnum].f);
	memset (&sign, 0, sizeof (sign));
	strncpy (sign_text, version_signature, sizeof (sign));
	if (fseek (caches[cnum].f, 0, SEEK_SET) == -1)
		return cache_err_file;
	count = fwrite (&sign, 1, sizeof (sign), caches[cnum].f);
	if (count != sizeof (sign))
		return cache_err_file;
	return 0;
}

/**
 * set_new_underlying_file:
 * @cnum: cache number.
 * @name: underlying file name.
 * @replay_mode: true for replay mode.
 * @fake: true for fake underlying file
 * (all packets will be dropped without saving).
 *
 * Frees cache and sets new underlying file.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
int
set_new_underlying_file (int cnum, const char *name, char replay_mode, char fake)
{
	if ((cnum < 0) || (cnum >= caches_count))
		return cache_err_cnum;
	free_packet_cache (cnum);
	caches[cnum].f = fopen (name, replay_mode ? "r" : "w+");
	if (! caches[cnum].f)
		return cache_err_file;
	caches[cnum].itwrite.cnum = caches[cnum].itpush.cnum = cnum;
	caches[cnum].itwrite.index = caches[cnum].itpush.index = 0;
	caches[cnum].itwrite.pos = caches[cnum].itpush.pos = 0;
	if (fake)
		return 0;
	if (replay_mode) {
		int res = check_signature (cnum);

		if (res)
			return res;
	} else {
		int res = write_signature (cnum);

		if (res)
			return res;
	}
	return load_final_packet (cnum);
}
