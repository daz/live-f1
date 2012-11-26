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
#include "packet.h"
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
static void _update_cell   (StateModel *m, int car, int type);
static void _update_car    (StateModel *m, int car);
static void _update_status (StateModel *m);
static void _update_time   (StateModel *m);
static void _update_info   ();


/* Curses display running */
int cursed = 0;

/* Number of lines being used for the board and status */
static int nlines = 0;

/* Number of columns being used for the board */
static int ncols = 69;

/* x- and y-coordinates of board point placed in
 * the top-left corner of the screen
 */
static int startx = 0;
static int starty = 0;

/* Attributes for the colours */
static int attrs[LAST_COLOUR];

/* Various windows */
static WINDOW *boardwin = NULL;
static WINDOW *statwin = NULL;
static WINDOW *infowin = NULL;

/* Count of info_rings */
#define DISPLAY_INFO_RING_BUFFER_COUNT 9
static const size_t info_ring_count = DISPLAY_INFO_RING_BUFFER_COUNT;
/* Size of info_ring[i] */
#define DISPLAY_INFO_RING_BUFFER_SIZE 32
static const size_t info_ring_size = DISPLAY_INFO_RING_BUFFER_SIZE;
/* Max length of message in info_ring */
static const size_t info_msg_width = 80;
/* Ring buffers for info messages */
static char *info_ring[DISPLAY_INFO_RING_BUFFER_COUNT][DISPLAY_INFO_RING_BUFFER_SIZE];
/* Indexes of most recent messages in info_rings */
static size_t info_head[DISPLAY_INFO_RING_BUFFER_COUNT];

/* Index of currently displayed (in infowin) info_ring */
static size_t displayed_info_index = 0;

/**
 * open_display:
 *
 * Opens the curses display to display timing information.
 **/
static void
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
 * outrefresh_window:
 * @win: window to refresh.
 *
 * Refreshes @win (wnoutrefresh/pnoutrefresh).
 * Doesn't call doupdate.
 **/
static void
outrefresh_window (WINDOW *win)
{
	int vcols = MIN (ncols, COLS - 11);
	int vlines = MIN (nlines - starty + 1, LINES);

	if (! win)
		return;
	if (win == stdscr)
		wnoutrefresh (win);
	if (win == boardwin)
		pnoutrefresh (win,     starty,     startx,
		                            0,          0,
				   vlines - 1,  vcols - 1);
	if (win == statwin)
		pnoutrefresh (win,     starty,          0,
		                            0,  vcols + 1,
				   vlines - 1, vcols + 10);
	if (win == infowin) {
		int istarty = MAX (info_ring_size - (LINES - vlines), 0);

		pnoutrefresh (win,    istarty,     startx,
		                       vlines,          0,
				    LINES - 1,   COLS - 1);
	}
}

/**
 * outrefresh_windows:
 *
 * Refreshes all windows except stdscr.
 **/
static void
outrefresh_windows ()
{
	outrefresh_window (boardwin);
	outrefresh_window (statwin);
	outrefresh_window (infowin);
}

/**
 * outrefresh_all:
 *
 * Invalidates stdscr and refreshes all windows.
 **/
static void
outrefresh_all ()
{
	touchwin (stdscr);
	outrefresh_window (stdscr);
	outrefresh_windows ();
}

/**
 * clear_board:
 * @m: model structure.
 *
 * Clears an area on the screen for the timing board and put the headers in.
 * Also updates info window and clears and updates status window.
 * Updates display when done.
 **/
