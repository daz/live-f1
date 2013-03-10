/* live-f1
 *
 * http.c - handle web-site authentication and keyframe grabbing
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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <event2/http.h>

#include "live-f1.h"
#include "packet.h"
#include "stream.h"
#include "http.h"


/* URLs to important places on the live-timing site */
#define LOGIN_URL           "/reg/login"
#define REGISTER_URL        "/reg/registration"
#define KEY_URL_BASE        "/reg/getkey/"
#define KEYFRAME_URL_PREFIX "/keyframe"


/* HTTP request structure (one request per connection) */
typedef struct {
	StateReader              *r;
	struct evhttp_connection *conn;
	struct evhttp_request    *req;
	void                     *userdata;
} HTTPRequest;

/* HTTP request result (response) */
typedef struct {
	StateReader     *r;
	struct evbuffer *response;
	void            *userdata;
} HTTPRequestResult;

/* Event number and event type (race/qualifying/practice) */
typedef struct {
	unsigned int no;
	EventType    type;
} EventNumTypePair;


/* Forward prototypes */
static void parse_cookie_hdr (char **value, const char  *header);
static void parse_hex_number (unsigned int *num, const char *buf, size_t len);
static void parse_dec_number (unsigned int *num, const char *buf, size_t len);


/**
 * numlen:
 * @number: number to calculate length of.
 *
 * Returns: the number of decimal digits in @number.
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
 * is_valid_http_response_code:
 * @code: HTTP response code.
 *
 * Returns: non-zero if non-error HTTP response.
 **/
static int
is_valid_http_response_code (int code)
{
	return code < 400;
}

/**
 * get_host_and_port:
 * @hostname[in]: source URI.
 * @fullname[out]: pointer to save host part of @hostname (may be NULL).
 * @port[out]: pointer to save port of @hostname (may be NULL).
 *
 * Parses @hostname.
 * Adds http:// prefix to host name if @hostname hasn't it.
 * Writes default HTTP port (80) to @port if @hostname has no port part.
 *
 * Returns: 0 on success, -1 otherwise.
 **/
static int
get_host_and_port (const char *hostname, char **fullname, int *port)
{
	struct evhttp_uri *uri;

	if (! hostname)
		return -1;
	uri = evhttp_uri_parse (hostname);
	if (fullname) {
		const char *name = evhttp_uri_get_host (uri);
		if (! name) {
			size_t  len = strlen (hostname) + 8;
			char   *name_buf = malloc (len);

			if (uri)
				evhttp_uri_free (uri);
			snprintf (name_buf, len, "http://%s", hostname);
			uri = evhttp_uri_parse (name_buf);
			free (name_buf);
			name = evhttp_uri_get_host (uri);
		}
		*fullname = (name ? strdup (name) : NULL);
	}
	if (port) {
		*port = evhttp_uri_get_port (uri);
		if (*port <= 0)
			*port = 80;
	}
	if (uri)
		evhttp_uri_free (uri);
	return (! fullname || *fullname) ? 0 : -1;
}

/**
 * create_http_request:
 * @r: stream reader structure.
 * @cb: request callback.
 * @host: host to request.
 * @userdata: pointer to user data allocated in heap (may be NULL).
 *
 * Creates and initialises HTTP request structure.
 *
 * Returns: pointer to newly allocated request structure
 * or NULL on failure.
 **/
static HTTPRequest *
create_http_request (StateReader  *r,
                     void        (*cb) (struct evhttp_request *, void *),
                     const char   *host,
                     void         *userdata)
{
	HTTPRequest *hr = malloc (sizeof (*hr));
	if (hr) {
		char   *fullhostname;
		int     port;

		if (get_host_and_port (host, &fullhostname, &port) == 0) {
			hr->r = r;
			hr->userdata = userdata;
			hr->conn = evhttp_connection_base_new (r->base, r->dnsbase, fullhostname, port);
			if (hr->conn) {
				hr->req = evhttp_request_new (cb, hr);
				if (hr->req) {
					evhttp_add_header (evhttp_request_get_output_headers (hr->req),
							   "Host", host);
					evhttp_add_header (evhttp_request_get_output_headers (hr->req),
							   "User-Agent", PACKAGE_STRING);
					return hr;
				}
				evhttp_connection_free (hr->conn);
			}
			free (fullhostname);
		}
		free (hr);
	}
	free (userdata);
	return NULL;
}

