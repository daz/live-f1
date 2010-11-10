/* live-f1
 *
 * display.c - displaying of timing and messages
 *
 * Copyright © 2010 Scott James Remnant <scott@netsplit.com>.
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
#include <curses.h>
#include <time.h>
#include <regex.h>

#include "live-f1.h"
#include "packet.h" /* for packet type */
#include "display.h"


/* Colours to be allocated, note that this mostly matches the data stream
 * values except that 0 is default text here and empty for the data stream,
 * we take care of clearing it instead.
 */
typedef enum {
	COLOUR_DEFAULT,
	COLOUR_LATEST,
	COLOUR_PIT,
	COLOUR_BEST,
	COLOUR_RECORD,
	COLOUR_DATA,
	COLOUR_OLD,
	COLOUR_ELIMINATED,
	COLOUR_POPUP,
	COLOUR_GREEN_FLAG,
	COLOUR_YELLOW_FLAG,
	COLOUR_RED_FLAG,
	LAST_COLOUR
} TextColour;


/* Forward prototypes */
static void _update_cell (CurrentState *state, int car, int type);
static void _update_time (CurrentState *state);


/* Curses display running */
int cursed = 0;

/* Number of lines being used for the board */
static int nlines = 0;

/* Attributes for the colours */
static int attrs[LAST_COLOUR];

/* Various windows */
static WINDOW *boardwin = NULL;
static WINDOW *statwin = NULL;
static WINDOW *popupwin = NULL;


/**
 * open_display:
 * @state: application state structure.
 *
 * Opens the curses display to display timing information.
 **/
void
open_display (void)
{
	if (cursed)
		return;

	initscr ();
	cbreak ();
	noecho ();

	nonl ();
	intrflush (stdscr, FALSE);
	keypad (stdscr, TRUE);
	nodelay (stdscr, TRUE);

	if (start_color () || (COLOR_PAIRS < LAST_COLOUR)) {
		/* Black and white */
		attrs[COLOUR_DEFAULT]     = A_NORMAL;
		attrs[COLOUR_LATEST]      = A_BOLD;
		attrs[COLOUR_PIT]         = A_NORMAL;
		attrs[COLOUR_BEST]        = A_STANDOUT;
		attrs[COLOUR_RECORD]      = A_STANDOUT | A_BOLD;
		attrs[COLOUR_DATA]        = A_NORMAL;
		attrs[COLOUR_OLD]         = A_DIM;
		attrs[COLOUR_ELIMINATED]  = A_DIM;
		attrs[COLOUR_POPUP]       = A_REVERSE;
		attrs[COLOUR_GREEN_FLAG]  = A_NORMAL;
		attrs[COLOUR_YELLOW_FLAG] = A_BOLD;
		attrs[COLOUR_RED_FLAG]    = A_REVERSE;
	} else {
		init_pair (COLOUR_DEFAULT,     COLOR_WHITE,   COLOR_BLACK);
		init_pair (COLOUR_LATEST,      COLOR_WHITE,   COLOR_BLACK);
		init_pair (COLOUR_PIT,         COLOR_RED,     COLOR_BLACK);
		init_pair (COLOUR_BEST,        COLOR_GREEN,   COLOR_BLACK);
		init_pair (COLOUR_RECORD,      COLOR_MAGENTA, COLOR_BLACK);
		init_pair (COLOUR_DATA,        COLOR_CYAN,    COLOR_BLACK);
		init_pair (COLOUR_OLD,         COLOR_YELLOW,  COLOR_BLACK);
		init_pair (COLOUR_ELIMINATED,  COLOR_BLACK,   COLOR_BLACK);
		init_pair (COLOUR_POPUP,       COLOR_WHITE,   COLOR_BLUE);
		init_pair (COLOUR_GREEN_FLAG,  COLOR_GREEN,   COLOR_BLACK);
		init_pair (COLOUR_YELLOW_FLAG, COLOR_YELLOW,  COLOR_BLACK);
		init_pair (COLOUR_RED_FLAG,    COLOR_RED,     COLOR_BLACK);

		attrs[COLOUR_DEFAULT]     = COLOR_PAIR (COLOUR_DEFAULT);
		attrs[COLOUR_LATEST]      = COLOR_PAIR (COLOUR_LATEST);
		attrs[COLOUR_PIT]         = COLOR_PAIR (COLOUR_PIT);
		attrs[COLOUR_BEST]        = COLOR_PAIR (COLOUR_BEST);
		attrs[COLOUR_RECORD]      = COLOR_PAIR (COLOUR_RECORD);
		attrs[COLOUR_DATA]        = COLOR_PAIR (COLOUR_DATA);
		attrs[COLOUR_OLD]         = COLOR_PAIR (COLOUR_OLD);
		attrs[COLOUR_ELIMINATED]  = COLOR_PAIR (COLOUR_ELIMINATED) | A_BOLD;
		attrs[COLOUR_POPUP]       = COLOR_PAIR (COLOUR_POPUP) | A_BOLD;
		attrs[COLOUR_GREEN_FLAG]  = COLOR_PAIR (COLOUR_GREEN_FLAG) | A_REVERSE;
		attrs[COLOUR_YELLOW_FLAG] = COLOR_PAIR (COLOUR_YELLOW_FLAG) | A_REVERSE;
		attrs[COLOUR_RED_FLAG]    = COLOR_PAIR (COLOUR_RED_FLAG) | A_REVERSE;
	}

	bkgdset (attrs[COLOUR_DEFAULT]);
	clear ();
	refresh ();

	cursed = 1;
}

