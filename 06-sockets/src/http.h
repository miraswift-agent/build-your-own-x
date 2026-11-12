/*
 * Stage 65.6 -- HTTP/1.1 from the wire.
 *
 * The lesson: HTTP is text with rules, and the server is a state machine
 * over bytes.
 *
 * Stage 65.5 taught that a stream has no message boundaries: you get
 * bytes, not messages, and you have to decide for yourself where one
 * message ends and the next begins. The frame it built was binary --
 * a 4-byte length prefix -- because that is the easy shape to frame.
 * HTTP is the other shape: the boundaries are written into the text
 * itself, as CRLF, and a request is "a line, some lines, and a blank
 * line, and then however many bytes the headers say".
 *
 * So this stage is a text framer, and the interesting failures are all
 * in the rules:
 *
 *   - the request line is METHOD SP TARGET SP VERSION, and the version
 *     has to be HTTP/ or you do not have an HTTP request;
 *   - headers are "Name: value", and the name is case-insensitive;
 *   - Content-Length says how many body bytes follow, and if they have
 *     not arrived yet the request is not ready -- it is not an error;
 *   - HTTP/1.1 keeps the connection alive unless told otherwise;
 *     HTTP/1.0 does the opposite. Getting this backwards is the kind of
 *     bug that only shows up in a browser, minutes later, as a hang.
 *
 * Nothing here talks to the network on its own: the parser takes bytes
 * and gives back requests, and the server layer puts the two together.
 */

#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#define HTTP_MAX_METHOD    16
#define HTTP_MAX_TARGET   256
#define HTTP_MAX_VERSION   16
#define HTTP_MAX_HEADERS   32
#define HTTP_MAX_HNAME     64
#define HTTP_MAX_HVALUE   256
#define HTTP_MAX_BODY    4096
/* The largest request we are willing to hold: headers plus a body. */
#define HTTP_MAX_REQUEST 16384

/* One header line. The name is stored as it arrived; comparisons are
 * case-insensitive (see http_header_get). */
typedef struct {
    char name[HTTP_MAX_HNAME];
    char value[HTTP_MAX_HVALUE];
} http_header;

/* A parsed request. */
typedef struct {
    char       method[HTTP_MAX_METHOD];
    char       target[HTTP_MAX_TARGET];
    char       version[HTTP_MAX_VERSION];
    http_header headers[HTTP_MAX_HEADERS];
    int        nheaders;
    long       content_length;  /* -1 when the header is absent */
    char       body[HTTP_MAX_BODY];
    size_t     body_len;
} http_request;

/* The three answers a parser can give. NEED_MORE is not a failure: the
 * bytes have not all arrived yet, which is the normal state of a
 * connection that is still open. */
typedef enum {
    HTTP_PARSE_ERROR     = -1,
    HTTP_PARSE_NEED_MORE =  0,
    HTTP_PARSE_OK        =  1
} http_parse_status;

/* The parser's buffer: bytes received, not yet consumed. */
typedef struct {
    char   buf[HTTP_MAX_REQUEST];
    size_t len;
} http_parser;

void http_parser_init(http_parser *p);

/* Add bytes to the parser's buffer. Returns 0, or -1 with errno set to
 * ENOBUFS if the buffer cannot hold them. */
int http_parser_feed(http_parser *p, const void *data, size_t len);

/* Pull the next complete request.
 *
 * Returns HTTP_PARSE_OK with *out filled, HTTP_PARSE_NEED_MORE if the
 * request is not complete yet (and leaves the buffer alone), or
 * HTTP_PARSE_ERROR with errno set for a request that will never be
 * valid -- a broken request line, a header with no colon, a
 * Content-Length that is not a number. */
http_parse_status http_parser_next(http_parser *p, http_request *out);

/* Look up a header by name, case-insensitively. NULL if absent. */
const char *http_header_get(const http_request *r, const char *name);

/* Should this connection stay open after answering?
 *
 * HTTP/1.1 keeps it open by default; HTTP/1.0 closes it by default.
 * Either version can override with a Connection header. */
int http_request_keep_alive(const http_request *r);

/* Build a response into dst.
 *
 * A status line, Content-Type, Content-Length, Connection, a blank line,
 * and the body. Returns the number of bytes written, or -1 (with errno
 * set) if dst is too small. */
int http_response_build(char *dst, size_t dst_len, int status, const char *reason,
                        const char *content_type, const char *body,
                        size_t body_len, int keep_alive);

/* What a served connection did. */
typedef struct {
    unsigned long requests;
    unsigned long bytes_out;
} http_server_stats;

/* Serve HTTP requests on an already-accepted connection until the peer
 * closes or asks to close.
 *
 * The server answers "/" with root_body and everything else with 404.
 * Returns the number of requests served, or -1 on a socket error. */
int http_serve_connection(int fd, const char *root_body, http_server_stats *st);

#endif /* HTTP_H */
