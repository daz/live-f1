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

void start_get_auth_cookie    (StateReader *r);
void start_get_decryption_key (StateReader *r);
void start_get_key_frame      (StateReader *r);

SJR_END_EXTERN

#endif /* LIVE_F1_HTTP_H */
