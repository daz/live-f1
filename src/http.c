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


//TODO: description
typedef struct {
	StateReader              *r;
	struct evhttp_connection *conn;
	struct evhttp_request    *req;
	void                     *userdata;
} StateRequest;

//TODO: description
typedef struct {
	StateReader     *r;
	struct evbuffer *response;
	void            *userdata;
} StateRequestResult;

//TODO: description
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
 * create_state_request:
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
static StateRequest *
create_state_request (StateReader  *r,
                      void        (*cb) (struct evhttp_request *, void *),
                      const char   *host,
		      void         *userdata)
{
	StateRequest *sr = malloc (sizeof (*sr));
	if (sr) {
		char   *fullhostname;
		int     port;

		if (get_host_and_port (host, &fullhostname, &port) == 0) {
			sr->r = r;
			sr->userdata = userdata;
			sr->conn = evhttp_connection_base_new (r->base, r->dnsbase, fullhostname, port);
			if (sr->conn) {
				sr->req = evhttp_request_new (cb, sr);
				if (sr->req) {
					evhttp_add_header (evhttp_request_get_output_headers (sr->req),
							   "Host", host);
					evhttp_add_header (evhttp_request_get_output_headers (sr->req),
							   "User-Agent", PACKAGE_STRING);
					return sr;
				}
				evhttp_connection_free (sr->conn);
			}
			free (fullhostname);
		}
		free (sr);
	}
	free (userdata);
	return NULL;
}

/**
 * destroy_state_request:
 * @sr: HTTP request structure.
 *
 * Destroys @sr.
 **/
static void
destroy_state_request (StateRequest *sr)
{
	if (! sr)
		return;
	if (sr->conn)
		evhttp_connection_free (sr->conn);
	free (sr->userdata);
	free (sr);
}

/**
 * do_destroy_state_request:
 * @sr: HTTP request structure.
 *
 * Destroys @sr.
 * Delegates execution to destroy_state_request.
 **/
static void
do_destroy_state_request (evutil_socket_t sock, short what, void *sr)
{
	destroy_state_request (sr);
}

/**
 * start_destroy_state_request:
 * @sr: HTTP request structure.
 *
 * Deferred @sr destroying.
 **/
static void
start_destroy_state_request (StateRequest *sr)
{
	if (! sr)
		return;
	event_base_once (sr->r->base, -1, EV_TIMEOUT, do_destroy_state_request, sr, NULL);
}

/**
 * do_get_auth_cookie:
 * @req: libevent's evhttp_request.
 * @arg: HTTP request structure.
 *
 * start_get_auth_cookie callback.
 * Retrieves authentication cookie from the response
 * and calls start_getaddrinfo on success response.
 * Initiates getting authentication cookie again on error response.
 *
 * For convenience sake, the cookie is never unencoded.
 **/