void
clear_board (StateModel *m)
{
	int i;

	open_display ();

	if (boardwin)
		delwin (boardwin);

	nlines = MAX (m->num_cars, 21);
	for (i = 0; i < m->num_cars; i++)
		nlines = MAX (nlines, m->car_position[i]);
	nlines += 3;

	boardwin = newpad (nlines + 1, ncols + 1);
	wbkgdset (boardwin, attrs[COLOUR_DATA]);
	werase (boardwin);

	switch (m->event_type) {
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

	for (i = 1; i <= m->num_cars; i++)
		_update_car (m, i);

	if (statwin) {
		delwin (statwin);
		statwin = NULL;
	}
	_update_status (m);
	_update_info ();

	outrefresh_all ();

	doupdate ();
}

/**
 * _update_cell:
 * @m: model structure.
 * @car: car number to update.
 * @type: atom to update.
 *
 * Updates a particular cell on the board, with the necessary information
 * available in the model structure. For internal use, does not refresh
 * or update the screen.
 **/
static void
_update_cell (StateModel *m,
	      int         car,
	      int         type)
{
	int         y, x, sz, align, attr;
	const char *text;
	size_t      len, pad;

	y = m->car_position[car - 1];
	if (! y)
		return;
	if (nlines < y)
		clear_board (m);

	switch (m->event_type) {
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

	if (m->car_info[car - 1]) {
		CarAtom *atom = &m->car_info[car - 1][type];

		attr = attrs[atom->data];
		text = atom->text;
		len = strlen ((const char *) text);
	} else {
		text = "";
		len = 0;
	}

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
 * @m: model structure.
 * @car: car number to update.
 * @type: atom to update.
 *
 * Updates a particular cell on the board, with the necessary information
 * available in the model structure. Intended for external code as it
 * updates the display when done.
 **/
void
update_cell (StateModel *m,
	     int         car,
	     int         type)
{
	if (! cursed)
		clear_board (m);

	_update_cell (m, car, type);

	outrefresh_window (boardwin);

	doupdate ();
}

/**
 * _update_car:
 * @m: model structure.
 * @car: car number to update.
 *
 * Updates the entire row for the given car. For internal use,
 * does not refresh or update the screen.
 **/
static void
_update_car (StateModel *m,
             int         car)
{
	int i;

	for (i = 0; i < LAST_CAR_PACKET; i++)
		_update_cell (m, car, i);
}

/**
 * update_car:
 * @m: model structure.
 * @car: car number to update.
 *
 * Update the entire row for the given car, and the display when done.
 **/
void
update_car (StateModel *m,
	    int         car)
{
	if (! cursed)
		clear_board (m);

	_update_car (m, car);

	outrefresh_window (boardwin);

	doupdate ();
}

/**
 * clear_car:
 * @m: model structure.
 * @car: car number to update.
 *
 * Clears the car from the board, updating the display when done.
 **/
void
clear_car (StateModel *m,
	   int         car)
{
	int y;

	if (! cursed)
		clear_board (m);

	y = m->car_position[car - 1];
	if (! y)
		return;
	if (nlines < y)
		clear_board (m);

	wmove (boardwin, y, 0);
	wclrtoeol (boardwin);

	outrefresh_window (boardwin);

	doupdate ();
}

/**
 * draw_bar:
 * @win: the window into which to draw.
 * @len: the length of the bar to draw.
 *
 * Draws a solid bar of a specified length, at the current cursor
 * position in the specified window.
 **/
static void
draw_bar (WINDOW *win, int len)
{
	int i;

	for (i = 0; i < len; i++)
		waddch (win, ' ');
}

/**
 * _update_status:
 * @m: model structure.
 *
 * Updates the status window, creating it if necessary. For internal use,
 * does not refresh or update the screen.
 **/
static void
_update_status (StateModel *m)
{
	if (! cursed)
		clear_board (m);

	/* Put the window down the side if we have enough room */
	if (! statwin) {
		statwin = newpad (nlines + 1, 11);
		wbkgdset (statwin, attrs[COLOUR_DATA]);
		werase (statwin);
	}

	/* Session status */

	wmove (statwin, 2, 0);
	wclrtoeol (statwin);
	switch (m->flag) {
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
	default:
		break;
	}
	wmove (statwin, 3, 0);
	wclrtoeol (statwin);
	if (m->flag == SAFETY_CAR_DEPLOYED) {
		wattrset (statwin, attrs[COLOUR_OLD]);
		waddstr (statwin, "SAFETY CAR");
	}

	/* Number of laps, or event type */

	wattrset (statwin, attrs[COLOUR_DATA]);
	wmove (statwin, 0, 0);
	wclrtoeol (statwin);
	switch (m->event_type) {
	case RACE_EVENT:
		switch (m->total_laps - m->laps_completed) {
		case 0:
			wprintw (statwin, "%10s", "FINISHED");
			break;
		case 1:
			wprintw (statwin, "%10s", "FINAL LAP");
			break;
		default:
			wprintw (statwin, "%4d TO GO", m->total_laps - m->laps_completed);
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
	wprintw(statwin,"%-6s%2d C", "Track", m->track_temp);
	wmove (statwin, wline, 8);
	waddch (statwin, ACS_DEGREE);

	wline += 2;

	wmove (statwin, wline, 0);
	wclrtoeol (statwin);
	wprintw(statwin,"%-6s%2d C", "Air", m->air_temp);
	wmove (statwin, wline, 8);
	waddch (statwin, ACS_DEGREE);

	wline += 2;

	wmove (statwin, wline, 0);
	wclrtoeol (statwin);
	wprintw (statwin, "%-6s%3d", "Wind", m->wind_direction);
	waddch (statwin, ACS_DEGREE);
	wline++;
	wmove (statwin, wline, 0);
	wclrtoeol (statwin);
	wprintw (statwin, "%-4s%03dm/s", "", m->wind_speed);
	wmove (statwin, wline, 5);
	waddch (statwin, '.');

	wline += 2;

	wmove (statwin, wline, 0);
	wclrtoeol (statwin);
	wprintw(statwin, "Humidity");
	wline++;
	wmove (statwin, wline, 0);
	wclrtoeol (statwin);
	wprintw (statwin, "%-6s%3d%%", "", m->humidity);

	wline += 2;

	wmove (statwin, wline, 0);
	wclrtoeol (statwin);
	wprintw(statwin, "Pressure");
	wline++;
	wmove (statwin, wline, 0);
	wclrtoeol (statwin);
	wprintw(statwin, "%-2s%6dmb", "", m->pressure);
	wmove (statwin, wline, 6);
	waddch (statwin, '.');

	/* Update fastest lap line (race only) */

	if (m->event_type == RACE_EVENT) {
		wmove (boardwin, nlines - 1, 3);
		wattrset (boardwin, attrs[COLOUR_RECORD]);
		wclrtoeol (boardwin);
		wprintw(boardwin, "%2s %-14s %4s %4s %8s", m->fl_car, m->fl_driver, "LAP", m->fl_lap, m->fl_time);
	}

	/* Update session clock */
	
	_update_time (m);
}

/**
 * update_status:
 * @m: model structure.
 *
 * Updates the status window, creating it if necessary. Updates the
 * display when done.
 **/
void
update_status (StateModel *m)
{
	_update_status (m);
	outrefresh_window (statwin);
	outrefresh_window (boardwin);
	doupdate ();
}

/**
 * _update_time:
 * @m: model structure.
 *
 * Updates the time in the status window without redrawing the display.
 **/
static void
_update_time (StateModel *m)
{
	time_t remaining;

	if (! statwin)
		return;

	wmove (statwin, nlines - 1, 2);
	wattrset (statwin, attrs[COLOUR_DATA]);

	/* Pause the clock during a red flag,
	 * but only for Qualifying and the Race.
	 */
	if ((m->flag == RED_FLAG) && (m->event_type != PRACTICE_EVENT))
		remaining = m->remaining_time;
	else if (m->model_time && m->remaining_time && m->epoch_time) {
		time_t maxtime = MAX (m->epoch_time + m->remaining_time, m->model_time);

		remaining = maxtime - m->model_time;
	} else
		remaining = m->remaining_time;

	remaining %= 86400;
	wprintw (statwin, "%d:", remaining / 3600);
	remaining %= 3600;
	wprintw (statwin, "%02d:", remaining / 60);
	remaining %= 60;
	wprintw (statwin, "%02d", remaining);
}

/**
 * update_time:
 * @m: model structure.
 *
 * External function to update the time, unlike most display functions this
 * one doesn't open the display if not already done.
 **/
void
update_time (StateModel *m)
{
	if ((! cursed) || (! statwin))
		return;

	_update_time (m);
	outrefresh_window (statwin);

	doupdate ();
}

/**
 * close_display:
 *
 * Closes the curses display and returns to normality.
 **/
void
close_display (void)
{
	size_t i, j;

	for (i = 0; i < info_ring_count; ++i) {
		for (j = 0; j < info_ring_size; ++j) {
			free (info_ring[i][j]);
			info_ring[i][j] = NULL;
		}
	}

	if (! cursed)
		return;

	if (boardwin)
		delwin (boardwin);
	if (statwin)
		delwin (statwin);
	if (infowin)
		delwin (infowin);

	endwin ();

	cursed = 0;
}

/**
 * handle_keys:
 * @m: model structure.
 *
 * Checks for a key press on the keyboard and handles it; this includes
 * keys that should quit the app (Enter, Escape, q, etc.) and pseudo-keys
 * like the resize event.
 *
 * Returns: 0 if none were pressed, 1 if one was, -1 if should quit.
 **/
int
handle_keys (StateModel *m)
{
	if (! cursed)
		return 0;

	int ch = getch ();
	switch (ch) {
		size_t new_disp_info_index;

	case KEY_ENTER:
	case '\r':
	case '\n':
	case 0x1b: /* Escape */
	case 'q':
	case 'Q':
		return -1;
	case KEY_RESIZE:
		outrefresh_all ();
		doupdate ();
		return 1;
	case KEY_LEFT:
		if (startx > 0) {
			--startx;
			outrefresh_window (boardwin);
			outrefresh_window (infowin);
			doupdate ();
		}
		return 1;
	case KEY_RIGHT:
		if (startx + 1 < ncols) {
			++startx;
			outrefresh_window (boardwin);
			outrefresh_window (infowin);
			doupdate ();
		}
		return 1;
	case KEY_UP:
		if (starty > 0) {
			--starty;
			outrefresh_all ();
			doupdate ();
		}
		return 1;
	case KEY_DOWN:
		if (starty + 1 < nlines) {
			++starty;
			outrefresh_all ();
			doupdate ();
		}
		return 1;
	case 'i':
	case 'I':
		++m->time_gap;
		return 1;
	case 'k':
	case 'K':
		if (m->time_gap + m->replay_gap >= 1)
			--m->time_gap;
		return 1;
	case 'u':
	case 'U':
		m->time_gap += 60;
		return 1;
	case 'j':
	case 'J':
		if (m->time_gap + m->replay_gap >= 60)
			m->time_gap -= 60;
		return 1;
	case '0':
		if (m->time_gap) {
			m->last_time_gap = m->time_gap;
			m->time_gap = 0;
		} else
			m->time_gap = m->last_time_gap;
		return 1;
	case 'p':
	case 'P':
		m->paused = ! m->paused;
		return 1;
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		new_disp_info_index = ch - '1';
		if (new_disp_info_index >= info_ring_count)
			new_disp_info_index = info_ring_count - 1;
		if (displayed_info_index != new_disp_info_index) {
			displayed_info_index = new_disp_info_index;
			_update_info ();
			outrefresh_window (infowin);
			doupdate ();
		}
		return 1;
	default:
		return 0;
	}
}

/**
 * new_info_line:
 * @index: index of info_ring.
 * @with_time: true if time header required.
 * @spaces: blank spaces at the beginning of a line.
 *
 * Adds new string of info_msg_width length to info_ring[@index] and write
 * time (@with_time == true) or @spaces at the beginning of it.
 *
 * Returns: header symbols count or -1 on failure.
 **/
static int
new_info_line (size_t index, int with_time, int spaces)
{
	if (index >= info_ring_count)
		return -1;
	info_head[index] = (info_head[index] + 1) % info_ring_size;
	if (! info_ring[index][info_head[index]]) {
		info_ring[index][info_head[index]] = malloc (info_msg_width + 1);
		if (! info_ring[index][info_head[index]])
			return -1;
	}
	memset (info_ring[index][info_head[index]], 0, info_msg_width + 1);
	if (with_time) {
		time_t ct = time (NULL) % 86400;
		int    h, m, s;

		if (ct < 0)
			ct += 86400;
		h = ct / 3600;
		m = (ct % 3600) / 60;
		s = ct % 60;
		return snprintf (info_ring[index][info_head[index]],
				 info_msg_width + 1,
				 "%02d:%02d:%02d ", h, m, s);
	} else {
		int res = MIN (spaces, info_msg_width);

		memset (info_ring[index][info_head[index]], ' ', res);
		return res;
	}
}

/**
 * split_message:
 * @index: index of info_ring.
 * @message: message to split.
 *
 * Splits long message by words and adds it to info_ring[@index].
 * Also replaces whitespaces with ordinary spaces.
 **/
static void
split_message (size_t index, const char *message)
{
	const size_t  len = strlen (message);
	int           col, ls, i;
	const int     spaces = new_info_line (index, 1, 0);
	char         *msg;

	if (index >= info_ring_count)
		return;
	msg = info_ring[index][info_head[index]];
	col = spaces;
	if ((col < 0) || (col >= info_msg_width))
		return;
	ls = -1;
	for (i = 0; i < len; i++) {
		msg[col] = message[i];
		if (strchr (" \t\r\n", message[i])) {
			msg[col] =  ' ';
			ls = i;
		}
		++col;
		if ((message[i] == '\n') || (col >= info_msg_width)) {
			if (ls > 0) {
				col -= i + 1 - ls;
				i = ls;
			}
			msg[col] = 0;
			col = new_info_line (index, 0, spaces);
			if ((col < 0) || (col >= info_msg_width))
				return;
			msg = info_ring[index][info_head[index]];
			ls = -1;
		}
	}
	msg[col] = 0;
}

/**
 * _update_info:
 *
 * Updates the info window, creating it if necessary. For internal use,
 * does not refresh or update the screen.
 **/
static void
_update_info ()
{
	size_t i, line;

	if (displayed_info_index >= info_ring_count)
		return;

	if (! infowin) {
		infowin = newpad (info_ring_size + 1, info_msg_width + 1);
		wbkgdset (infowin, attrs[COLOUR_POPUP]);
		werase (infowin);
	}

	i = (info_head[displayed_info_index] + 1) % info_ring_size;
	for (line = 0; line < info_ring_size; i = (i + 1) % info_ring_size, ++line) {
		wmove (infowin, line, 0);
		wclrtoeol (infowin);
		if (! info_ring[displayed_info_index][i])
			continue;
		wmove (infowin, line, 0);
		waddnstr (infowin, info_ring[displayed_info_index][i], info_msg_width);
	}

}

/**
 * info_message:
 * @index: index of info_ring.
 * @message: message to display.
 *
 * Adds @message to info_ring[i] for each i >= @index and
 * displays it on the screen if displayed_info_index >= @index.
 **/
void
info_message (size_t index, const char *message)
{
	char  *msg;
	size_t msglen;
	static regex_t *re = NULL;

	if (! re) {
		re = malloc (sizeof (*re));
		if (! re)
			return;
		regcomp(re, "^img:", REG_EXTENDED|REG_NOSUB);
	}

	if (regexec(re, message, 0, NULL, 0) == 0)
		msg = strdup ("CURRENTLY NO LIVE SESSION");
	else
		msg = strdup (message);

	msglen = strlen (msg);
	while (msglen && strchr(" \t\r\n", msg[msglen - 1]))
		msg[--msglen] = 0;
	if (msglen) {
		size_t i;

		for (i = index; i < info_ring_count; ++i)
			split_message (i, msg);
	}

	free (msg);

	if ((! msglen) || (! cursed) || (displayed_info_index < index))
		return;

	_update_info ();

	outrefresh_window (infowin);
	doupdate ();
}
