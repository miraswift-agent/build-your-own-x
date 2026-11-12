/*
 * Stage 65.7 -- HTTP/1.1 client from the wire.
 *
 * The lesson: the client has to know how the body ends, and there are two
 * ways to know.
 *
 * The server (Stage 65.6) frames a response with Content-Length: it says
 * how many bytes follow, so the reader knows where the message stops. A
 * client cannot assume that. A response with no Content-Length is framed
 * by the close of the connection -- the body is whatever arrived before
 * the peer hung up. Both framings are legal HTTP, and a client that
 * handles only one of them has a bug that shows up as a truncated page (a
 * body silently cut short) or as a hang (waiting for bytes that will never
 * come).
 *
 * So the client gets a third answer, alongside NEED_MORE and ERROR:
 * FINISH. When the peer closes, an EOF-framed response is complete.
 *
 * The client side is where framing bugs bite first, because the client
 * has to decide what to do with the bytes it has already read: stop
 * reading (the body is complete), keep reading (it is not), or give up
 * (the response is broken).
 *
 * No curl, no libraries: the request is text we assemble, the response is
 * text we parse, and the wire between them is a socket.
 */

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "http.h"

#define HTTP_CLIENT_MAX_REASON   64
#define HTTP_CLIENT_MAX_BODY   4096
#define HTTP_CLIENT_MAX_REQUEST 8192

/* A parsed response. */
typedef struct {
    char        version[HTTP_MAX_VERSION];
    int         status;
    char        reason[HTTP_CLIENT_MAX_REASON];
    http_header headers[HTTP_MAX_HEADERS];
    int         nheaders;
    long        content_length;   /* -1 when the header is absent */
    char        body[HTTP_CLIENT_MAX_BODY + 1];
    size_t      body_len;
    int         framed_by_length; /* 1 = Content-Length, 0 = EOF or chunked */
    int         framed_by_chunked;/* 1 = Transfer-Encoding: chunked */
} http_response;

/* Build a request into dst.
 *
 * "METHOD SP TARGET SP HTTP/1.1 CRLF", a Host header, a User-Agent, a
 * Connection header, and the body (with its Content-Length) if there is
 * one. Returns the number of bytes written, or -1 with errno set to
 * EINVAL for a bad argument or ENOBUFS when dst is too small. */
int http_client_request_build(char *dst, size_t dst_len,
                              const char *method, const char *target,
                              const char *host, const char *body, size_t body_len);

/* The same request, with the connection's fate stated in the header.
 *
 * keep_alive != 0 writes "Connection: keep-alive"; keep_alive == 0
 * writes "Connection: close". HTTP/1.1 keeps a connection open by
 * default, but saying so is worth the bytes: the header is how the
 * peer learns that a second request is coming. */
int http_client_request_build_ex(char *dst, size_t dst_len,
                                 const char *method, const char *target,
                                 const char *host, const char *body, size_t body_len,
                                 int keep_alive);

/* The parser's buffer: bytes received, not yet consumed. */
typedef struct {
    char   buf[HTTP_CLIENT_MAX_REQUEST];
    size_t len;

    /* Stage 65.13: the chunked framing. The head stays in buf; the body
     * bytes are handed to the Stage 65.11 decoder as they arrive, and
     * the decoded body accumulates here (the output is a view of it). */
    http_chunk_decoder chunker;
    int         body_started;   /* the head has been parsed; body_start is valid */
    size_t      body_start;     /* where the body begins in buf */
    size_t      body_fed_upto;  /* body bytes already given to the decoder */
    size_t      decoded_len;    /* decoded body bytes so far */
    char        decoded[HTTP_CLIENT_MAX_BODY + 1];
} http_response_parser;

void http_response_parser_init(http_response_parser *p);

/* The bytes the parser is holding for the NEXT message on this
 * connection (0 when it is empty).
 *
 * A connection carries messages back to back, so a response can end
 * before the bytes that follow it have been read. The parser keeps
 * those bytes: they are the start of the next response, and throwing
 * them away would lose a message the peer has already sent. */
size_t http_response_parser_pending(const http_response_parser *p);

/* Add bytes to the parser's buffer. Returns 0, or -1 with errno set to
 * ENOBUFS if the buffer cannot hold them. */
int http_response_parser_feed(http_response_parser *p, const void *data, size_t len);

/* Pull the next complete response.
 *
 * HTTP_PARSE_OK when the response is complete, HTTP_PARSE_NEED_MORE when
 * it is not (and the peer has not closed), HTTP_PARSE_ERROR when it will
 * never be valid. */
http_parse_status http_response_parser_next(http_response_parser *p, http_response *out);

/* The peer closed: complete an EOF-framed response.
 *
 * A response that carried a Content-Length and ended early is truncated,
 * and that is an error, not a short body. */
http_parse_status http_response_parser_finish(http_response_parser *p, http_response *out);

/* Look up a header by name, case-insensitively. NULL if absent. */
const char *http_response_header_get(const http_response *r, const char *name);

/* A connection: a socket, a parser, and the address it is talking to.
 *
 * A connection carries a conversation, not a message. The object holds
 * the socket so that a second request can be written on it after the
 * first response has been read -- and the parser, because the bytes of
 * the next response may already have arrived in the same read as the
 * end of this one. */
typedef struct {
    int         fd;
    uint16_t    port;
    char        host[64];
    http_response_parser parser;
} http_client_connection;

/* Open a connection to a dotted-quad address. Returns 0, or -1 with
 * errno set. */
int http_client_connection_open(http_client_connection *c, const char *host, uint16_t port);

/* Write a request on the connection, WITHOUT reading the response.
 *
 * This is the half that makes pipelining possible: two requests can go
 * out on the socket before either response comes back. The bytes are
 * written now; nothing is read. Returns 0, or -1 with errno set. */
int http_client_connection_write_request(http_client_connection *c, const char *method,
                                         const char *target, const char *body,
                                         size_t body_len);

/* Read the next response from the connection.
 *
 * The parser is asked before the socket: the bytes of the next response
 * may already be in hand (Stage 65.14a's leftovers). Returns 0 with
 * *out filled, or -1 with errno set. */
int http_client_connection_read_response(http_client_connection *c, http_response *out);

/* Write a request on the connection and read the response that answers
 * it. The request asks to keep the connection alive, so a second call
 * writes a second request on the same socket. This is write-then-read,
 * one turn at a time -- pipelining is the two halves used separately.
 * Returns 0 with *out filled, or -1 with errno set. */
int http_client_connection_request(http_client_connection *c, const char *method,
                                   const char *target, const char *body, size_t body_len,
                                   http_response *out);

/* Close the connection. Safe to call twice. */
void http_client_connection_close(http_client_connection *c);

/* One request and one response over TCP.
 *
 * host is a dotted-quad address -- a name needs DNS, which is Stage 65.8.
 * Returns 0 with *out filled, or -1 with errno set. */
int http_client_get(const char *host, uint16_t port, const char *target,
                    http_response *out);

#endif /* HTTP_CLIENT_H */