/**
 * destroy_http_request:
 * @hr: HTTP request structure.
 *
 * Destroys @hr.
 **/
static void
destroy_http_request (HTTPRequest *hr)
{
	if (! hr)
		return;
	if (hr->conn)
		evhttp_connection_free (hr->conn);
	free (hr->userdata);
	free (hr);
}

/**
 * do_destroy_http_request:
 * @hr: HTTP request structure.
 *
 * Destroys @hr.
 * Delegates execution to destroy_http_request.
 **/
static void
do_destroy_http_request (evutil_socket_t sock, short what, void *hr)
{
	info (6, _("do_destroy_http_request\n"));
	destroy_http_request (hr);
}

/**
 * start_destroy_http_request:
 * @hr: HTTP request structure.
 *
 * Deferred @hr destroying.
 **/
static void
start_destroy_http_request (HTTPRequest *hr)
{
	if (! hr)
		return;
	info (6, _("start_destroy_http_request\n"));
	event_base_once (hr->r->base, -1, EV_TIMEOUT, do_destroy_http_request, hr, NULL);
}

/**
 * start_pending_requests:
 * @r: stream reader structure.
 * @mask: pending mask (bitwise OR combination of pending requests
 * that can be initiated).
 *
 * Starts pending requests.
 * Decryption key request becomes pending when authentication request is
 * in progress - this function unfreezes it.
 **/
static void
start_pending_requests (StateReader *r, int mask)
{
	assert (r);

	/* prevent key obtaining if cookie obtaining is in progress */
	if (r->obtaining & OBTAINING_AUTH)
		mask &= ~OBTAINING_KEY;

	if (r->pending & mask & OBTAINING_AUTH)
		start_get_auth_cookie (r);
	if (r->pending & mask & OBTAINING_FRAME)
		start_get_key_frame (r);
	if (r->pending & mask & OBTAINING_KEY)
		start_get_decryption_key (r);
	r->pending &= ~mask;
}

/**
 * clear_obtaining_flag:
 * @r: stream reader structure.
 * @flag: request type flag.
 *
 * Clears request type flag. Used in callbacks and indicates that request
 * process is finished.
 **/
static void
clear_obtaining_flag (StateReader *r, int flag)
{
	assert (r);

	r->stop_handling_reason &= ~flag;
	r->obtaining &= ~flag;
	r->pending &= ~flag;
}

/**
 * do_get_auth_cookie:
 * @req: libevent's evhttp_request.
 * @arg: HTTP request structure.
 *
 * start_get_auth_cookie callback.
 * Retrieves authentication cookie from the response.
 * Initiates pending decryption key request on success response only.
 * Initiates getting authentication cookie again on error response.
 *
 * For convenience sake, the cookie is never unencoded.
 **/
static void
do_get_auth_cookie (struct evhttp_request *req, void *arg)
{
	HTTPRequest *hr = arg;
	int          code;

	info (6, _("do_get_auth_cookie\n"));
	code = evhttp_request_get_response_code (req);
	start_destroy_http_request (hr);
	clear_obtaining_flag (hr->r, OBTAINING_AUTH);

	if (! is_valid_http_response_code (code))
		info (0, "%s: %s: %s %d\n", program_name,
		      _("login request failed"),
		      _("HTTP response code"), code);
	else {
		const char *header = evhttp_find_header (evhttp_request_get_input_headers (req),
		                                         "Set-Cookie");
		if (header)
			parse_cookie_hdr (&hr->r->cookie, header);
	}

	if (! is_valid_http_response_code (code))
		start_get_auth_cookie (hr->r);
	else if (hr->r->cookie) {
		info (2, _("Authentication cookie obtained\n"));
		start_pending_requests (hr->r, OBTAINING_KEY);
	} else
		info (0, "%s: %s\n", program_name,
		      _("login failed: check email and password in ~/.f1rc"));
	start_pending_requests (hr->r, OBTAINING_ALL & ~OBTAINING_KEY);
}