/**
 * clear_board;
 * @state: application state structure.
 *
 * Clear an area on the screen for the timing board and put the headers
 * in.  Updates display when done.
 **/
void
clear_board (CurrentState *state)
{
	int i, j;

	open_display ();
	close_popup ();

	if (boardwin)
		delwin (boardwin);

	nlines = MAX (state->num_cars, 21);
	for (i = 0; i < state->num_cars; i++)
		nlines = MAX (nlines, state->car_position[i]);

	nlines += 3;

	if (LINES < nlines) {
		close_display ();
		fprintf (stderr, "%s: %s\n", program_name,
			 _("insufficient lines on display"));
		exit (10);
	}
	if (COLS < 69) {
		close_display ();
		fprintf (stderr, "%s: %s\n", program_name,
			 _("insufficient columns on display"));
		exit (10);
	}

	boardwin = newwin (nlines, 69, 0, 0);
	wbkgdset (boardwin, attrs[COLOUR_DATA]);
	werase (boardwin);

		switch (state->event_type) {
		case RACE_EVENT:
			mvwprintw (boardwin, 0, 0,
				   "%2s %2s %-14s %4s %4s %-8s %-8s %-8s %-8s %2s",
				   _("P"), _(""), _("Name"), _("Gap"), _("Int"),
				   _("Time"), _("Sector 1"), _("Sector 2"),
				   _("Sector 3"), _("Ps"));
			break;
		case PRACTICE_EVENT:
			mvwprintw (boardwin, 0, 0,
				   "%2s %2s %-14s %-8s %6s %5s %5s %5s %-4s",
				   _("P"), _(""), _("Name"), _("Best"), _("Gap"),
				   _("Sec 1"), _("Sec 2"), _("Sec 3"), _(" Lap"));
			break;
		case QUALIFYING_EVENT:
			mvwprintw (boardwin, 0, 0,
				   "%2s %2s %-14s %-8s %-8s %-8s %5s %5s %5s %-2s",
				   _("P"), _(""), _("Name"), _("Period 1"),
				   _("Period 2"), _("Period 3"), _("Sec 1"),
				   _("Sec 2"), _("Sec 3"), _("Lp"));
			break;
		}

	for (i = 1; i <= state->num_cars; i++) {
		for (j = 0; j < LAST_CAR_PACKET; j++)
			_update_cell (state, i, j);
	}

	wnoutrefresh (boardwin);
	doupdate ();

	if (statwin) {
		delwin (statwin);
		statwin = NULL;

		update_status (state);
	}
}

/**
 * _update_cell:
 * @state: application state structure,
 * @car: car number to update,
 * @type: atom to update.
 *
 * Update a particular cell on the board, with the necessary information
 * available in the state structure.  For internal use, does not refresh
 * or update the screen.
 **/
