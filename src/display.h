/* live-f1
 *
 * Copyright © 2005 Scott James Remnant <scott@netsplit.com>.
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

#ifndef LIVE_F1_DISPLAY_H
#define LIVE_F1_DISPLAY_H

#include <time.h>

#include "live-f1.h"


SJR_BEGIN_EXTERN

/* Curses display running */
int cursed;


void close_display (void);
int  handle_keys   (StateModel *m);

void clear_board   (StateModel *m);
void update_cell   (StateModel *m, int car, int type);
void update_car    (StateModel *m, int car);
void clear_car     (StateModel *m, int car);

void update_status (StateModel *m);
void update_time   (StateModel *m);

void info_message (size_t index, const char *message);
void add_commentary_chunk (const char *chunk, char last_chunk);
void add_timestamp (time_t ts);

SJR_END_EXTERN

#endif /* LIVE_F1_DISPLAY_H */
