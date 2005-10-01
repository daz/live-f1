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

#ifndef LIVE_F1_H
#define LIVE_F1_H

#include <gettext.h>

#include <ne_session.h>

#include "macros.h"


/* Make gettext a little friendlier */
#define _(_str) gettext (_str)
#define N_(_str) gettext_noop (_str)


/**
 * CurrentState:
 * @http_sess: neon http session to the web server,
 * @data_sock: data stream socket,
 * @cookie: user's authorisation cookie,
 * @key: decryption key,
 * @frame: last seen key frame.
 **/
typedef struct {
	ne_session   *http_sess, *auth_sess; /* latter is a hack */
	int           data_sock;
	char         *cookie;
	unsigned int  key, frame;
} CurrentState;


SJR_BEGIN_EXTERN

/* Program's name */
const char *program_name;

int info (int irrelevance, const char *format, ...);

SJR_END_EXTERN

#endif /* LIVE_F1_H */