static void
_update_cell (CurrentState *state,
	      int           car,
	      int           type)
{
	int         y, x, sz, align, attr;
	CarAtom    *atom;
	const char *text;
	size_t      len, pad;

	y = state->car_position[car - 1];
	if (! y)
		return;
	if (nlines < y)
		clear_board (state);

	switch (state->event_type) {
	case RACE_EVENT:
		switch ((RaceAtomType) type) {
		case RACE_POSITION:
			x = 0;
			sz = 2;
			align = 1;
			break;
		case RACE_NUMBER:
			x = 3;
			sz = 2;
			align = 1;
			break;
		case RACE_DRIVER:
			x = 6;
			sz = 14;
			align = -1;
			break;
		case RACE_GAP:
			x = 21;
			sz = 4;
			align = 1;
			break;
		case RACE_INTERVAL:
			x = 26;
			sz = 4;
			align = 1;
			break;
		case RACE_LAP_TIME:
			x = 31;
			sz = 8;
			align = -1;
			break;
		case RACE_SECTOR_1:
			x = 40;
			sz = 4;
			align = 1;
			break;
		case RACE_PIT_LAP_1:
			x = 45;
			sz = 3;
			align = -1;
			break;
		case RACE_SECTOR_2:
			x = 49;
			sz = 4;
			align = 1;
			break;
		case RACE_PIT_LAP_2:
			x = 54;
			sz = 3;
			align = -1;
			break;
		case RACE_SECTOR_3:
			x = 58;
			sz = 4;
			align = 1;
			break;
		case RACE_PIT_LAP_3:
			x = 63;
			sz = 3;
			align = -1;
			break;
		case RACE_NUM_PITS:
			x = 67;
			sz = 2;
			align = 1;
			break;
		default:
			return;
		}
		break;
	case PRACTICE_EVENT:
		switch ((PracticeAtomType) type) {
		case PRACTICE_POSITION:
			x = 0;
			sz = 2;
			align = 1;
			break;
		case PRACTICE_NUMBER:
			x = 3;
			sz = 2;
			align = 1;
			break;
		case PRACTICE_DRIVER:
			x = 6;
			sz = 14;
			align = -1;
			break;
		case PRACTICE_BEST:
			x = 21;
			sz = 8;
			align = 1;
			break;
		case PRACTICE_GAP:
			x = 30;
			sz = 6;
			align = 1;
			break;
		case PRACTICE_SECTOR_1:
			x = 37;
			sz = 5;
			align = 1;
			break;
		case PRACTICE_SECTOR_2:
			x = 43;
			sz = 5;
			align = 1;
			break;
		case PRACTICE_SECTOR_3:
			x = 49;
			sz = 5;
			align = 1;
			break;
		case PRACTICE_LAP:
			x = 55;
			sz = 4;
			align = 1;
			break;
		default:
			return;
		}
		break;
	case QUALIFYING_EVENT:
		switch ((QualifyingAtomType) type) {
		case QUALIFYING_POSITION:
			x = 0;
			sz = 2;
			align = 1;
			break;
		case QUALIFYING_NUMBER:
			x = 3;
			sz = 2;
			align = 1;
			break;
		case QUALIFYING_DRIVER:
			x = 6;
			sz = 14;
			align = -1;
			break;
		case QUALIFYING_PERIOD_1:
			x = 21;
			sz = 8;
			align = 1;
			break;
		case QUALIFYING_PERIOD_2:
			x = 30;
			sz = 8;
			align = 1;
			break;
		case QUALIFYING_PERIOD_3:
			x = 39;
			sz = 8;
			align = 1;
			break;
		case QUALIFYING_SECTOR_1:
			x = 48;
			sz = 5;
			align = 1;
			break;
		case QUALIFYING_SECTOR_2:
			x = 54;
			sz = 5;
			align = 1;
			break;
		case QUALIFYING_SECTOR_3:
			x = 60;
			sz = 5;
			align = 1;
			break;
		case QUALIFYING_LAP:
			x = 66;
			sz = 2;
			align = 1;
			break;
		default:
			return;
		}
		break;
	default:
		return;
	}

	atom = &state->car_info[car - 1][type];
	attr = attrs[atom->data];
	text = atom->text;
	len = strlen ((const char *) text);

	/* Check for over-long atoms */
	if (len > sz) {
		text = "";
		len = 0;
	}
	pad = sz - len;

	wmove (boardwin, y, x);
	if (len) {
		wattrset (boardwin, attr);
	} else {
		wattrset (boardwin, attrs[COLOUR_DEFAULT]);
	}

	while ((align > 0) && pad--)
		waddch (boardwin, ' ');
	waddstr (boardwin, text);
	while ((align < 0) && pad--)
		waddch (boardwin, ' ');
}

