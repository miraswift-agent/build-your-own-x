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

/* Stage 65.20: the largest piece of body one chunk may carry.
 *
 * A chunk is a piece of the body, not the body -- the point of chunked
 * framing is that the body arrives in pieces and its total length is
 * not known until the last piece has gone. The limit bounds the buffer
 * one call needs; it is not a limit on the body. */
#define HTTP_CLIENT_MAX_CHUNK  4096

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

/* Stage 65.20: the head of a request whose body length is not known yet.
 *
 * The same request line, Host, User-Agent and Connection as
 * http_client_request_build_ex -- but with "Transfer-Encoding: chunked"
 * and NO Content-Length. A length we do not have would be a lie, and
 * chunked is the framing that says "I will show you where the body ends
 * when it ends". The head is written first; the body follows as chunks.
 *
 * Returns the number of bytes written, or -1 with errno set to EINVAL
 * for a bad argument or ENOBUFS when dst is too small. */
int http_client_request_build_chunked_head(char *dst, size_t dst_len,
                                           const char *method, const char *target,
                                           const char *host, int keep_alive);

/* Stage 65.28: the largest number of requests that may be in flight
 * on one connection. Each one is a response the client is waiting
 * for, and each one may be a HEAD response -- so each one needs its
 * own "there is no body" flag. */
#define HTTP_CLIENT_MAX_INFLIGHT 16

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

    /* Stage 65.28: 1 when the response being parsed answers a HEAD
     * request, and so has no body at all.
     *
     * RFC 9112 6.3 item 1: "Any response to a HEAD request ... is
     * always terminated by the first empty line after the header
     * fields, regardless of the header fields present in the message,
     * and thus cannot contain a message body or trailer section."
     *
     * The flag is set by the connection layer, which knows what it
     * asked for; the parser consumes it when a response completes. */
    int         no_body;
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
 * never be valid.
 *
 * Stage 65.24: a response whose Transfer-Encoding names a transfer
 * coding this client cannot apply is an error, and the reason is
 * ENOTSUP -- "not implemented", the same reason the server uses for a
 * request it cannot frame (Stage 65.23). The codings are applied in
 * turn, so a body that still carries one we cannot undo is not a body
 * this client can deliver, and out.body means "the body".
 *
 * The refusal happens in the head: the client does not wait for a body
 * it has already decided it cannot read.
 *
 * Stage 65.25: a response whose Content-Length cannot be USED is
 * refused too (RFC 9112 6.3 item 5) -- the message framing is
 * invalid. The reason travels out in errno: EBADMSG, so the
 * connection layer can do what the RFC says a user agent must do --
 * close the connection to the server and discard the response.
 *
 * On the error path the output is NOT filled in -- a caller must not
 * read a response the parser refused. (The server's request parser
 * zeroes its output; this one does not.) */
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
    /* Stage 65.20: 1 while a chunked request's head is on the wire and
     * its body has not been finished. A chunk without a head, or a
     * second request in the middle of a chunked body, would put two
     * messages on the wire at once -- so the connection refuses. */
    int         chunked_open;

    /* Stage 65.28: the requests in flight, in order.
     *
     * A response to a HEAD request carries no body, and the client must
     * not wait for one -- so it has to know which response it is
     * reading. The responses come back in the order the requests were
     * written, so the expectations are a queue: 1 means "this one is a
     * HEAD response, there is no body". */
    int         in_flight[HTTP_CLIENT_MAX_INFLIGHT];
    int         n_inflight;
} http_client_connection;

/* Open a connection to a dotted-quad address. Returns 0, or -1 with
 * errno set. */
int http_client_connection_open(http_client_connection *c, const char *host, uint16_t port);

/* Write a request on the connection, WITHOUT reading the response.
 *
 * This is the half that makes pipelining possible: two requests can go
 * out on the socket before either response comes back. The bytes are
 * written now; nothing is read. Returns 0, or -1 with errno set.
 *
 * Stage 65.28: the method is remembered, because the response to a
 * HEAD request has no body and the client must not wait for one. The
 * requests in flight are a queue, in the order they were written. */
int http_client_connection_write_request(http_client_connection *c, const char *method,
                                         const char *target, const char *body,
                                         size_t body_len);

/* Stage 65.20: write the head of a chunked request.
 *
 * The head goes out first, saying "Transfer-Encoding: chunked" and
 * naming no length -- because the length is not known yet. That is the
 * whole reason to choose this framing: not that the body is big, but
 * that its size is not known until it is done. The body then follows as
 * chunks (http_client_connection_write_chunk) and ends with the
 * terminal chunk (http_client_connection_write_chunked_end).
 *
 * The connection asks to keep-alive, like the other request path.
 * Returns 0, or -1 with errno set. */
int http_client_connection_write_chunked_head(http_client_connection *c,
                                              const char *method, const char *target);

/* Stage 65.20: write one chunk of the body.
 *
 * The chunk is framed by the Stage 65.12 encoder -- "<hex size>\r\n
 * <payload>\r\n" -- so the bytes on the wire are the bytes every other
 * implementation writes. A chunk of zero bytes is refused with EINVAL:
 * a zero-length chunk IS the terminal chunk, and writing one here would
 * end the body by accident. The terminal chunk has its own call.
 *
 * Returns 0, or -1 with errno set (EINVAL for a zero-length chunk or a
 * chunk written before the head, ENOSPC for a chunk larger than
 * HTTP_CLIENT_MAX_CHUNK). */
int http_client_connection_write_chunk(http_client_connection *c,
                                       const void *data, size_t len);

/* Stage 65.20: write the terminal chunk -- "0\r\n\r\n".
 *
 * This is the only way to end a chunked body, and it is what makes the
 * bytes after it the start of the next message. Returns 0, or -1 with
 * errno set. */
int http_client_connection_write_chunked_end(http_client_connection *c);

/* Read the next response from the connection.
 *
 * The parser is asked before the socket: the bytes of the next response
 * may already be in hand (Stage 65.14a's leftovers). Returns 0 with
 * *out filled, or -1 with errno set.
 *
 * Stage 65.25: a response whose framing is invalid (a Content-Length
 * that cannot be used) closes the connection -- RFC 9112 6.3 item 5:
 * "If it is in a response message received by a user agent, the user
 * agent MUST close the connection to the server and discard the
 * received response." A connection whose framing is untrustworthy
 * cannot be trusted with a second message. The reason survives the
 * close: it is the evidence of what happened.
 *
 * Stage 65.28: the response to a HEAD request is read without waiting
 * for a body: RFC 9112 6.3 item 1 says such a response "is always
 * terminated by the first empty line after the header fields". The
 * Content-Length in that head is a claim about the body the matching
 * GET would have returned, and the bytes are not coming. */
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