/**
 * start_get_auth_cookie:
 * @r: stream reader structure.
 *
 * Initiates obtaining the user's authentication cookie from the
 * Live Timing website by logging in with their e-mail address and password
 * and stealing out of the response headers in callback (do_get_auth_cookie).
 **/
void
start_get_auth_cookie (StateReader *r)
{
	HTTPRequest *hr;
	char        *email, *password;

	if (r->obtaining & OBTAINING_AUTH)
		return;

	info (1, _("Obtaining authentication cookie ...\n"));

	info (6, _("start_get_auth_cookie\n"));
	hr = create_http_request (r, do_get_auth_cookie, r->auth_host, NULL);
	if (! hr)
		return;
	evhttp_add_header (evhttp_request_get_output_headers (hr->req), "Content-Type",
			   "application/x-www-form-urlencoded");

	/* Encode the e-mail and password as a form */
	email = evhttp_encode_uri (r->email);
	password = evhttp_encode_uri (r->password);
	if (email && password)
		evbuffer_add_printf (evhttp_request_get_output_buffer (hr->req),
		                     "email=%s&password=%s", email, password);

	if (! email || ! password || evhttp_make_request (hr->conn, hr->req,
				                          EVHTTP_REQ_POST, LOGIN_URL)) {
		info (0, "%s: %s\n", program_name,
		      _("login request failed"));
		destroy_http_request (hr);
	} else {
		r->obtaining |= OBTAINING_AUTH;
	}
	free (password);
	free (email);
}

/**
 * parse_cookie_hdr:
 * @value: pointer to store allocated string.
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

	info (3, _("Got authentication cookie: %s\n"), *value);
}

/**
 * destroy_hr_result:
 * @res: HTTP request result.
 *
 * Destroys @res.
 * Frees @res->userdata.
 **/
static void
destroy_hr_result (HTTPRequestResult *res)
{
	if (! res)
		return;
	if (res->response)
		evbuffer_free (res->response);
	free (res->userdata);
	free (res);
}

/**
 * create_hr_result:
 * @hr: HTTP request.
 * @req: libevent's evhttp_request.
 *
 * Creates and initialises HTTP request result.
 *
 * Returns: pointer to newly allocated HTTP request result structure
 * or NULL on failure.
 **/
static HTTPRequestResult *
create_hr_result (HTTPRequest *hr, struct evhttp_request *req)
{
	HTTPRequestResult *res = malloc (sizeof (*res));

	if (! res)
		return NULL;

	res->r = hr->r;
	res->response = evbuffer_new ();
	evbuffer_add_buffer (res->response, evhttp_request_get_input_buffer (req));

	res->userdata = hr->userdata;
	hr->userdata = NULL;

	return res;
}

/**
 * parse_hr_result:
 * @number[out]: number to save parsing result.
 * @res[in]: HTTP request result structure.
 * @parser[in]: parse function.
 *
 * Parses HTTP response (@res->response) by @parser function
 * and writes result in @number.
 * @parser has following arguments: number to save (intermediate) result,
 * pointer to first symbol and symbols count. @parser may be called
 * next times if HTTP response doesn't stores in one contiguous chain.
 **/
static void
parse_hr_result (unsigned int       *number,
                 HTTPRequestResult  *res,
                 void              (*parser) (unsigned int *, const char *, size_t))
{
	unsigned int num = 0;

	while (evbuffer_get_contiguous_space (res->response)) {
		struct evbuffer_iovec chain;

		evbuffer_peek (res->response, -1, NULL, &chain, 1);
		(*parser) (&num, chain.iov_base, chain.iov_len);
		evbuffer_drain (res->response, chain.iov_len);
	}
	*number = num;
}