/**
 * update_cell:
 * @state: application state structure,
 * @car: car number to update,
 * @type: atom to update.
 *
 * Update a particular cell on the board, with the necessary information
 * available in the state structure.  Intended for external code as it
 * updates the display when done.
 **/
void
update_cell (CurrentState *state,
	     int           car,
	     int           type)
{
	if (! cursed)
		clear_board (state);
	close_popup ();

	_update_cell (state, car, type);

	_update_time (state);
 	wnoutrefresh (boardwin);
	doupdate ();
}

/**
 * update_car:
 * @state: application state structure,
 * @car: car number to update.
 *
 * Update the entire row for the given car, and the display when done.
 **/
void
update_car (CurrentState *state,
	    int           car)
{
	int i;

	if (! cursed)
		clear_board (state);
	close_popup ();

	for (i = 0; i < LAST_CAR_PACKET; i++)
		_update_cell (state, car, i);

	_update_time (state);
 	wnoutrefresh (boardwin);
	doupdate ();
}

/**
 * clear_car:
 * @state: application state structure,
 * @car: car number to update.
 *
 * Clear the car from the board, updating the display when done.
 **/
void
clear_car (CurrentState *state,
	   int           car)
{
	int y;

	if (! cursed)
		clear_board (state);

	y = state->car_position[car - 1];
	if (! y)
		return;
	if (nlines < y)
		clear_board (state);

	close_popup ();

	wmove (boardwin, y, 0);
	wclrtoeol (boardwin);

	_update_time (state);
	wnoutrefresh (boardwin);
	doupdate ();
}

/**
 * draw_bar:
 * @win: the window into which to draw
 * @len: the length of the bar to draw
 *
 * Draws a solid bar of a specified length, at the current cursor
 * position in the specified window.
 **/
void
draw_bar (WINDOW *win, int len)
{
	int i;

	for (i = 0; i < len; i++)
	{
		waddch (win, ' ');
	}
}

/**
 * update_status:
 * @state: application state structure,
 *
 * Update the status window, creating it if necessary.  Updates the
 * display when done.
 **/
