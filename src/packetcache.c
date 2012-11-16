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
	cache_err_overflow = PACKETCACHE_ERR_OVERFLOW;


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

static PacketCache cache;


/**
 * get_file_size:
 *
 * Returns: size of cache.f data (without signature) in bytes
 * or -1 on failure.
 **/
static long
get_file_size ()
{
	long res;

	info (5, _("get_file_size\n"));
	if (fseek (cache.f, 0, SEEK_END) == -1)
		return -1;
	res = ftell (cache.f);
	return res >= sizeof (Packet) ? res - sizeof (Packet) : -1;
}

/**
 * seek_func:
 * @packet_offset: file offset in packets.
 *
 * Sets the file position indicator to @packet_offset packets
 * from the beginning of file data (skips signature).
 *
 * Returns: 0 on success, -1 on failure.
 **/
static int
seek_func (long packet_offset)
{
	size_t bytes = (packet_offset + 1) * sizeof (Packet);

	info (5, _("seek_func (packet_offset = %d)\n"), packet_offset);
	if (bytes / sizeof (Packet) != (packet_offset + 1))
		return -1;
	return fseek (cache.f, bytes, SEEK_SET) == -1 ? -1 : 0;
}

/**
 * read_func:
 * @dest[out]: pointer to start packet to save result into.
 * @packet_count[in]: count of packets to read.
 *
 * Reads @packet_count packets from the file.
 *
 * Returns: 0 on success, -1 on failure.
 **/
static int
read_func (Packet *dest, size_t packet_count)
{
	size_t bytes = packet_count * sizeof (Packet);

	info (5, _("read_func (packet_count = %u)\n"), packet_count);
	assert (packet_count > 0);
	assert (bytes / sizeof (Packet) == packet_count);
	return fread (dest, 1, bytes, cache.f) == bytes ? 0 : -1;
}

/**
 * write_func:
 * @src: pointer to start packet which will be saved to the file.
 * @packet_count: count of packets to write.
 *
 * Writes @packet_count packets to the file.
 *
 * Returns: count of packets were written.
 **/
static size_t
write_func (Packet *src, size_t packet_count)
{
	size_t bytes = packet_count * sizeof (Packet);

	info (5, _("write_func (packet_count = %u)\n"), packet_count);
	assert (packet_count > 0);
	assert (bytes / sizeof (Packet) == packet_count);
	return fwrite (src, 1, bytes, cache.f) / sizeof (Packet);
}

/**
 * reserve_space_for_holder:
 * @newindex: index of loaded or newly creating chunk.
 *
 * Grows cache.array if @newindex >= cache.capacity.
 *
 * Returns 0 on success, < 0 on failure.
 **/
static int
reserve_space_for_holder (size_t newindex)
{
	if (newindex >= cache.capacity) {
		size_t        newcap   = MAX ((size_t) cache.capacity * 2, newindex) + 1;
		size_t        newbytes = newcap * sizeof (ChunkHolder);
		ChunkHolder  *newarray;

		if ((newindex >= newcap) || ((newbytes / sizeof (ChunkHolder)) != newcap))
			return cache_err_overflow;
		newarray = realloc (cache.array, newbytes);
		if (! newarray)
			return cache_err_nomem;
		cache.array = newarray;
		memset (&cache.array[cache.capacity], 0,
		        (newcap - cache.capacity) * sizeof (ChunkHolder));
		cache.capacity = newcap;
	}
	return 0;
}

/**
 * push_to_unused:
 * @index: chunk index.
 *
 * Pushes @index-th chunk to unused list.
 **/
static void
push_to_unused (size_t index)
{
	ChunkHolder *curr = &cache.array[index];
	ChunkHolder *head = &cache.array[0];

	assert (index > 0);
	assert (index < cache.capacity);
	cache.array[head->next].prev = index;
	curr->next = head->next;
	head->next = index;
	curr->prev = 0;
	++head->unused_count;
}

/**
 * release_chunk:
 * @index: chunk index.
 *
 * Releases @index-th chunk (decrements ref_count and pushes chunk
 * to unused list if chunk becomes unused).
 **/
static void
release_chunk (size_t index)
{
	ChunkHolder *curr = &cache.array[index];

	assert (index > 0);
	assert (index < cache.capacity);
	assert (curr->ref_count > 0);
	--curr->ref_count;
	if ((curr->ref_count == 0) && curr->data && (index < cache.itwrite.index))
		push_to_unused (index);
}

/**
 * pop_from_unused:
 * @index: chunk index.
 *
 * Pops @index-th chunk from unused list.
 **/