/**
 * do_get_decryption_key:
 * @req: libevent's evhttp_request.
 * @arg: HTTP request structure.
 *
 * start_get_decryption_key callback.
 * Retrieves decryption key from the response and calls
 * continue_pre_handle_stream on success response.
 **/
static void
do_get_decryption_key (struct evhttp_request *req, void *arg)
{
	HTTPRequest *hr = arg;
	int          code;
	int          success = 0;

	info (6, _("do_get_decryption_key\n"));
	code = evhttp_request_get_response_code (req);
	start_destroy_http_request (hr);
	clear_obtaining_flag (hr->r, OBTAINING_KEY);

	if (is_valid_http_response_code (code)) {
		HTTPRequestResult *res;

		info (2, _("Decryption key obtained\n"));
		res = create_hr_result (hr, req);
		if (res) {
			EventNumTypePair *userdata = res->userdata;

			if (userdata) {
				unsigned int decryption_key;

				parse_hr_result (&decryption_key, res, parse_hex_number);
				info (3, _("Got decryption key: %08x\n"), decryption_key);
				info (3, _("Begin new event #%d (type: %d)\n"),
				      userdata->no, userdata->type); //TODO: to other place

				write_decryption_key (decryption_key, hr->r, 1); //TODO: check errors
				continue_pre_handle_stream (hr->r);
				success = 1;
			}
			destroy_hr_result (res);
		}
	} else
		info (0, "%s: %s: %s %d\n", program_name,
		      _("key request failed"),
		      _("HTTP response code"), code);
	if (! success)
		hr->r->key_request_failure = 1;
	start_pending_requests (hr->r, OBTAINING_ALL);
}

/**
 * start_get_decryption_key:
 * @r: stream reader structure.
 *
 * Initiates obtaining the decryption key for the event
 * using the authorisation cookie of a registered user.
 * Decryption key will be obtained in callback (do_get_decryption_key).
 *
 * Cookie should be supplied already uri-encoded.
 **/
void
start_get_decryption_key (StateReader *r)
{
	HTTPRequest      *hr;
	EventNumTypePair *userdata;
	size_t            len;
	char             *url;

	if (r->obtaining & OBTAINING_KEY)
		return;
	if (r->obtaining & OBTAINING_AUTH) {
		r->pending |= OBTAINING_KEY;
		return;
	}
	if (! r->cookie)
		return;

	userdata = malloc (sizeof (*userdata));
	if (! userdata)
		return;
	userdata->no = r->new_event_no;
	userdata->type = r->new_event_type;

	info (6, _("start_get_decryption_key\n"));
	hr = create_http_request (r, do_get_decryption_key, r->host, userdata);
	if (! hr) {
		free (userdata);
		return;
	}

	info (1, _("Obtaining decryption key ...\n"));

	len = strlen (KEY_URL_BASE) + numlen (userdata->no) + strlen (r->cookie) + 11;
	url = malloc (len);
	if (url)
		snprintf (url, len,
		          "%s%u.asp?auth=%s", KEY_URL_BASE, userdata->no, r->cookie);

	r->key_request_failure = (! url) || evhttp_make_request (hr->conn, hr->req,
								 EVHTTP_REQ_GET, url);
	if (r->key_request_failure) {
		info (0, "%s: %s\n", program_name,
		      _("key request failed"));
		destroy_http_request (hr);
	} else {
		r->obtaining |= OBTAINING_KEY;
	}
	free (url);
}

/**
 * parse_hex_number:
 * @num: pointer to store result in.
 * @buf: buffer containing data received from server.
 * @len: length of buffer.
 *
 * Parses data received from the server in response to the key request,
 * filling the result while we can see hexadecimal digits.
 **/