void
update_status (CurrentState *state)
{
	if (! cursed)
		clear_board (state);
	close_popup ();

	/* Put the window down the side if we have enough room */
	if (! statwin) {
		if (COLS < 80)
			return;

		statwin = newwin (nlines, 10, 0, COLS - 10);
		wbkgdset (statwin, attrs[COLOUR_DATA]);
		werase (statwin);
	}

	/* Session status */

	wmove (statwin, 2, 0);
	wclrtoeol (statwin);
	switch (state->flag) {
	case YELLOW_FLAG:
	case SAFETY_CAR_STANDBY:
	case SAFETY_CAR_DEPLOYED:
		wattrset (statwin, attrs[COLOUR_YELLOW_FLAG]);
		draw_bar (statwin, 10);
		break;
	case RED_FLAG:
		wattrset (statwin, attrs[COLOUR_RED_FLAG]);
		draw_bar (statwin, 10);
		break;
	}
	wmove (statwin, 3, 0);
	wclrtoeol (statwin);
	switch (state->flag) {
	case SAFETY_CAR_DEPLOYED:
		wattrset (statwin, attrs[COLOUR_OLD]);
		waddstr (statwin, "SAFETY CAR");
		break;
	}

	/* Number of laps, or event type */

	wattrset (statwin, attrs[COLOUR_DATA]);
	wmove (statwin, 0, 0);
	wclrtoeol (statwin);
	switch (state->event_type) {
	case RACE_EVENT:
		switch (state->total_laps - state->laps_completed) {
		case 0:
			wprintw (statwin, "%10s", "FINISHED");
			break;
		case 1:
			wprintw (statwin, "%10s", "FINAL LAP");
			break;
		default:
			wprintw (statwin, "%4d TO GO", state->total_laps - state->laps_completed);
			break;
		}
		break;
	case PRACTICE_EVENT:
		wprintw (statwin, "Practice");
		break;
	case QUALIFYING_EVENT:
		wprintw (statwin, "Qualifying");
		break;
	}

	/* Display weather */

	int wline = 5;
	wattrset (statwin, attrs[COLOUR_DATA]);
 
	wmove (statwin, wline, 0);
	wclrtoeol (statwin);
	wprintw(statwin,"%-6s%2d C", "Track", state->track_temp);
	wmove (statwin, wline, 8);
	waddch (statwin, ACS_DEGREE);

	wline += 2;

	wmove (statwin, wline, 0);
	wclrtoeol (statwin);
	wprintw(statwin,"%-6s%2d C", "Air", state->air_temp);
	wmove (statwin, wline, 8);
	waddch (statwin, ACS_DEGREE);

	wline += 2;

	wmove (statwin, wline, 0);
	wclrtoeol (statwin);
	wprintw (statwin, "%-6s%3d", "Wind", state->wind_direction);
	waddch (statwin, ACS_DEGREE);
	wline++;
	wmove (statwin, wline, 0);
	wclrtoeol (statwin);
	wprintw (statwin, "%-4s%03dm/s", "", state->wind_speed);
	wmove (statwin, wline, 5);
	waddch (statwin, '.');

	wline += 2;

	wmove (statwin, wline, 0);
	wclrtoeol (statwin);
	wprintw(statwin, "Humidity");
	wline++;
	wmove (statwin, wline, 0);
	wclrtoeol (statwin);
	wprintw (statwin, "%-6s%3d%%", "", state->humidity);

	wline += 2;

	wmove (statwin, wline, 0);
	wclrtoeol (statwin);
	wprintw(statwin, "Pressure");
	wline++;
	wmove (statwin, wline, 0);
	wclrtoeol (statwin);
	wprintw(statwin, "%-2s%6dmb", "", state->pressure);
	wmove (statwin, wline, 6);
	waddch (statwin, '.');

	/* Update fastest lap line (race only) */

	if (state->event_type == RACE_EVENT)
	{
		wmove (boardwin, nlines - 1, 3);
		wattrset (boardwin, attrs[COLOUR_RECORD]);
		wclrtoeol (boardwin);
		wprintw(boardwin, "%2s %-14s %4s %4s %8s", state->fl_car, state->fl_driver, "LAP", state->fl_lap, state->fl_time);
	}

	/* Update session clock */
	
	_update_time (state);

	/* Refresh display */

	wnoutrefresh (statwin);
	wnoutrefresh (boardwin);
	doupdate ();
}

/**
 * _update_time:
 * @state: application state structure.
 *
 * Updates the time in the status window without redrawing the display.
 **/
static void
_update_time (CurrentState *state)
{
	time_t remaining;

	if (! statwin)
		return;

	wmove (statwin, nlines - 1, 2);
	wattrset (statwin, attrs[COLOUR_DATA]);

	// Pause the clock during a red flag, but only for Qualifying and the Race.
	if ((state->flag == RED_FLAG) && (state->event_type != PRACTICE_EVENT))
	{
		remaining = state->remaining_time;
	} else if (state->epoch_time) {
		remaining = MAX ((state->epoch_time + state->remaining_time) - time (NULL), 0);
	} else {
		remaining = state->remaining_time;
	}

	if (remaining >= 3600) {
		wprintw (statwin, "%d:", remaining / 3600);
		remaining %= 3600;
		wprintw (statwin, "%02d:", remaining / 60);
		remaining %= 60;
		wprintw (statwin, "%02d", remaining);
	} else if (remaining >= 60) {
		wprintw (statwin, "0:%02d:", remaining / 60);
		remaining %= 60;
		wprintw (statwin, "%02d", remaining);
	} else {
		wprintw (statwin, "0:00:%02d", remaining);
	}

	wnoutrefresh (statwin);
}

/**
 * update_time:
 * @state: application state structure.
 *
 * External function to update the time, unlike most display functions this
 * one doesn't clear an open popup as it's not possible for them to ever
 * cover the time.  It also doesn't open the display if not already done.
 **/