static void
pop_from_unused (size_t index)
{
	ChunkHolder *curr = &cache.array[index];

	assert (index > 0);
	assert (index < cache.capacity);
	cache.array[curr->prev].next = curr->next;
	cache.array[curr->next].prev = curr->prev;
	curr->prev = curr->next = 0;
	--cache.array[0].unused_count;
}

/**
 * intercept_ownership:
 * @curr: chunk holder.
 * @unused: index of unused chunk.
 *
 * Intercepts ChunkHolder::data from @unused chunk to @curr.
 **/
static void
intercept_ownership (ChunkHolder *curr, size_t unused)
{
	if (! curr)
		return;
	assert (unused >= 0);
	assert (unused < cache.capacity);
	curr->data = cache.array[unused].data;
	cache.array[unused].data = NULL;
	if (unused)
		pop_from_unused (unused);
}

/**
 * lock_chunk:
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
 **/
static int
lock_chunk (size_t index)
{
	ChunkHolder *curr   = &cache.array[index];
	size_t       unused =  cache.array[0].prev;

	assert (index > 0);
	assert (index < cache.capacity);
	if (curr->ref_count + 1 == 0)
		return cache_err_overflow;
	if (curr->data) {
		++curr->ref_count;
		return 0;
	}
	if (unused && (cache.array[0].unused_count > min_chunks_cache_size))
		intercept_ownership (curr, unused);
	if (! curr->data) {
		curr->data = malloc (packet_chunk_size * sizeof (Packet));
		while ((! curr->data) && unused) {
			intercept_ownership (curr, unused);
			unused = cache.array[0].prev;
		}
		if (! curr->data)
			return cache_err_nomem;
	}
	if (index < cache.itwrite.index) {
		if ((seek_func ((long)(index - 1) * packet_chunk_size) != 0) ||
		    (read_func (curr->data, packet_chunk_size) != 0)) {
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
 * @newindex: index of new chunk.
 * @oldindex: index of old chunk.
 *
 * Reserves space for and locks @newindex chunk,
 * then releases @oldindex chunk.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
static int
change_chunk (size_t newindex, size_t oldindex)
{
	int res;

	assert ((oldindex < cache.capacity) || (oldindex == 0));
	res = reserve_space_for_holder (newindex);
	if (res)
		return res;
	if (newindex)
		res = lock_chunk (newindex);
	if (res)
		return res;
	if (oldindex)
		release_chunk (oldindex);
	return 0;
}

/**
 * init_packet_iterator:
 * @it: packet iterator.
 *
 * Initialises @it.
 **/
void
init_packet_iterator (PacketIterator *it)
{
	if (! it)
		return;
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
	if (! it)
		return;
	change_chunk (0, it->index);
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
	other.index = 1;
	other.pos = 0;
	res = change_chunk (other.index, it->index);
	if (res == 0)
		*it = other;
	return res;
}

/**
 * load_final_packet:
 *
 * Sets cache.itwrite and cache.itpush to point to past-the-end packet
 * and locks appropriate chunk.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
static int
load_final_packet ()
{
	int            res;
	long           endpos = get_file_size ();
	size_t         count, newindex;
	PacketIterator oiw, oip;

	if (endpos == -1)
		return cache_err_file;
	count = endpos / sizeof (Packet);
	newindex = 1 + count / packet_chunk_size;

	res = change_chunk (newindex, cache.itwrite.index);
	if (res)
		return res;
	res = change_chunk (newindex, cache.itpush.index);
	if (res) {
		change_chunk (cache.itwrite.index, newindex);
		return res;
	}
	oiw.index = oip.index = newindex;
	oiw.pos = oip.pos = count % packet_chunk_size;

	if (oiw.pos) {
		ChunkHolder *hw = &cache.array[oiw.index];

		if ((seek_func ((long)(oiw.index - 1) * packet_chunk_size) != 0) ||
		    (read_func (hw->data, oiw.pos) != 0)) {
			free (hw->data);
			hw->data = NULL;
			/* change chunks after freeing hw->data only
			 * (to avoid push_to_unused)
			 */
			change_chunk (cache.itpush.index, oip.index);
			change_chunk (cache.itwrite.index, oiw.index);
			return cache_err_file;
		}
	}
	cache.itwrite = oiw;
	cache.itpush = oip;
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
	other.index = it->index + 1;
	other.pos = 0;
	res = change_chunk (other.index, it->index);
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
	if (it->pos + 1 < packet_chunk_size) {
		++it->pos;
		return 0;
	}
	return to_next_chunk (it);
}