static void
parse_hex_number (unsigned int *num,
		  const char   *buf,
		  size_t        len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if ((buf[i] >= '0') && (buf[i] <= '9')) {
			*num = (*num << 4) | (buf[i] - '0');
		} else if ((buf[i] >= 'a') && (buf[i] <= 'f')) {
			*num = (*num << 4) | (buf[i] - 'a' + 10);
		} else if ((buf[i] >= 'A') && (buf[i] <= 'F')) {
			*num = (*num << 4) | (buf[i] - 'A' + 10);
		} else {
			break;
		}
	}
}

/**
 * do_get_key_frame:
 * @req: libevent's evhttp_request.
 * @arg: HTTP request structure.
 *
 * start_get_key_frame callback.
 * Calls read_stream on success response
 * (inserts received key frame before input, then parses all input).
 **/
static void
do_get_key_frame (struct evhttp_request *req, void *arg)
{
	HTTPRequest *hr = arg;
	int          code;

	info (6, _("do_get_key_frame\n"));
	code = evhttp_request_get_response_code (req);
	start_destroy_http_request (hr);
	clear_obtaining_flag (hr->r, OBTAINING_FRAME);

	if (is_valid_http_response_code (code)) {
		hr->r->valid_frame = 1;
		info (2, _("Key frame #%u received\n"),
		      (unsigned int) (hr->userdata ? *((unsigned int *) hr->userdata) : 0));
		read_stream (hr->r, evhttp_request_get_input_buffer (req), 1);
	} else
		info (0, "%s: %s: %s %d\n", program_name,
		      _("key frame request failed"),
		      _("HTTP response code"), code);
	//TODO: try again on error ?
	start_pending_requests (hr->r, OBTAINING_ALL);
}

/**
 * start_get_key_frame:
 * @r: stream reader structure.
 *
 * Initiates obtaining the key frame from the website.
 * Key frame will be obtained in callback (do_get_key_frame).
 **/
void
start_get_key_frame (StateReader *r)
{
	HTTPRequest  *hr;
	size_t        len;
	char         *url;
	unsigned int *userdata;

	if (r->obtaining & OBTAINING_FRAME)
		return;

	userdata = malloc (sizeof (*userdata));
	if (! userdata)
		return;
	*userdata = r->new_frame;
	info (6, _("start_get_key_frame\n"));
	hr = create_http_request (r, do_get_key_frame, r->host, userdata);
	if (! hr) {
		free (userdata);
		return;
	}

	len = strlen (KEYFRAME_URL_PREFIX) +
	      (r->new_frame > 0 ? MAX (numlen (r->new_frame), 5) + 1 : 0) + 5;
	url = malloc (len);
	if (url) {
		if (r->new_frame > 0)
			snprintf (url, len, "%s_%05d.bin", KEYFRAME_URL_PREFIX, r->new_frame);
		else
			snprintf (url, len, "%s.bin", KEYFRAME_URL_PREFIX);
	}

	if (r->new_frame > 0)
		info (2, _("Obtaining key frame %d ...\n"), r->new_frame);
	else
		info (2, _("Obtaining current key frame ...\n"));

	if (! url || evhttp_make_request (hr->conn, hr->req,
	                                  EVHTTP_REQ_GET, url)) {
		info (0, "%s: %s\n", program_name,
		      _("key frame request failed"));
		destroy_http_request (hr);
	} else {
		r->obtaining |= OBTAINING_FRAME;
		r->stop_handling_reason |= OBTAINING_FRAME;
	}
	free (url);
}

/**
 * parse_dec_number:
 * @num: pointer to store result in.
 * @buf: buffer containing data received from server.
 * @len: length of buffer.
 *
 * Parses data received from the server,
 * filling the result while we can see decimal digits.
 *
 * Function is not used currently.
 **/
static void
parse_dec_number (unsigned int *num,
		  const char   *buf,
		  size_t        len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if ((buf[i] >= '0') && (buf[i] <= '9')) {
			*num *= 10;
			*num += buf[i] - '0';
		} else {
			break;
		}
	}
}