void
update_time (CurrentState *state)
{
	if ((! cursed) || (! statwin))
		return;

	_update_time (state);

	doupdate ();
}

/**
 * close_display:
 *
 * Waits for the user to press a key, then closes the curses display
 * and returns to normality.
 **/
void
close_display (void)
{
	if (! cursed)
		return;

	if (popupwin)
		delwin (popupwin);
	if (boardwin)
		delwin (boardwin);

	endwin ();

	cursed = 0;
}

/**
 * handle_keys:
 * @state: application state structure.
 *
 * Checks for a key press on the keyboard and handles it; this includes
 * keys that should quit the app (Enter, Escape, q, etc.) and pseudo-keys
 * like the resize event.
 *
 * Returns: 0 if none were pressed, 1 if one was, -1 if should quit.
 **/
int
handle_keys (CurrentState *state)
{
	if (! cursed)
		return 0;

	switch (getch ()) {
	case KEY_ENTER:
	case '\r':
	case '\n':
	case 0x1b: /* Escape */
	case 'q':
	case 'Q':
		return -1;
	case KEY_RESIZE:
		clear_board (state);
		return 1;
	default:
		return 0;
	}
}

/**
 * popup_message:
 * @message: message to display.
 *
 * Displays a popup message over top of the screen, calling doupdate() when
 * done.  This can be dismisssed by calling close_popup().
 **/
void
popup_message (const char *message)
{
	char  *msg;
	size_t msglen;
	int    nlines, ncols, col, ls, i;
	regex_t re;

	open_display ();
	close_popup ();

	regcomp(&re, "^img:", REG_EXTENDED|REG_NOSUB);

	if (regexec(&re, message, (size_t)0, NULL, 0) == 0)
	{
		msg = strdup ("CURRENTLY NO LIVE SESSION");
	} else {
		msg = strdup (message);
	}
	regfree (&re);

	msglen = strlen (msg);
	while (msglen && strchr(" \t\r\n", msg[msglen - 1]))
		msg[--msglen] = 0;

	if (! msglen) {
		free (msg);
		return;
	}

	/* Calculate the popup size needed for the message.
	 * Also replaces whitespace with ordinary spaces or newlines.
	 */
	nlines = 1;
	ncols = col = ls = 0;
	for (i = 0; i < msglen; i++) {
		if (strchr (" \t\r", msg[i])) {
			msg[i] =  ' ';
			ls = i;
		} else if (msg[i] == '\n') {
			ncols = MAX (ncols, col);

			nlines++;
			col = ls = 0;
			continue;
		}

		if (++col > 58) {
			if (ls) {
				col -= i - ls + 1;
				i = ls;
				msg[i] = '\n';

				ncols = MAX (ncols, col);
			} else {
				ncols = MAX (ncols, 58);
				i--;
			}

			nlines++;
			col = ls = 0;
		}
	}
	ncols = MAX (ncols, col);

	/* Create the popup window in the middle of the screen */
	popupwin = newwin (nlines + 2, ncols + 2,
			   (LINES - (nlines + 2)) / 2,
			   (COLS - (ncols + 2)) / 2);
	wbkgdset (popupwin, attrs[COLOUR_POPUP]);
	werase (popupwin);
	box (popupwin, 0, 0);

	/* Now draw the characters into it */
	nlines = col = 0;
	for (i = 0; i < msglen; i++) {
		if (msg[i] == '\n') {
			nlines++;
			col = 0;
			continue;
		} else if (++col > 58) {
			nlines++;
			col = 1;
		}

		mvwaddch (popupwin, nlines + 1, col, msg[i]);
	}

	wnoutrefresh (popupwin);
	doupdate ();

	free (msg);
}

/**
 * close_popup:
 *
 * Close the popup window and schedule all other windows on the screen
 * to be redrawn when the next doupdate() is called.
 **/
void
close_popup (void)
{
	if ((! cursed) || (! popupwin))
		return;

	delwin (popupwin);
	popupwin = NULL;

	redrawwin (stdscr);
	wnoutrefresh (stdscr);

	if (boardwin) {
		redrawwin (boardwin);
		wnoutrefresh (boardwin);
	}

	if (statwin) {
		redrawwin (statwin);
		wnoutrefresh (statwin);
	}
}
