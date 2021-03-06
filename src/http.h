/* live-f1
 *
 * Copyright © 2006 Scott James Remnant <scott@netsplit.com>.
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

#ifndef LIVE_F1_HTTP_H
#define LIVE_F1_HTTP_H

#include "live-f1.h"


SJR_BEGIN_EXTERN

char *       obtain_auth_cookie    (const char *host,
				    const char *email, const char *password);
unsigned int obtain_decryption_key (const char *host, unsigned int event_no,
				    const char *cookie);
int          obtain_key_frame      (const char *host, unsigned int frame,
				    void *unknown);

SJR_END_EXTERN

#endif /* LIVE_F1_HTTP_H */
