/* live-f1
 *
 * http.c - handle web-site authentication and keyframe grabbing
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ne_session.h>
#include <ne_request.h>
#include <ne_uri.h>

#include "live-f1.h"
#include "http.h"


/* URLs to important places on the live-timing site */
#define LOGIN_URL           "/reg/login.asp"
#define REGISTER_URL        "/reg/register.asp"
#define KEY_URL_BASE        "/reg/getkey/"
#define KEYFRAME_URL_PREFIX "/keyframe"


/* Forward prototypes */
static char *obtain_cookie    (ne_session *sess,
			       const char *email, const char *password);
static void  parse_cookie_hdr (char **value, const char  *header);
static int   obtain_key       (ne_session *sess, unsigned int session,
			       const char *cookie, unsigned int *key);
static void  parse_key_body   (unsigned int *key, const char *buf, size_t len);


/**
 * numlen:
 * @number: number to calculate length of.
 *
 * Returns: the number of digits in number.
 **/
static inline size_t
numlen (unsigned int number)
{
	size_t len = 1;

	while (number /= 10)
		len++;

	return len;
}


/**
 * obtain_decryption_key:
 * @hostname: live timing hostname,
 * @session: race session number,
 * @email: e-mail address registered with the F1 website,
 * @password: paassword registered for @email,
 * @key: pointer to store decryption key in.
 *
 * Obtains the decryption key for the given race session, this involves
 * obtaining an authorisation cookie from the F1 website itself and then
 * using that to request the key.
 *
 * Returns: 0 on success, non-zero on failure.
 **/
int
obtain_decryption_key (const char   *hostname,
		       unsigned int  session,
		       const char   *email,
		       const char   *password,
		       unsigned int *key)
{
	ne_session *sess;
	char       *cookie;
	int         ret = 1;

	sess = ne_session_create ("http", hostname, 80);
	ne_set_useragent (sess, PACKAGE_STRING);

	cookie = obtain_cookie (sess, email, password);
	if (cookie) {
		ret = obtain_key (sess, session, cookie, key);
		free (cookie);
	}

	ne_session_destroy (sess);

	return ret;
}

/**
 * obtain_cookie:
 * @sess: the neon session to use,
 * @email: e-mail address registered with the F1 website,
 * @password: paassword registered for @email.
 *
 * Obtains the user's authentication cookie for the live timing website
 * by logging in with their e-mail address and password.
 *
 * For convenience sake, the cookie is never unencoded.
 *
 * Returns: cookie in newly allocated string or NULL on failure.
 **/
static char *
obtain_cookie (ne_session *sess,
	       const char *email,
	       const char *password)
{
	ne_request *req;
	char       *cookie = NULL, *body, *e_email, *e_password;

	printf ("%s\n", _("Obtaining authentication cookie ..."));

	/* Encode the e-mail and password as a form */
	e_email = ne_path_escape (email);
	e_password = ne_path_escape (password);
	body = malloc (strlen (e_email) + strlen (e_password) + 17);
	sprintf (body, "email=%s&password=%s", e_email, e_password);
	free (e_password);
	free (e_email);

	/* Create the request */
	req = ne_request_create (sess, "POST", LOGIN_URL);
	ne_add_request_header (req, "Content-Type",
			       "application/x-www-form-urlencoded");
	ne_set_request_body_buffer (req, body, strlen (body));

	/* Set the handler for the cookie header */
	ne_add_response_header_handler (req, "Set-Cookie",
					(ne_header_handler) parse_cookie_hdr,
					&cookie);

	/* Dispatch the event, and check it was a good one */
	if (ne_request_dispatch (req)) {
		fprintf (stderr, "%s: %s: %s\n", program_name,
			 _("login request failed"), ne_get_error (sess));
	} else if (ne_get_status (req)->code >= 400) {
		fprintf (stderr, "%s: %s: %s\n", program_name,
			 _("login request failed"),
			 ne_get_status (req)->reason_phrase);
	}

	ne_request_destroy (req);
	return cookie;
}

/**
 * parse_cookie_hdr:
 * @value: pointer to store allocated string,
 * @header: header to parse.
 *
 * Parses an HTTP cookie header looking for the USER cookie, and if found
 * sets @value to point to a newly allocated string containing the cookie
 * value; for convenience sake the cookie is never unencoded.
 **/
static void
parse_cookie_hdr (char       **value,
		  const char  *header)
{
	size_t len;

	if (strncmp (header, "USER=", 5))
		return;

	header += 5;
	len = strcspn (header, ";");

	*value = malloc (len + 1);
	strncpy (*value, header, len);
	(*value)[len] = 0;
}

/**
 * obtain_key:
 * @sess: the neon session to use,
 * @session: race session number,
 * @cookie: uri-encoded cookie.
 * @key: pointer to store decryption key in.
 *
 * Requests the decryption key for the race session using the authorisation
 * cookie obtained.
 *
 * Returns: 0 on success, non-zero on failure.
 **/
static int
obtain_key (ne_session   *sess,
	    unsigned int  session,
	    const char   *cookie,
	    unsigned int *key)
{
	ne_request *req;
	char       *url;
	int         ret = 0;

	*key = 0;

	printf ("%s\n", _("Obtaining decryption key ..."));

	url = malloc (strlen (KEY_URL_BASE) + numlen (session)
		      + strlen (cookie) + 11);
	sprintf (url, "%s%u.asp?auth=%s", KEY_URL_BASE, session, cookie);

	/* Create the request */
	req = ne_request_create (sess, "GET", url);
	ne_add_response_body_reader (req, ne_accept_2xx,
				     (ne_block_reader) parse_key_body, key);

	/* Dispatch the event */
	if (ne_request_dispatch (req)) {
		fprintf (stderr, "%s: %s: %s\n", program_name,
			 _("key request failed"), ne_get_error (sess));
		ret = 1;
	}

	free (url);
	ne_request_destroy (req);
	return ret;
}

/**
 * parse_key_body:
 * @key: pointer to store decryption key in,
 * @buf: buffer of data received from server,
 * @len: length of buffer.
 *
 * Parse data received from the server in response to the key request,
 * filling the decryption key while we can see hexadecimal digits.
 **/
static void
parse_key_body (unsigned int *key,
		const char   *buf,
		size_t        len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if ((buf[i] >= '0') && (buf[i] <= '9')) {
			*key = (*key << 4) | (buf[i] - '0');
		} else if ((buf[i] >= 'a') && (buf[i] <= 'f')) {
			*key = (*key << 4) | (buf[i] - 'a' + 10);
		} else if ((buf[i] >= 'A') && (buf[i] <= 'F')) {
			*key = (*key << 4) | (buf[i] - 'A' + 10);
		} else {
			break;
		}
	}
}
