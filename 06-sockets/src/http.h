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
#include <time.h>

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
 * Content-Length that is not a number.
 *
 * A body is framed by a length OR by a coding. When the request says
 * "Transfer-Encoding: chunked", the body is the chunks, and the bytes
 * after the terminal chunk are the start of the NEXT message.
 *
 * A request that carries BOTH a Transfer-Encoding and a Content-Length
 * is refused (RFC 9112 6.3 item 3): two framings are two answers to
 * "where does the body end", and that is the shape request smuggling is
 * made of. The coding that is on the wire frames the body when it is
 * the only framing there is -- that is the rule for a recipient that
 * has to process the message anyway (the client; an intermediary that
 * strips the length before forwarding). An origin server says no
 * instead.
 *
 * A request whose Transfer-Encoding names a FINAL coding other than
 * chunked is refused too (RFC 9112 6.3 item 4): the message body
 * length cannot be determined reliably, so the server MUST answer 400
 * and close. "gzip, chunked" is framed by chunks and is answered;
 * "chunked, gzip" is not, and is refused. The word "final" carries the
 * rule: the LAST coding in the list draws the boundary. */
http_parse_status http_parser_next(http_parser *p, http_request *out);

/* Look up a header by name, case-insensitively. NULL if absent. */
const char *http_header_get(const http_request *r, const char *name);

/* Should this connection stay open after answering?
 *
 * HTTP/1.1 keeps it open by default; HTTP/1.0 closes it by default.
 * Either version can override with a Connection header. */
int http_request_keep_alive(const http_request *r);

/* The Date header's value for a time, in IMF-fixdate form:
 * "Sun, 06 Nov 1994 08:49:37 GMT" (RFC 9110 5.6.7).
 *
 * The day and month names are written out by hand: strftime's %a and %b
 * are locale-dependent, and a header a cache parses must not be at the
 * mercy of the environment.
 *
 * Returns the number of characters written, or -1 (with errno set). */
int http_date_format(time_t when, char *out, size_t out_cap);

/* Build a response into dst.
 *
 * A status line, Date, Content-Type, Content-Length, Connection, a
 * blank line, and the body. Returns the number of bytes written, or -1
 * (with errno set) if dst is too small.
 *
 * The date is a parameter, not something the builder looks up: the
 * builder does not read a clock. An origin server MUST generate a Date
 * header field in all 2xx, 3xx, 4xx and 5xx responses (RFC 9110
 * 6.6.1), and the server that answers is the one that knows when it
 * answered. Pass the value http_date_format produced; NULL is refused,
 * because a response without a Date is not one this server sends. */
int http_response_build(char *dst, size_t dst_len, int status, const char *reason,
                        const char *content_type, const char *body,
                        size_t body_len, int keep_alive, const char *date);

/* What a served connection did. */
typedef struct {
    unsigned long requests;
    unsigned long bytes_out;
} http_server_stats;

/* Serve HTTP requests on an already-accepted connection until the peer
 * closes or asks to close.
 *
 * The server answers "/" with root_body, "/stream" with the chunked
 * demonstration, "/echo" with the request body, and everything else
 * with 404.
 * Returns the number of requests served, or -1 on a socket error. */
int http_serve_connection(int fd, const char *root_body, http_server_stats *st);

/* ---- Stage 65.16: routing by the Host header ----
 *
 * A server with one document does not need the Host header. A server
 * with several does: the Host names the site the request is for.
 *
 * A host name is case-insensitive (RFC 9110 4.2.3), and the port in
 * "example.com:8080" is not part of the name. */
typedef struct {
    const char *host;   /* the host name, without a port */
    const char *body;   /* the document served for that host */
} http_host_route;

/* Serve with a routing table: the Host header selects the document.
 *
 * A request whose Host matches a route is answered with that route's
 * document; a request with an unknown Host is answered with
 * default_body. A request with NO Host is answered 400: a server with
 * several sites cannot route a request that does not say which site it
 * is for.
 *
 * The path still routes: "/" serves the selected document, "/stream"
 * serves the chunked demonstration, "/echo" answers with the request
 * body, anything else is 404. */
int http_serve_connection_routed(int fd, const http_host_route *routes,
                                  size_t route_count, const char *default_body,
                                  http_server_stats *st);

