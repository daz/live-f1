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

#include "live-f1.h"
#include "packet.h"


SJR_BEGIN_EXTERN

/* Curses display running */
int cursed;


void open_display  (void);
void close_display (void);
int  handle_keys   (CurrentState *state);

void clear_board   (CurrentState *state);
void update_cell   (CurrentState *state, int car, int type);
void update_car    (CurrentState *state, int car);
void clear_car     (CurrentState *state, int car);

void update_status (CurrentState *state);
void update_time   (CurrentState *state);

void popup_message (const char *message);
void close_popup   (void);

SJR_END_EXTERN

#endif /* LIVE_F1_DISPLAY_H */