static void
do_get_auth_cookie (struct evhttp_request *req, void *arg)
{
	StateRequest *sr = arg;
	int           code = evhttp_request_get_response_code (req);

	start_destroy_state_request (sr);
	sr->r->stop_handling_reason &= ~STOP_HANDLING_REASON_AUTH;

	if (! is_valid_http_response_code (code))
		fprintf (stderr, "%s: %s: %s %d\n", program_name,
			 _("login request failed"),
			 _("HTTP response code"), code);
	else {
		const char *header = evhttp_find_header (evhttp_request_get_input_headers (req),
		                                         "Set-Cookie");
		if (header)
			parse_cookie_hdr (&sr->r->cookie, header);
	}

	if (! is_valid_http_response_code (code))
		start_get_auth_cookie (sr->r);
	else if (sr->r->cookie) {
		info (2, _("Authentication cookie obtained\n"));
		start_getaddrinfo (sr->r);
	} else {
		fprintf (stderr, "%s: %s\n", program_name,
			 _("login failed: check email and password in ~/.f1rc"));
		start_loopexit (sr->r->base);
	}
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
	StateRequest *sr;
	char         *email, *password;

	if (r->stop_handling_reason & STOP_HANDLING_REASON_AUTH)
		return;

	info (1, _("Obtaining authentication cookie ...\n"));

	sr = create_state_request (r, do_get_auth_cookie, r->auth_host, NULL);
	if (! sr)
		return;
	evhttp_add_header (evhttp_request_get_output_headers (sr->req), "Content-Type",
			   "application/x-www-form-urlencoded");

	/* Encode the e-mail and password as a form */
	email = evhttp_encode_uri (r->email);
	password = evhttp_encode_uri (r->password);
	if (email && password)
		evbuffer_add_printf (evhttp_request_get_output_buffer (sr->req),
		                     "email=%s&password=%s", email, password);

	if (! email || ! password || evhttp_make_request (sr->conn, sr->req,
				                          EVHTTP_REQ_POST, LOGIN_URL)) {
		fprintf (stderr, "%s: %s\n", program_name,
			 _("login request failed"));
		destroy_state_request (sr);
	} else
		r->stop_handling_reason |= STOP_HANDLING_REASON_AUTH;
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
 * destroy_sr_result:
 * @res: HTTP request result.
 *
 * Destroys @res.
 * Frees @res->userdata.
 **/
static void
destroy_sr_result (StateRequestResult *res)
{
	if (! res)
		return;
	if (res->response)
		evbuffer_free (res->response);
	free (res->userdata);
	free (res);
}

/**
 * create_sr_result:
 * @sr: HTTP request.
 * @req: libevent's evhttp_request.
 *
 * Creates and initialises HTTP request result.
 *
 * Returns: pointer to newly allocated HTTP request result structure
 * or NULL on failure.
 **/
static StateRequestResult *
create_sr_result (StateRequest *sr, struct evhttp_request *req)
{
	StateRequestResult *res = malloc (sizeof (*res));

	if (! res)
		return NULL;

	res->r = sr->r;
	res->response = evbuffer_new ();
	evbuffer_add_buffer (res->response, evhttp_request_get_input_buffer (req));

	res->userdata = sr->userdata;
	sr->userdata = NULL;

	return res;
}

/**
 * parse_sr_result:
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
parse_sr_result (unsigned int        *number,
                 StateRequestResult  *res,
		 void               (*parser) (unsigned int *, const char *, size_t))
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

//TODO: description
static void
write_decryption_key (StateReader *r, unsigned int decryption_key)
{
	const Packet *oldp;
	Packet p;
	int i;

	if ((! r) || (! decryption_key))
		return;
	oldp = get_packet (&r->key_iter);
	if (! oldp)
		return;
	p = *oldp;
	p.data = 3; //TODO: without magic numbers
	p.len = sizeof (decryption_key);
	for (i = 0; i < p.len; ++i, decryption_key >>= 8)
		p.payload[i] = decryption_key & 0xff;
	write_packet (&r->key_iter, &p);
	info (3, _("Decryption key was saved to stream buffer (0x%02x%02x%02x%02x)\n"),
	      p.payload[0], p.payload[1], p.payload[2], p.payload[3]);
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
	StateRequest *sr = arg;
	int           code = evhttp_request_get_response_code (req);
	int           success = 0;

	start_destroy_state_request (sr);
/*	sr->r->decryption_obtaining = 0; */
	sr->r->stop_handling_reason &= ~STOP_HANDLING_REASON_KEY;

	if (is_valid_http_response_code (code)) {
		StateRequestResult *res;

		info (2, _("Decryption key obtained\n"));
		res = create_sr_result (sr, req);
		if (res) {
			EventNumTypePair *evnt = res->userdata;

			if (evnt) {
				unsigned int decryption_key;

				parse_sr_result (&decryption_key, res, parse_hex_number);
				info (3, _("Got decryption key: %08x\n"), decryption_key);
				info (3, _("Begin new event #%d (type: %d)\n"), evnt->no, evnt->type);

				write_decryption_key (sr->r, decryption_key);
				continue_pre_handle_stream (sr->r);
				success = 1;
			}
			destroy_sr_result (res);
		}
	} else
		fprintf (stderr, "%s: %s: %s %d\n", program_name,
			 _("key request failed"),
			 _("HTTP response code"), code);
	if (! success)
		sr->r->key_request_failure = 1;
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
	StateRequest     *sr;
	EventNumTypePair *evnt;
	size_t            len;
	char             *url;

/*	if (r->decryption_obtaining) */
	if (r->stop_handling_reason & STOP_HANDLING_REASON_KEY)
		return;

	info (1, _("Obtaining decryption key ...\n"));

	evnt = malloc (sizeof (*evnt));
	if (! evnt)
		return;
	evnt->no = r->new_event_no;
	evnt->type = r->new_event_type;

	sr = create_state_request (r, do_get_decryption_key, r->host, evnt);
	if (! sr)
		return;

	len = strlen (KEY_URL_BASE) + numlen (evnt->no) + strlen (r->cookie) + 11;
	url = malloc (len);
	if (url)
		snprintf (url, len,
		          "%s%u.asp?auth=%s", KEY_URL_BASE, evnt->no, r->cookie);

	if (! url || evhttp_make_request (sr->conn, sr->req,
				          EVHTTP_REQ_GET, url)) {
		fprintf (stderr, "%s: %s\n", program_name,
			 _("key request failed"));
		destroy_state_request (sr);
		r->key_request_failure = 1;
	} else {
/*		r->decryption_obtaining = 1; */
		r->stop_handling_reason |= STOP_HANDLING_REASON_KEY;
		r->key_request_failure = 0;
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
	StateRequest *sr = arg;
	int           code = evhttp_request_get_response_code (req);

	start_destroy_state_request (sr);
	sr->r->stop_handling_reason &= ~STOP_HANDLING_REASON_FRAME;

	if (is_valid_http_response_code (code)) {
		info (2, _("Key frame received\n"));
		sr->r->frame = sr->r->new_frame;
		read_stream (sr->r, evhttp_request_get_input_buffer (req), 1);
	} else
		fprintf (stderr, "%s: %s: %s %d\n", program_name,
			 _("key frame request failed"),
			 _("HTTP response code"), code);
	//TODO: try again on error ?
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
	StateRequest *sr;
	size_t        len;
	char         *url;

	if (r->stop_handling_reason & STOP_HANDLING_REASON_FRAME)
		return;

	if (r->new_frame > 0)
		info (2, _("Obtaining key frame %d ...\n"), r->new_frame);
	else
		info (2, _("Obtaining current key frame ...\n"));

	sr = create_state_request (r, do_get_key_frame, r->host, NULL);
	if (! sr)
		return;

	len = strlen (KEYFRAME_URL_PREFIX) +
	      (r->new_frame > 0 ? MAX (numlen (r->new_frame), 5) + 1 : 0) + 5;
	url = malloc (len);
	if (url) {
		if (r->new_frame > 0)
			snprintf (url, len, "%s_%05d.bin", KEYFRAME_URL_PREFIX, r->new_frame);
		else
			snprintf (url, len, "%s.bin", KEYFRAME_URL_PREFIX);
	}

	if (! url || evhttp_make_request (sr->conn, sr->req,
	                                  EVHTTP_REQ_GET, url)) {
		fprintf (stderr, "%s: %s\n", program_name,
			 _("key frame request failed"));
		destroy_state_request (sr);
	} else
		r->stop_handling_reason |= STOP_HANDLING_REASON_FRAME;
	free (url);

}

/**
 * do_get_total_laps:
 * @req: libevent's evhttp_request.
 * @arg: HTTP request structure.
 *
 * start_get_total_laps callback.
 * Retrieves total laps from the response, adds USER_SYS_TOTAL_LAPS packet
 * to the packet cache and calls continue_pre_handle_stream on
 * success response.
 **/
static void
do_get_total_laps (struct evhttp_request *req, void *arg)
{
	StateRequest *sr = arg;
	int           code = evhttp_request_get_response_code (req);

	start_destroy_state_request (sr);
	sr->r->stop_handling_reason &= ~STOP_HANDLING_REASON_TOTALLAPS;

	if (is_valid_http_response_code (code)) {
		StateRequestResult *res;

		info (2, _("Total laps obtained\n"));
		res = create_sr_result (sr, req);
		if (res) {
			unsigned int        total_laps;
			Packet              tlp;

			parse_sr_result (&total_laps, res, parse_dec_number);
			tlp.car = 0;
			tlp.type = USER_SYS_TOTAL_LAPS;
			tlp.data = total_laps;
			tlp.len = 0;
			tlp.at = sr->r->saving_time;
			push_packet (sr->r->encrypted_cnum, &tlp); //TODO: check errors
			info (3, _("Got total laps: %u\n"), total_laps);

			continue_pre_handle_stream (sr->r);

			destroy_sr_result (res);
		}
	} else
		fprintf (stderr, "%s: %s: %s %d\n", program_name,
			 _("total laps request failed"),
			 _("HTTP response code"), code);
	//TODO: try again on error ?
}

/**
 * start_get_total_laps:
 * @r: stream reader structure.
 *
 * Initiates obtaining the total number of laps for the race.
 * Total laps will be obtained in callback (do_get_total_laps).
 **/
void
start_get_total_laps (StateReader *r)
{
	StateRequest *sr;

	if (r->stop_handling_reason & STOP_HANDLING_REASON_TOTALLAPS)
		return;

	info (1, _("Obtaining total laps ...\n"));

	sr = create_state_request (r, do_get_total_laps, WEBSERVICE_HOST, NULL);
	if (! sr)
		return;

	if (evhttp_make_request (sr->conn, sr->req,
			         EVHTTP_REQ_GET, "/laps.php")) {
		fprintf (stderr, "%s: %s\n", program_name,
			 _("total laps request failed"));
		destroy_state_request (sr);
	} else
		r->stop_handling_reason |= STOP_HANDLING_REASON_TOTALLAPS;
}

/**
 * parse_dec_number:
 * @num: pointer to store result in.
 * @buf: buffer containing data received from server.
 * @len: length of buffer.
 *
 * Parses data received from the server in response to the total laps request,
 * filling the result while we can see decimal digits.
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