/* ---- Stage 65.11: chunked transfer-encoding, the decoder ----
 *
 * The lesson of 65.5 was that a stream has no message boundaries: you
 * get bytes, not messages. Content-Length is one way to draw the
 * boundary; chunked transfer-encoding is the other, and it draws the
 * boundary INSIDE the body, as text, the same way HTTP draws it in the
 * headers.
 *
 * A chunked body is a sequence of chunks:
 *
 *     <hex size>[;extensions]CRLF <size bytes>CRLF ... 0CRLF [trailers]CRLF
 *
 * The size is hexadecimal. The zero chunk ends the body, and what
 * follows it may be trailers, ended by a blank line.
 *
 * The decoder is fed bytes and hands back one chunk at a time, so a
 * chunk that arrives in pieces is not an error -- it is the normal
 * state of an open connection. */
#define HTTP_MAX_CHUNKED 16384

typedef enum {
    HTTP_CHUNK_ERROR     = -1,  /* the stream can never be valid */
    HTTP_CHUNK_NEED_MORE =  0,  /* more bytes are needed; not a failure */
    HTTP_CHUNK_OK        =  1,  /* one chunk was decoded */
    HTTP_CHUNK_DONE      =  2   /* the terminal chunk was seen */
} http_chunk_status;

typedef struct {
    char   buf[HTTP_MAX_CHUNKED];  /* bytes fed, not yet decoded */
    size_t len;                    /* bytes in buf */
    size_t pos;                    /* cursor: bytes already decoded */
    int    done;                   /* the terminal chunk has been seen */
} http_chunk_decoder;

void http_chunk_decoder_init(http_chunk_decoder *d);

/* The bytes that arrived after the terminal chunk: the start of the next
 * message on the same connection. *out points into the decoder's buffer
 * and is valid until the next feed. */
size_t http_chunk_decoder_pending(const http_chunk_decoder *d, const char **out);

/* Add bytes to the decoder. Returns 0, or -1 with errno set to ENOBUFS
 * if the buffer cannot hold them (the decoded prefix is dropped first,
 * so room is made as the caller goes). */
int http_chunk_decoder_feed(http_chunk_decoder *d, const void *data, size_t len);

/* Pull the next chunk.
 *
 * Returns HTTP_CHUNK_OK with *out_len set to the chunk's size (the
 * payload is copied into out), HTTP_CHUNK_NEED_MORE if the chunk has
 * not fully arrived yet, HTTP_CHUNK_DONE once the terminal chunk has
 * been seen (and *out_len is 0), or HTTP_CHUNK_ERROR with errno set for
 * a stream that can never be valid -- a size that is not hexadecimal, a
 * missing CRLF after a chunk's payload, or a chunk too large for out. */
http_chunk_status http_chunk_decoder_next(http_chunk_decoder *d, char *out,
                                           size_t out_cap, size_t *out_len);


/* ---- Stage 65.12: chunked transfer-encoding, the encoder ----
 *
 * The mirror of the decoder. Instead of pulling one chunk at a time out
 * of a stream, the encoder pushes one chunk at a time into one: each
 * call frames exactly one chunk -- the size line, the payload, the
 * CRLF -- so the caller can put the bytes on the wire immediately.
 * Nothing is buffered, because the whole point of chunked
 * transfer-encoding is that the length is not known until the body is
 * finished. */

/* A chunk size line: up to 16 hex digits for a 64-bit size, plus CRLF. */
#define HTTP_MAX_CHUNK_LINE 32

/* Frame one chunk: "<hex size>\r\n<payload>\r\n".
 *
 * Returns the number of bytes written, or -1 with errno set: EINVAL if
 * len is zero (a zero-length chunk IS the terminal chunk, which is
 * http_chunk_encoder_finish's job), ENOSPC if the buffer is too
 * small. */
int http_chunk_encode(const void *data, size_t len, char *out, size_t out_cap);

/* The terminal chunk, "0\r\n\r\n". Returns the number of bytes
 * written, or -1 with errno set to ENOSPC. */
int http_chunk_encoder_finish(char *out, size_t out_cap);

/* Build the header block of a chunked response.
 *
 * The same status line, Content-Type and Connection as
 * http_response_build, but with Transfer-Encoding: chunked and NO
 * Content-Length -- the length of a chunked body is not known until the
 * body is finished, and that is the only honest reason to choose this
 * framing. Returns the number of bytes written, or -1 (with errno set)
 * if dst is too small. */
int http_response_build_chunked(char *dst, size_t dst_len, int status,
                                const char *reason, const char *content_type,
                                int keep_alive, const char *date);


#endif /* HTTP_H */
