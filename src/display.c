/* live-f1
 *
 * display.c - displaying of timing and messages
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <stdlib.h>
#include <string.h>

#include <curses.h>

#include "live-f1.h"
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
	COLOUR_POPUP,
	LAST_COLOUR
} Colours;


/* Curses display running */
int cursed = 0;

/* Attributes for the colours */
int attrs[LAST_COLOUR];

/* Various windows */
static WINDOW *boardwin = 0;
static WINDOW *popupwin = 0;


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

	if (start_color () || (COLOR_PAIRS < LAST_COLOUR)) {
		/* Black and white */
		attrs[COLOUR_DEFAULT] = A_NORMAL;
		attrs[COLOUR_LATEST]  = A_BOLD;
		attrs[COLOUR_PIT]     = A_NORMAL;
		attrs[COLOUR_BEST]    = A_STANDOUT;
		attrs[COLOUR_RECORD]  = A_STANDOUT | A_BOLD;
		attrs[COLOUR_DATA]    = A_NORMAL;
		attrs[COLOUR_OLD]     = A_DIM;
		attrs[COLOUR_POPUP]   = A_REVERSE;
	} else {
		init_pair (COLOUR_DEFAULT, COLOR_WHITE,   COLOR_BLACK);
		init_pair (COLOUR_LATEST,  COLOR_WHITE,   COLOR_BLACK);
		init_pair (COLOUR_PIT,     COLOR_RED,     COLOR_BLACK);
		init_pair (COLOUR_BEST,    COLOR_GREEN,   COLOR_BLACK);
		init_pair (COLOUR_RECORD,  COLOR_MAGENTA, COLOR_BLACK);
		init_pair (COLOUR_DATA,    COLOR_CYAN,    COLOR_BLACK);
		init_pair (COLOUR_OLD,     COLOR_YELLOW,  COLOR_BLACK);
		init_pair (COLOUR_POPUP,   COLOR_WHITE,   COLOR_BLUE);

		attrs[COLOUR_DEFAULT] = COLOR_PAIR(COLOUR_DEFAULT);
		attrs[COLOUR_LATEST]  = COLOR_PAIR(COLOUR_LATEST);
		attrs[COLOUR_PIT]     = COLOR_PAIR(COLOUR_PIT);
		attrs[COLOUR_BEST]    = COLOR_PAIR(COLOUR_BEST);
		attrs[COLOUR_RECORD]  = COLOR_PAIR(COLOUR_RECORD);
		attrs[COLOUR_DATA]    = COLOR_PAIR(COLOUR_DATA);
		attrs[COLOUR_OLD]     = COLOR_PAIR(COLOUR_OLD);
		attrs[COLOUR_POPUP]   = COLOR_PAIR(COLOUR_POPUP) | A_BOLD;
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
	open_display ();
	close_popup ();

	if (boardwin)
		delwin (boardwin);

	boardwin = newwin (22, 60, 0, 0);
	wbkgdset (boardwin, attrs[COLOUR_DEFAULT]);
	werase (boardwin);
	box (boardwin, 0, 0);

	mvwaddstr(boardwin, 1, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin, 2, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin, 3, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin, 4, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin, 5, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin, 6, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin, 7, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin, 8, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin, 9, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin,10, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin,11, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin,12, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin,13, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin,14, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin,15, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin,16, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin,17, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin,18, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin,19, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");
	mvwaddstr(boardwin,20, 1, " P    Name            Gap  Int Time     Sec1 Sec2 Sec3 Pit");

	wnoutrefresh (boardwin);
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
	if (! cursed)
		return;

	if (popupwin) {
		wgetch (popupwin);
		delwin (popupwin);
	}
	if (boardwin)
		delwin (boardwin);

	endwin ();

	cursed = 0;
}

/**
 * popup_message:
 * @message: message to display.
 *
 * Displays a popup message over top of the screen, calling doupdate() when
 * done.  This can be dismisssed by calling close_popup().
 **/
void
popup_message (const unsigned char *message)
{
	unsigned char *msg;
	size_t         msglen;
	int            nlines, ncols, col, ls, i;

	open_display ();
	close_popup ();


	msglen = strlen ((char *) message);
	msg = malloc (msglen + 1);
	strcpy ((char *) msg, (const char *) message);
	while (msglen && strchr(" \t\r\n", msg[msglen - 1]))
		msg[--msglen] = 0;

	if (!msglen) {
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
	if (!cursed || !popupwin)
		return;

	delwin (popupwin);
	popupwin = 0;

	redrawwin (stdscr);
	wnoutrefresh (stdscr);

	if (boardwin) {
		redrawwin (boardwin);
		wnoutrefresh (boardwin);
	}
}