/**
 * push_packet:
 * @packet: new packet.
 * @saving_time: @packet timestamp.
 *
 * Pushes @packet to the end of the cache (cache.itpush),
 * then corrects cache.itpush.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
int
push_packet (const Packet *packet, time_t saving_time)
{
	Packet *dest;

	if (! cache.itpush.index) {
		int res = to_start_packet (&cache.itpush);

		if (res)
			return res;
	}
	dest = &cache.array[cache.itpush.index].data[cache.itpush.pos];
	*dest = *packet;
	if (packet->len < sizeof (packet->payload)) {
		int start = MAX (packet->len, 0);

		memset (&dest->payload[start], 0, sizeof (packet->payload) - start);
	}
	dest->at = saving_time;
	return to_next_packet (&cache.itpush);
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
	if ((! it->index) && (to_start_packet (it) != 0))
		return NULL;
	if ((it->index < cache.itpush.index) ||
	   ((it->index == cache.itpush.index) && (it->pos < cache.itpush.pos)))
		return &cache.array[it->index].data[it->pos];
	return NULL;
}

/**
 * save_packets:
 *
 * Save unsaved packets to underlying file. Corrects cache.itwrite.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
int
save_packets ()
{
	PacketIterator *ip = &cache.itpush, *iw = &cache.itwrite;
	int psaved, err;

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
				change_chunk (ip->index, oip.index);
				return res;
			}
		}
		*ip = oip;
		*iw = oiw;
	}

	if ((iw->index > ip->index) || ((iw->index == ip->index) && (iw->pos >= ip->pos)))
		return 0;
	if (seek_func ((long)(iw->index - 1) * packet_chunk_size + iw->pos) != 0)
		return cache_err_file;
	for (psaved = err = 0; (! err) && (iw->index < ip->index); ) {
		Packet *sp     = &cache.array[iw->index].data[iw->pos];
		size_t  wcount = packet_chunk_size - iw->pos;
		size_t  count  = write_func (sp, wcount);

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
		Packet *sp     = &cache.array[iw->index].data[iw->pos];
		size_t  wcount = ip->pos - iw->pos;
		size_t  count  = write_func (sp, wcount);

		iw->pos += count;
		err = count != wcount;
		psaved = psaved || count;
	}
	return psaved ? 0 : cache_err_file;
}

/**
 * init_packet_cache:
 *
 * Initialises cache.
 **/
void
init_packet_cache ()
{
	memset (&cache, 0, sizeof (PacketCache));
}

/**
 * free_packet_cache:
 *
 * Frees cache. Frees allocated memory and closes underlying file.
 **/
static void
free_packet_cache ()
{
	if (cache.array) {
		size_t i;

		for (i = 0; i != cache.capacity; ++i)
			free (cache.array[i].data);
		free (cache.array);
		cache.array = NULL;
	}
	if (cache.f) {
		fclose (cache.f);
		cache.f = NULL;
	}
	init_packet_cache ();
}

/**
 * destroy_packet_cache:
 *
 * Destroys cache. Delegates execution to free_packet_cache.
 **/
void
destroy_packet_cache ()
{
	free_packet_cache ();
}

/**
 * check_signature:
 *
 * Checks underlying file version signature.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
static int
check_signature ()
{
	Packet sign;
	size_t count;
	const char *sign_text = (const char*) (&sign);

	assert (cache.f);
	if (fseek (cache.f, 0, SEEK_SET) == -1)
		return cache_err_file;
	count = fread (&sign, 1, sizeof (sign), cache.f);
	if (count != sizeof (sign))
		return cache_err_version;
	return strncmp (sign_text, version_signature, sizeof (sign)) != 0 ?
	       cache_err_version : 0;
}

/**
 * write_signature:
 *
 * Writes underlying file version signature.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
static int
write_signature ()
{
	Packet sign;
	size_t count;
	char *sign_text = (char*) (&sign);

	assert (cache.f);
	memset (&sign, 0, sizeof (sign));
	strncpy (sign_text, version_signature, sizeof (sign));
	if (fseek (cache.f, 0, SEEK_SET) == -1)
		return cache_err_file;
	count = fwrite (&sign, 1, sizeof (sign), cache.f);
	if (count != sizeof (sign))
		return cache_err_file;
	return 0;
}

/**
 * set_new_underlying_file:
 * @name: underlying file name.
 * @replay_mode: true for replay mode.
 *
 * Frees cache and sets new underlying file.
 *
 * Returns: 0 on success, < 0 on failure.
 **/
int
set_new_underlying_file (const char *name, char replay_mode)
{
	free_packet_cache ();
	cache.f = fopen (name, replay_mode ? "r" : "w+");
	if (! cache.f)
		return cache_err_file;
	if (replay_mode) {
		int res = check_signature ();

		if (res)
			return res;
	} else {
		int res = write_signature ();

		if (res)
			return res;
	}
	return load_final_packet ();
}
