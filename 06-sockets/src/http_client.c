/*
 * Stage 65.7 -- HTTP/1.1 client from the wire.
 *
 * The response parser here is a deliberate mirror of the request parser
 * in http.c: same boundaries, same rules, opposite direction. They are
 * two copies for now, and the seam between them is the thing to close
 * later -- a client that speaks a slightly different dialect of HTTP
 * than the server it talks to is exactly the bug that takes an afternoon
 * to find and an afternoon to fix.
 *
 * The boundary rules, restated, because they are where the bugs live:
 *
 *   - the status line is "HTTP/1.1 SP code SP reason", and the reason
 *     may be empty;
 *   - the blank line is "\r\n\r\n", which *starts* at the last header's
 *     own CRLF -- so the headers run to blank+2, and the body starts at
 *     blank+4;
 *   - with no headers at all the blank line is the status line's own
 *     CRLF plus an empty one, so the header region is empty and the
 *     body starts four bytes in;
 *   - a body is framed by Content-Length, or by the close of the
 *     connection, and those are the only two framings.
 */

/* _DEFAULT_SOURCE exposes clock_gettime and CLOCK_MONOTONIC under
 * -std=c11. */
#define _DEFAULT_SOURCE

#include "http_client.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>

#include "sockets.h"
#include "coding.h"
#include "content_length.h"

/* ---------- small helpers ---------- */

/* The offset of the first "\r\n" in buf, or (size_t)-1. */
static size_t find_crlf(const char *buf, size_t len)
{
    size_t i;
    for (i = 0; i + 1 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n') {
            return i;
        }
    }
    return (size_t)-1;
}

/* The offset of the first "\r\n\r\n" in buf, or (size_t)-1. */
static size_t find_blank(const char *buf, size_t len)
{
    size_t i;
    for (i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return i;
        }
    }
    return (size_t)-1;
}

/* The offset of the first ch in buf[0..len), or (size_t)-1. */
static size_t find_char(const char *buf, size_t len, char ch)
{
    size_t i;
    for (i = 0; i < len; i++) {
        if (buf[i] == ch) {
            return i;
        }
    }
    return (size_t)-1;
}

/* ---------- the status line ---------- */

static int parse_status_line(const char *line, size_t len, http_response *out)
{
    size_t sp;
    size_t code_len;
    size_t rlen;
    long code;

    sp = find_char(line, len, ' ');
    if (sp == (size_t)-1 || sp == 0 || sp >= HTTP_MAX_VERSION) {
        return -1;
    }
    memcpy(out->version, line, sp);
    out->version[sp] = '\0';
    if (strncmp(out->version, "HTTP/", 5) != 0) {
        return -1;
    }

    /* The code is three digits. Not two, not four: three. */
    sp++;
    code_len = 0;
    while (sp + code_len < len && line[sp + code_len] >= '0' && line[sp + code_len] <= '9') {
        code_len++;
    }
    if (code_len != 3) {
        return -1;
    }
    code = (long)(line[sp] - '0') * 100L +
           (long)(line[sp + 1] - '0') * 10L +
           (long)(line[sp + 2] - '0');
    out->status = (int)code;

    /* The reason phrase is whatever is left, after one space. It may be
     * empty -- "HTTP/1.1 204" is a legal status line. */
    sp += code_len;
    rlen = 0;
    if (sp < len) {
        if (line[sp] != ' ') {
            return -1;
        }
        sp++;
        rlen = len - sp;
        if (rlen >= HTTP_CLIENT_MAX_REASON) {
            rlen = HTTP_CLIENT_MAX_REASON - 1;
        }
        memcpy(out->reason, line + sp, rlen);
    }
    out->reason[rlen] = '\0';
    return 0;
}

/* ---------- the headers ---------- */

static int parse_headers(char *buf, size_t start, size_t end, http_response *out)
{
    size_t hp = start;

    out->nheaders = 0;
    while (hp < end) {
        size_t crlf;
        size_t linelen;
        size_t colon;
        size_t name_len;
        size_t value_start;
        size_t value_len;

        crlf = find_crlf(buf + hp, end - hp);
        if (crlf == (size_t)-1) {
            return -1;
        }
        linelen = crlf;
        if (linelen == 0) {
            return -1;
        }
        colon = find_char(buf + hp, linelen, ':');
        if (colon == (size_t)-1 || colon == 0) {
            return -1;
        }
        name_len = colon;
        value_start = hp + colon + 1;
        value_len = linelen - colon - 1;

        /* The value is trimmed of the spaces that follow the colon. */
        while (value_len > 0 && isspace((unsigned char)buf[value_start])) {
            value_start++;
            value_len--;
        }
        while (value_len > 0 && isspace((unsigned char)buf[value_start + value_len - 1])) {
            value_len--;
        }
        if (name_len >= HTTP_MAX_HNAME || value_len >= HTTP_MAX_HVALUE) {
            return -1;
        }
        if (out->nheaders >= HTTP_MAX_HEADERS) {
            return -1;
        }
        memcpy(out->headers[out->nheaders].name, buf + hp, name_len);
        out->headers[out->nheaders].name[name_len] = '\0';
        memcpy(out->headers[out->nheaders].value, buf + value_start, value_len);
        out->headers[out->nheaders].value[value_len] = '\0';
        out->nheaders++;
        hp += crlf + 2;
    }
    return 0;
}

const char *http_response_header_get(const http_response *r, const char *name)
{
    int i;
    for (i = 0; i < r->nheaders; i++) {
        if (strcasecmp(r->headers[i].name, name) == 0) {
            return r->headers[i].value;
        }
    }
    return NULL;
}

/* The Content-Length, or -1 when the header is absent.
 *
 * A Content-Length that cannot be used is not a missing header: the
 * framing is invalid (RFC 9112 6.3 item 5).
 *
 * Stage 65.25: the value is read by the shared reader, so the list
 * exception applies -- "5, 5" names one value, 5, and the message is
 * processed with it -- and the two roles cannot drift apart again.
 * (They had: the server's strtol accepted a sign, this side's digit
 * loop did not.)
 *
 * Stage 65.26: the reader reads the COMBINED field value, so a
 * response that carries the field more than once is read by all of its
 * field lines (RFC 9110 5.2). Two lines naming 5 are one value; two
 * lines naming 5 and 6 are an invalid framing -- and taking the first
 * line instead (what we used to do) is a wrong accept, and the
 * response-splitting shape. */
static int header_content_length(const http_response *r, long *out)
{
    int st = http_content_length_collect(r->headers, r->nheaders, out);

    if (st == HTTP_CL_ABSENT) {
        *out = -1;
        return 0;
    }
    if (st != HTTP_CL_OK) {
        return -1;
    }
    if (*out > 100000000L) {
        return -1;               /* bigger than this client will hold */
    }
    return 0;
}

/* The transfer codings this client understands.
 *
 * Chunked is the only one -- the same set the server has, for the same
 * reason: RFC 9112 6.1 says "A recipient MUST be able to parse the
 * chunked transfer coding" and says nothing about the others, and we
 * have no decoder for any other coding.
 *
 * A coding this client cannot apply is a coding it cannot UNDO: the
 * codings are applied in turn, so a body that still carries one of them
 * is not the body the caller asked for. The set is the client's own --
 * what a program can apply is a property of the program. */
static int client_coding_is_understood(const char *coding, size_t len)
{
    return http_coding_is_chunked(coding, len);
}

/* What the response's Transfer-Encoding header says, read once.
 *
 * The list is read by the shared scan in coding.h -- the same one the
 * server uses, so the two roles cannot drift apart. (They had: the
 * client was still using the old scan, the text after the last comma,
 * so it read "Transfer-Encoding: chunked," as "no coding" and handed
 * back the raw chunk framing as the body.) */
static void response_coding_scan(const http_response *r, http_coding_scan *out)
{
    http_coding_scan_list(http_response_header_get(r, "Transfer-Encoding"),
                          client_coding_is_understood, out);
}

/* ---------- the head: status line, headers, and where the body starts ---------- */

/* Stage 65.31: does the STATUS alone say that this message has no body?
 *
 * RFC 9112 6.3 item 1 -- the first item of the precedence list:
 *
 *   "Any response to a HEAD request and any response with a 1xx
 *   (Informational), 204 (No Content), or 304 (Not Modified) status code
 *   is always terminated by the first empty line after the header fields,
 *   regardless of the header fields present in the message, and thus
 *   cannot contain a message body or trailer section."
 *
 * The HEAD half of that sentence needs the conversation -- only the
 * connection knows it asked for HEAD (Stage 65.28). These three are
 * different: the status line carries the answer, so the parser can see
 * it. The rule does not care where the knowledge comes from, only that
 * it is there.
 *
 * "Regardless of the header fields present in the message" is the load
 * bearing half: a 204 that carries "Content-Length: 5" still has no
 * body, and a 204 that says "Transfer-Encoding: chunked" is not
 * chunk-decoded. */
static int status_has_no_body(int status)
{
    return status == 204 || status == 304 || (status >= 100 && status < 200);
}

/* Stage 65.31: an interim response is a complete message that is not
 * the answer to the request -- the final response follows it. */
static int status_is_interim(int status)
{
    return status >= 100 && status < 200;
}

static http_parse_status parse_head(http_response_parser *p, http_response *out,
                                    size_t *body_start)
{
    size_t line_end;
    size_t blank;
    size_t hdr_start;
    size_t hdr_end;
    long cl;
    int bodyless;

    if (p->len == 0) {
        return HTTP_PARSE_NEED_MORE;
    }
    line_end = find_crlf(p->buf, p->len);
    if (line_end == (size_t)-1) {
        return HTTP_PARSE_NEED_MORE;
    }
    if (parse_status_line(p->buf, line_end, out) != 0) {
        errno = EPROTO;
        return HTTP_PARSE_ERROR;
    }
    /* Stage 65.31: the status may already have decided that this
     * message has no body at all. */
    bodyless = status_has_no_body(out->status);
    blank = find_blank(p->buf, p->len);
    if (blank == (size_t)-1) {
        return HTTP_PARSE_NEED_MORE;
    }
    if (blank < line_end) {
        errno = EPROTO;
        return HTTP_PARSE_ERROR;
    }
    /* The blank line starts at the last header's own CRLF, so the headers
     * run to blank+2 and the body starts at blank+4. With no headers the
     * blank is the status line's CRLF plus an empty one, which makes the
     * header region empty and puts the body four bytes in. */
    hdr_start = line_end + 2;
    hdr_end = blank + 2;
    *body_start = blank + 4;
    if (parse_headers(p->buf, hdr_start, hdr_end, out) != 0) {
        errno = EPROTO;
        return HTTP_PARSE_ERROR;
    }
    if (header_content_length(out, &cl) != 0) {
        /* Stage 65.25: the framing is invalid (RFC 9112 6.3 item 5).
         * The reason travels out in errno: EBADMSG, so the connection
         * layer can do what the RFC says a user agent must do -- close
         * the connection to the server and discard the response.
         *
         * Stage 65.28: but item 1 comes FIRST in the precedence list. A
         * response to a HEAD request "is always terminated by the first
         * empty line after the header fields, regardless of the header
         * fields present in the message" -- so a length that cannot be
         * used does not make the message invalid: there is no body for
         * it to be invalid about. The length is still reported when it
         * can be read, because it is a claim about the body the
         * matching GET would have returned.
         *
         * Stage 65.31: the same precedence holds for a 1xx, 204 or 304
         * -- the status decides, and the status is in the message. */
        if (!p->no_body && !bodyless) {
            errno = EBADMSG;
            return HTTP_PARSE_ERROR;
        }
        cl = -1;
    }
    out->content_length = cl;
    {
        http_coding_scan cs;

        response_coding_scan(out, &cs);
        if (cs.has_unknown) {
            /* Stage 65.24: the response names a transfer coding this
             * client cannot apply.
             *
             * The mirror of Stage 65.23, on the other side of the
             * wire. The server could refuse such a REQUEST with 501 --
             * it chooses not to send a body it cannot frame. The client
             * has no such choice: the body is already on the wire, and
             * the codings are applied in turn, so the bytes that arrive
             * still carry a coding we cannot undo. out.body means "the
             * body", and a body this client cannot decode is not one.
             *
             * The refusal happens HERE, in the head: the client does
             * not wait for a body it has already decided it cannot
             * read. The reason travels out in errno -- ENOTSUP, the
             * same reason the server used, because it is the same
             * reason: "not implemented".
             *
             * Stage 65.31: but a message that cannot contain a body at
             * all has nothing to decode. A 204 that names gzip is
             * bodyless "regardless of the header fields present in the
             * message", so the coding is meaningless and the refusal
             * has nothing to act on. */
            if (!bodyless) {
                errno = ENOTSUP;
                return HTTP_PARSE_ERROR;
            }
        }
        out->framed_by_chunked = cs.final_is_chunked;
    }
    if (out->framed_by_chunked) {
        /* Transfer-Encoding overrides Content-Length. A message with
         * both is the classic request-smuggling shape, and the safe
         * reading is the one that trusts the framing that is actually
         * on the wire: the transfer coding. */
        out->content_length = -1;
    }
    return HTTP_PARSE_OK;
}

/* ---------- the chunked body ---------- */

/* The body is chunked: the bytes go to the Stage 65.11 decoder, and the
 * decoded body accumulates in the parser -- the caller's output is a
 * view of it, so a body that arrives in pieces is still one body. */
static http_parse_status chunked_next(http_response_parser *p, http_response *out,
                                      size_t body_start)
{
    char chunk[HTTP_CLIENT_MAX_BODY];
    size_t chunk_len;
    http_chunk_status cst;

    if (!p->body_started) {
        p->body_start = body_start;
        p->body_fed_upto = body_start;
        p->decoded_len = 0;
        http_chunk_decoder_init(&p->chunker);
        p->body_started = 1;
    }
    if (p->len > p->body_fed_upto) {
        if (http_chunk_decoder_feed(&p->chunker, p->buf + p->body_fed_upto,
                                    p->len - p->body_fed_upto) != 0) {
            errno = ENOBUFS;
            return HTTP_PARSE_ERROR;
        }
        /* The body bytes now live in the decoder; only the head stays
         * in the parser's buffer, so a long body does not fill it. */
        p->len = body_start;
        p->body_fed_upto = body_start;
    }
    for (;;) {
        cst = http_chunk_decoder_next(&p->chunker, chunk, sizeof(chunk), &chunk_len);
        if (cst == HTTP_CHUNK_OK) {
            if (p->decoded_len + chunk_len > HTTP_CLIENT_MAX_BODY) {
                errno = ENOBUFS;
                return HTTP_PARSE_ERROR;
            }
            memcpy(p->decoded + p->decoded_len, chunk, chunk_len);
            p->decoded_len += chunk_len;
            continue;
        }
        if (cst == HTTP_CHUNK_NEED_MORE) {
            return HTTP_PARSE_NEED_MORE;
        }
        if (cst == HTTP_CHUNK_ERROR) {
            return HTTP_PARSE_ERROR;
        }
        /* The terminal chunk: the body is complete. What arrived after
         * it is the start of the NEXT message on this connection, so it
         * is kept -- the bytes live in the decoder, not in the parser's
         * buffer, because the decoder consumed the framing. */
        memcpy(out->body, p->decoded, p->decoded_len);
        out->body[p->decoded_len] = '\0';
        out->body_len = p->decoded_len;
        out->framed_by_length = 0;
        out->framed_by_chunked = 1;
        {
            const char *pend = NULL;
            size_t pending = http_chunk_decoder_pending(&p->chunker, &pend);
            if (pending > 0) {
                memcpy(p->buf, pend, pending);
            }
            p->len = pending;
        }
        http_chunk_decoder_init(&p->chunker);
        p->body_started = 0;
        p->body_fed_upto = 0;
        p->decoded_len = 0;
        return HTTP_PARSE_OK;
    }
}

/* ---------- the parser ---------- */

size_t http_response_parser_pending(const http_response_parser *p)
{
    if (p == NULL) {
        return 0;
    }
    return p->len;
}

void http_response_parser_init(http_response_parser *p)
{
    p->len = 0;
    p->body_started = 0;
    p->body_start = 0;
    p->body_fed_upto = 0;
    p->decoded_len = 0;
    p->no_body = 0;
    http_chunk_decoder_init(&p->chunker);
}

int http_response_parser_feed(http_response_parser *p, const void *data, size_t len)
{
    if (p == NULL || (data == NULL && len > 0)) {
        errno = EINVAL;
        return -1;
    }
    if (p->len + len > sizeof(p->buf)) {
        errno = ENOBUFS;
        return -1;
    }
    memcpy(p->buf + p->len, data, len);
    p->len += len;
    return 0;
}

http_parse_status http_response_parser_next(http_response_parser *p, http_response *out)
{
    size_t body_start = 0;
    http_parse_status st;
    size_t cl;

    st = parse_head(p, out, &body_start);
    if (st != HTTP_PARSE_OK) {
        return st;
    }
    if (status_has_no_body(out->status)) {
        /* Stage 65.31: the status decides.
         *
         * A 1xx, 204 or 304 response "is always terminated by the first
         * empty line after the header fields, regardless of the header
         * fields present in the message, and thus cannot contain a
         * message body or trailer section" (RFC 9112 6.3 item 1).
         *
         * So a length in the head is not a promise -- the bytes are not
         * coming -- and a coding is not a framing: there is no body to
         * frame. What follows the head, if anything, is the start of
         * the NEXT message (for a 1xx, the final response).
         *
         * This branch comes BEFORE the HEAD branch: an interim response
         * does not consume the HEAD expectation, because it does not
         * answer the request -- the final response does. */
        out->body_len = 0;
        out->framed_by_length = 0;
        out->framed_by_chunked = 0;
        memmove(p->buf, p->buf + body_start, p->len - body_start);
        p->len -= body_start;
        return HTTP_PARSE_OK;
    }
    if (p->no_body) {
        /* Stage 65.28: the response to a HEAD request ends at the end of
         * the head.
         *
         * RFC 9112 6.3 item 1: "Any response to a HEAD request ... is
         * always terminated by the first empty line after the header
         * fields, regardless of the header fields present in the
         * message, and thus cannot contain a message body or trailer
         * section."
         *
         * So the Content-Length in the head is NOT a promise about this
         * message: it describes the body the matching GET would have
         * returned (RFC 9110 8.6). The bytes are not coming, and a
         * client that waits for them sits on the connection until the
         * peer times it out.
         *
         * What follows the head, if anything, is the start of the NEXT
         * message -- and a server that sent content anyway broke the
         * rule, so those bytes are kept rather than taken as a body
         * ("A client SHOULD NOT interpret the response to a HEAD
         * request as containing content", RFC 9110 9.3.2). */
        out->body_len = 0;
        out->framed_by_length = 0;
        memmove(p->buf, p->buf + body_start, p->len - body_start);
        p->len -= body_start;
        p->no_body = 0;
        return HTTP_PARSE_OK;
    }
    if (out->framed_by_chunked) {
        return chunked_next(p, out, body_start);
    }
    cl = (size_t)out->content_length;
    if (out->content_length < 0) {
        /* No Content-Length: the body is framed by the close of the
         * connection. Until the peer hangs up, the response is not
         * complete -- and it is not broken either. */
        out->framed_by_length = 0;
        out->body_len = 0;
        return HTTP_PARSE_NEED_MORE;
    }
    if (cl > HTTP_CLIENT_MAX_BODY) {
        errno = ENOBUFS;
        return HTTP_PARSE_ERROR;
    }
    if (p->len < body_start + cl) {
        return HTTP_PARSE_NEED_MORE;
    }
    memcpy(out->body, p->buf + body_start, cl);
    out->body[cl] = '\0';
    out->body_len = cl;
    out->framed_by_length = 1;
    {
        /* What follows the body is the start of the next message on this
         * connection: keep it. */
        size_t consumed = body_start + cl;
        memmove(p->buf, p->buf + consumed, p->len - consumed);
        p->len -= consumed;
    }
    return HTTP_PARSE_OK;
}

http_parse_status http_response_parser_finish(http_response_parser *p, http_response *out)
{
    size_t body_start = 0;
    http_parse_status st;
    size_t blen;

    st = parse_head(p, out, &body_start);
    if (st == HTTP_PARSE_NEED_MORE) {
        if (p->len == 0) {
            /* The peer closed and we never got a status line. That is
             * not a short response; there was no response. */
            errno = EPROTO;
            return HTTP_PARSE_ERROR;
        }
        /* Stage 65.27: the peer closed in the MIDDLE of the header
         * section. The message is incomplete: "If a response
         * terminates in the middle of the header section (before the
         * empty line is received) and the status code might rely on
         * header fields to convey the full meaning of the response,
         * then the client cannot assume that meaning has been conveyed"
         * (RFC 9112 8). */
        errno = HTTP_REASON_INCOMPLETE;
        return HTTP_PARSE_ERROR;
    }
    if (st != HTTP_PARSE_OK) {
        return st;
    }
    if (status_has_no_body(out->status)) {
        /* Stage 65.31: the peer closed after a 1xx, 204 or 304. That
         * response ended at the end of its head (RFC 9112 6.3 item 1),
         * so it is COMPLETE -- the close is the peer hanging up on a
         * conversation it is done with, not a message that stopped
         * early. */
        out->body_len = 0;
        out->framed_by_length = 0;
        out->framed_by_chunked = 0;
        p->len = 0;
        return HTTP_PARSE_OK;
    }
    if (p->no_body) {
        /* Stage 65.28: the peer closed after a response to a HEAD
         * request. That response ended at the end of its head (RFC 9112
         * 6.3 item 1), so it is COMPLETE -- the close is the peer
         * hanging up on a conversation it is done with, not a message
         * that stopped early. */
        out->body_len = 0;
        out->framed_by_length = 0;
        p->len = 0;
        p->no_body = 0;
        return HTTP_PARSE_OK;
    }
    if (out->framed_by_chunked) {
        /* The peer closed before the terminal chunk. A chunked body
         * that stops is truncated, not short -- and "a message body
         * that uses the chunked transfer coding is incomplete if the
         * zero-sized chunk that terminates the encoding has not been
         * received" (RFC 9112 8). */
        errno = HTTP_REASON_INCOMPLETE;
        return HTTP_PARSE_ERROR;
    }
    if (out->content_length >= 0) {
        /* The peer closed before the promised octets arrived. "A
         * message that uses a valid Content-Length is incomplete if the
         * size of the message body received (in octets) is less than
         * the value given by Content-Length" (RFC 9112 8); and the
         * recipient "MUST consider the message to be incomplete and
         * close the connection" (RFC 9112 6.3 item 6).
         *
         * The reason is its own: not EPROTO (the bytes that arrived
         * were well formed) and not EBADMSG (the length was valid --
         * the octets did not arrive). */
        errno = HTTP_REASON_INCOMPLETE;
        return HTTP_PARSE_ERROR;
    }
    blen = p->len - body_start;
    if (blen > HTTP_CLIENT_MAX_BODY) {
        errno = ENOBUFS;
        return HTTP_PARSE_ERROR;
    }
    memcpy(out->body, p->buf + body_start, blen);
    out->body[blen] = '\0';
    out->body_len = blen;
    out->framed_by_length = 0;
    p->len = 0;
    return HTTP_PARSE_OK;
}

/* ---------- the request ---------- */

/* Write every byte, or fail.
 *
 * A short write is not an error: it means the socket took what it had
 * room for. A write that moves NOTHING is an error -- the peer is gone
 * (EPIPE) -- and treating it as success would leave the request
 * half-written with nobody to notice. */
static int write_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            errno = EPIPE;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

int http_client_request_build(char *dst, size_t dst_len,
                              const char *method, const char *target,
                              const char *host, const char *body, size_t body_len)
{
    return http_client_request_build_ex(dst, dst_len, method, target, host,
                                        body, body_len, 0);
}

/* Stage 65.35: the head, on its own.
 *
 * The head is what goes on the wire first in the expect flow: it says
 * how long the body will be, and it carries the expectation. So it is
 * built here, and the whole-request builder below delegates to it and
 * appends the body -- ONE reader for the head.
 *
 * The header checks are the 65.34 ones, unchanged: a header value that
 * carries a CR or an LF ends the line and starts another one, and a
 * name carrying a colon is not a name. */
int http_client_request_build_head(char *dst, size_t dst_len,
                                   const char *method, const char *target,
                                   const char *host, long content_length,
                                   int keep_alive,
                                   const char *hdr_name, const char *hdr_value)
{
    int n;
    size_t need;

    if (dst == NULL || method == NULL || target == NULL || host == NULL) {
        errno = EINVAL;
        return -1;
    }
    if ((hdr_name == NULL) != (hdr_value == NULL)) {
        errno = EINVAL;
        return -1;
    }
    if (hdr_name != NULL) {
        /* A header value that carries a CR or an LF ends the line and
         * starts another one: the request would then say two things
         * about itself, and a request that says two things about where
         * the body ends is the shape request smuggling is made of
         * (RFC 9112 6.3 item 3). A name carrying a colon is not a
         * name. An empty name or an empty value is not a header. */
        if (hdr_name[0] == '\0' || hdr_value[0] == '\0' ||
            strchr(hdr_name, '\r') || strchr(hdr_name, '\n') ||
            strchr(hdr_name, ':') || strchr(hdr_name, ' ') ||
            strchr(hdr_value, '\r') || strchr(hdr_value, '\n')) {
            errno = EINVAL;
            return -1;
        }
    }
    need = strlen(method) + strlen(target) + strlen(host) + 128;
    if (hdr_name != NULL) {
        need += strlen(hdr_name) + strlen(hdr_value) + 4;
    }
    if (dst_len < need) {
        errno = ENOBUFS;
        return -1;
    }
    n = snprintf(dst, dst_len,
                 "%s %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: mira-http/1.0\r\n"
                 "Connection: %s\r\n",
                 method, target, host, keep_alive ? "keep-alive" : "close");
    if (n < 0 || (size_t)n >= dst_len) {
        errno = ENOBUFS;
        return -1;
    }
    if (hdr_name != NULL) {
        int m = snprintf(dst + n, dst_len - (size_t)n,
                         "%s: %s\r\n", hdr_name, hdr_value);
        if (m < 0 || (size_t)(n + m) >= dst_len) {
            errno = ENOBUFS;
            return -1;
        }
        n += m;
    }
    if (content_length >= 0) {
        int m = snprintf(dst + n, dst_len - (size_t)n,
                         "Content-Length: %ld\r\n", content_length);
        if (m < 0 || (size_t)(n + m) >= dst_len) {
            errno = ENOBUFS;
            return -1;
        }
        n += m;
    }
    {
        int m = snprintf(dst + n, dst_len - (size_t)n, "\r\n");
        if (m < 0 || (size_t)(n + m) >= dst_len) {
            errno = ENOBUFS;
            return -1;
        }
        n += m;
    }
    return n;
}

/* The whole request: the head, then the body.
 *
 * Stage 65.35: this delegates to the head builder above and appends
 * the body, so the head bytes are the same whether a request is built
 * whole or in two parts (the expect flow needs the head alone). */
int http_client_request_build_with_header(char *dst, size_t dst_len,
                                           const char *method, const char *target,
                                           const char *host, const char *body,
                                           size_t body_len, int keep_alive,
                                           const char *hdr_name, const char *hdr_value)
{
    int n;
    size_t need;

    if (dst == NULL || method == NULL || target == NULL || host == NULL ||
        (body == NULL && body_len > 0)) {
        errno = EINVAL;
        return -1;
    }
    if ((hdr_name == NULL) != (hdr_value == NULL)) {
        errno = EINVAL;
        return -1;
    }
    if (body_len > (size_t)LONG_MAX) {
        errno = EINVAL;
        return -1;
    }
    need = strlen(method) + strlen(target) + strlen(host) + 128;
    if (hdr_name != NULL) {
        need += strlen(hdr_name) + strlen(hdr_value) + 4;
    }
    if (body_len > 0) {
        need += body_len + 32;
    }
    if (dst_len < need) {
        errno = ENOBUFS;
        return -1;
    }
    n = http_client_request_build_head(dst, dst_len, method, target, host,
                                       body_len > 0 ? (long)body_len : -1,
                                       keep_alive, hdr_name, hdr_value);
    if (n < 0) {
        return -1;
    }
    if (body_len > 0) {
        if (dst_len < (size_t)n + body_len) {
            errno = ENOBUFS;
            return -1;
        }
        memcpy(dst + n, body, body_len);
        n += (int)body_len;
    }
    return n;
}

/* Stage 65.34: the plain request is the one with no extra header, so
 * there is one reader for the head and not two. */
int http_client_request_build_ex(char *dst, size_t dst_len,
                                 const char *method, const char *target,
                                 const char *host, const char *body, size_t body_len,
                                 int keep_alive)
{
    return http_client_request_build_with_header(dst, dst_len, method, target,
                                                 host, body, body_len,
                                                 keep_alive, NULL, NULL);
}

/* Stage 65.20: the head of a request whose body length is not known.
 *
 * The difference from http_client_request_build_ex is one header and
 * one omission: "Transfer-Encoding: chunked" instead of a length.
 * The body does not follow here -- it follows as chunks, because the
 * whole point is that it is not all in hand yet. */
int http_client_request_build_chunked_head(char *dst, size_t dst_len,
                                           const char *method, const char *target,
                                           const char *host, int keep_alive)
{
    int n;
    size_t need;

    if (dst == NULL || method == NULL || target == NULL || host == NULL) {
        errno = EINVAL;
        return -1;
    }
    need = strlen(method) + strlen(target) + strlen(host) + 160;
    if (dst_len < need) {
        errno = ENOBUFS;
        return -1;
    }
    n = snprintf(dst, dst_len,
                 "%s %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: mira-http/1.0\r\n"
                 "Connection: %s\r\n"
                 "Transfer-Encoding: chunked\r\n"
                 "\r\n",
                 method, target, host, keep_alive ? "keep-alive" : "close");
    if (n < 0 || (size_t)n >= dst_len) {
        errno = ENOBUFS;
        return -1;
    }
    return n;
}

/* ---------- the address, octet by octet ---------- */

/* The port bytes come from the helper that already knows how to spell
 * them; the address is ours, octet by octet. */
static int client_addr(const char *host, uint16_t port, struct sockaddr_in *addr)
{
    uint8_t ip[4];
    sock_ipv4_bytes b;

    if (host == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (sock_ipv4_from_dotted(host, ip) != 0) {
        return -1;
    }
    if (sock_ipv4_loopback_bytes(port, &b) != 0) {
        return -1;
    }
    memcpy(b.ip, ip, 4);
    return sock_addr_apply(&b, addr);
}

/* ---------- the connection: a socket that carries a conversation ---------- */

void http_client_connection_init(http_client_connection *c)
{
    if (c == NULL) {
        return;
    }
    memset(c, 0, sizeof(*c));
    c->fd = -1;
    http_response_parser_init(&c->parser);
}

int http_client_connection_open(http_client_connection *c, const char *host, uint16_t port)
{
    struct sockaddr_in addr;
    size_t hostlen;

    if (c == NULL || host == NULL) {
        errno = EINVAL;
        return -1;
    }
    hostlen = strlen(host);
    if (hostlen == 0 || hostlen >= sizeof(c->host)) {
        errno = EINVAL;
        return -1;
    }
    if (client_addr(host, port, &addr) != 0) {
        return -1;
    }
    c->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (c->fd < 0) {
        return -1;
    }
    if (connect(c->fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        int e = errno;
        close(c->fd);
        c->fd = -1;
        errno = e;
        return -1;
    }
    memcpy(c->host, host, hostlen + 1);
    c->port = port;
    c->chunked_open = 0;
    c->n_inflight = 0;
    c->body_pending = 0;
    c->interims = 0;
    http_response_parser_init(&c->parser);
    return 0;
}

void http_client_connection_close(http_client_connection *c)
{
    if (c == NULL) {
        return;
    }
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
}

int http_client_connection_write_request(http_client_connection *c, const char *method,
                                         const char *target, const char *body,
                                         size_t body_len)
{
    char req[HTTP_CLIENT_MAX_REQUEST];
    int reqlen;

    if (c == NULL || c->fd < 0 || method == NULL || target == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (c->chunked_open) {
        /* A chunked body is still open. A request written now would
         * land in the middle of that body, and the peer would read it
         * as part of the body it is still being sent. */
        errno = EINVAL;
        return -1;
    }
    if (c->n_inflight >= HTTP_CLIENT_MAX_INFLIGHT) {
        /* Too many requests are in flight: the client could no longer
         * tell which response is the one without a body. Refuse
         * BEFORE the request goes out. */
        errno = ENOBUFS;
        return -1;
    }
    reqlen = http_client_request_build_ex(req, sizeof(req), method, target, c->host,
                                          body, body_len, 1);
    if (reqlen < 0) {
        return -1;
    }
    if (write_all(c->fd, req, (size_t)reqlen) != 0) {
        return -1;
    }
    /* Stage 65.28: remember what was asked for. The response to a HEAD
     * request has no body, and the client must not wait for one. The
     * method token is case-sensitive (RFC 9110 9.1), so "head" is not
     * HEAD and its response may carry a body. */
    c->in_flight[c->n_inflight++] = (strcmp(method, "HEAD") == 0) ? 1 : 0;
    return 0;
}

/* Stage 65.20: the client sends a chunked request body.
 *
 * The shape of the API is the lesson: the head goes out first, the body
 * follows in pieces as they become available, and the terminal chunk
 * says the body is done. A body whose length is already known should
 * carry Content-Length instead -- chunked is for a length that is not
 * known until the body ends, not for a body that happens to be large. */

int http_client_connection_write_chunked_head(http_client_connection *c,
                                              const char *method, const char *target)
{
    char req[HTTP_CLIENT_MAX_REQUEST];
    int reqlen;

    if (c == NULL || c->fd < 0 || method == NULL || target == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (c->chunked_open) {
        /* The body of the last request has not been finished. */
        errno = EINVAL;
        return -1;
    }
    if (c->n_inflight >= HTTP_CLIENT_MAX_INFLIGHT) {
        errno = ENOBUFS;
        return -1;
    }
    reqlen = http_client_request_build_chunked_head(req, sizeof(req), method, target,
                                                     c->host, 1);
    if (reqlen < 0) {
        return -1;
    }
    if (write_all(c->fd, req, (size_t)reqlen) != 0) {
        return -1;
    }
    c->chunked_open = 1;
    /* Stage 65.28: the same expectation as the other request path --
     * a response to a HEAD request carries no body. */
    c->in_flight[c->n_inflight++] = (strcmp(method, "HEAD") == 0) ? 1 : 0;
    return 0;
}

int http_client_connection_write_chunk(http_client_connection *c,
                                       const void *data, size_t len)
{
    char buf[HTTP_MAX_CHUNK_LINE + HTTP_CLIENT_MAX_CHUNK + 2];
    int n;

    if (c == NULL || c->fd < 0 || data == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!c->chunked_open) {
        /* A chunk before the head: the peer would see a size where a
         * request line belongs. */
        errno = EINVAL;
        return -1;
    }
    if (len == 0) {
        /* A zero-length chunk IS the terminal chunk. */
        errno = EINVAL;
        return -1;
    }
    if (len > HTTP_CLIENT_MAX_CHUNK) {
        errno = ENOSPC;
        return -1;
    }
    n = http_chunk_encode(data, len, buf, sizeof(buf));
    if (n < 0) {
        return -1;
    }
    return write_all(c->fd, buf, (size_t)n);
}

int http_client_connection_write_chunked_end(http_client_connection *c)
{
    char buf[8];
    int n;

    if (c == NULL || c->fd < 0) {
        errno = EINVAL;
        return -1;
    }
    if (!c->chunked_open) {
        errno = EINVAL;
        return -1;
    }
    n = http_chunk_encoder_finish(buf, sizeof(buf));
    if (n < 0) {
        return -1;
    }
    if (write_all(c->fd, buf, (size_t)n) != 0) {
        return -1;
    }
    c->chunked_open = 0;
    return 0;
}

/* Stage 65.35: the clock the wait is measured on.
 *
 * Monotonic, so a wall-clock jump cannot make the wait longer or
 * shorter than it was asked for. */
static long monotonic_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + (long)(ts.tv_nsec / 1000000L);
}

/* Read ONE message off the connection.
 *
 * ONE reader for the bytes: the parser is asked before the socket (the
 * bytes of the next message may already be in hand -- Stage 65.14a's
 * leftovers), and a complete message comes back with *out filled.
 *
 * The interim is NOT skipped here: what an interim MEANS depends on
 * who is asking. The final-answer reader skips it (Stage 65.31: it is
 * not the answer to the request); the expect wait hands it to the
 * caller (it IS the answer to the expectation). Skipping it is a
 * decision, and this function does not make it.
 *
 * deadline_ms < 0 means "no deadline": wait as long as the socket
 * takes, which is the ordinary path. A deadline bounds the wait, and
 * a wait that runs out is ETIMEDOUT -- an OUTCOME, not a broken
 * connection: the socket is still good and the body still has to go.
 *
 * *interims counts the interim messages already read for the request
 * being answered. It lives in the CONNECTION, not in the call: the
 * expect flow reads the 100 in one call and the final response in
 * another (Stage 65.35), and a peer that closes between them is
 * still a peer that closed after an interim (Stage 65.31). The
 * knowledge lives with the conversation, not with the bytes.
 *
 * Returns 0 with *out filled, 1 when the message was completed by the
 * peer's close (an EOF-framed final response), or -1 with errno set. */
static int client_read_message(http_client_connection *c, http_response *out,
                                long deadline_ms)
{
    char buf[4096];
    http_parse_status st;

    /* Stage 65.28: which response is this one?
     *
     * A response to a HEAD request carries no body (RFC 9112 6.3 item
     * 1), so the parser has to be told before it reads -- otherwise it
     * waits for octets that are not coming. The requests were written
     * in order and the responses come back in that order, so the front
     * of the queue is this response. */
    c->parser.no_body = (c->n_inflight > 0) ? c->in_flight[0] : 0;
    for (;;) {
        st = http_response_parser_next(&c->parser, out);
        if (st == HTTP_PARSE_OK) {
            return 0;
        }
        if (st == HTTP_PARSE_ERROR) {
            /* Stage 65.25: the user agent's half of RFC 9112 6.3 item
             * 5 -- "If it is in a response message received by a
             * user agent, the user agent MUST close the connection to
             * the server and discard the received response."
             *
             * The close is the claim: a connection whose framing is
             * untrustworthy cannot be trusted with a second message.
             * The reason survives the close -- it is the evidence of
             * what happened. */
            if (errno == EBADMSG) {
                int saved = errno;
                http_client_connection_close(c);
                errno = saved;
            }
            return -1;
        }
        /* NEED_MORE: the message is not here yet. The bytes of the NEXT
         * response may already be in the parser -- Stage 65.14a's
         * leftovers -- but the parser only says so by handing us the
         * next one, so read only when the parser has nothing complete. */
        if (deadline_ms >= 0) {
            long remaining = deadline_ms - monotonic_ms();
            int ready;

            if (remaining <= 0) {
                errno = ETIMEDOUT;
                return -1;
            }
            ready = sock_wait_readable(c->fd, (int)remaining);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return -1;
            }
            if (ready == 0) {
                errno = ETIMEDOUT;
                return -1;
            }
        }
        {
            ssize_t n = read(c->fd, buf, sizeof(buf));
            if (n > 0) {
                if (http_response_parser_feed(&c->parser, buf, (size_t)n) != 0) {
                    return -1;
                }
                continue;
            }
            if (n == 0) {
                /* The peer hung up. */
                st = http_response_parser_finish(&c->parser, out);
                if (st == HTTP_PARSE_OK) {
                    return 1;
                }
                if (c->interims > 0) {
                    /* Stage 65.31: the peer closed after an interim
                     * response. The final response never came, so the
                     * request is unanswered -- and the reason is the
                     * same one a truncated body has: the message did
                     * not arrive. Not EPROTO ("there was no response"):
                     * there was one, and it was not the answer. */
                    errno = HTTP_REASON_INCOMPLETE;
                }
                if (errno == HTTP_REASON_INCOMPLETE) {
                    /* Stage 65.27: RFC 9112 6.3 item 6 -- "If the
                     * sender closes the connection or the recipient
                     * times out before the indicated number of octets
                     * are received, the recipient MUST consider the
                     * message to be incomplete and close the
                     * connection."
                     *
                     * The close is the claim: a connection whose
                     * message did not finish cannot be trusted with
                     * another one. The reason survives the close --
                     * it is the evidence of what happened. */
                    int saved = errno;
                    http_client_connection_close(c);
                    errno = saved;
                }
                return -1;
            }
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
    }
}

int http_client_connection_read_response(http_client_connection *c, http_response *out)
{
    if (c == NULL || c->fd < 0 || out == NULL) {
        errno = EINVAL;
        return -1;
    }
    for (;;) {
        int rc = client_read_message(c, out, -1);

        if (rc < 0) {
            return -1;
        }
        if (rc == 1) {
            /* The peer's close completed the message (an EOF-framed
             * final response). */
            return 0;
        }
        if (status_is_interim(out->status)) {
            /* Stage 65.31: an interim response is a complete
             * message, but it is not the answer to the request --
             * the final response follows it. RFC 9112 15.2: "A
             * client MUST be able to parse one or more 1xx
             * responses received prior to a final response, even if
             * the client does not expect one."
             *
             * So the request stays in flight: the interim does not
             * answer it, and the expectation (a HEAD response has
             * no body) belongs to the final one. */
            c->interims++;
            continue;
        }
        /* The final response answers the request at the front of
         * the queue -- and only then is the queue advanced. */
        if (c->n_inflight > 0) {
            memmove(c->in_flight, c->in_flight + 1,
                    (size_t)(c->n_inflight - 1) * sizeof(c->in_flight[0]));
            c->n_inflight--;
        }
        /* The conversation with this request is over: the next one
         * starts with no interims behind it. */
        c->interims = 0;
        return 0;
    }
}

/* Stage 65.35: write the HEAD of a request, and nothing else.
 *
 * The head says how long the body will be and -- when expect_continue
 * is set -- carries "Expect: 100-continue" (RFC 9110 10.1.1). The
 * body is NOT written here: it goes later, and only if the server
 * wants it.
 *
 * The request enters the in-flight queue when the HEAD goes out, not
 * when the body goes: the server's answer may be a final response
 * that arrives before the body does, and that answer belongs to this
 * request. */
int http_client_connection_write_head(http_client_connection *c, const char *method,
                                      const char *target, long content_length,
                                      int keep_alive, int expect_continue)
{
    char req[HTTP_CLIENT_MAX_REQUEST];
    int reqlen;

    if (c == NULL || c->fd < 0 || method == NULL || target == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (c->chunked_open) {
        /* A chunked body is still open. */
        errno = EINVAL;
        return -1;
    }
    if (c->body_pending) {
        /* The head of the last request is on the wire and its body has
         * not gone yet: a second head now would put two messages on
         * the wire at once. */
        errno = EINVAL;
        return -1;
    }
    if (expect_continue && content_length <= 0) {
        /* RFC 9110 10.1.1: "A client MUST NOT generate a 100-continue
         * expectation in a request that does not include content."
         *
         * Nothing goes on the wire: the request is refused before it
         * is written, not after. */
        errno = EINVAL;
        return -1;
    }
    if (c->n_inflight >= HTTP_CLIENT_MAX_INFLIGHT) {
        errno = ENOBUFS;
        return -1;
    }
    reqlen = http_client_request_build_head(req, sizeof(req), method, target, c->host,
                                             content_length, keep_alive,
                                             expect_continue ? "Expect" : NULL,
                                             expect_continue ? "100-continue" : NULL);
    if (reqlen < 0) {
        return -1;
    }
    if (write_all(c->fd, req, (size_t)reqlen) != 0) {
        return -1;
    }
    c->in_flight[c->n_inflight++] = (strcmp(method, "HEAD") == 0) ? 1 : 0;
    if (content_length > 0) {
        c->body_pending = 1;
    }
    return 0;
}

/* Stage 65.35: wait for the server's answer to the expectation.
 *
 * The wait is BOUNDED, because RFC 9110 10.1.1 says a client that sent
 * a 100-continue expectation SHOULD NOT wait for an indefinite period.
 * One second is the house's number (curl's), and it is a parameter.
 *
 * The deadline is measured on the monotonic clock, so the wait is the
 * time asked for and not the time the clock happened to lose. */
int http_client_connection_expect_continue(http_client_connection *c, int timeout_ms,
                                           http_response *msg)
{
    long deadline;
    int rc;

    if (c == NULL || c->fd < 0 || msg == NULL || timeout_ms < 0) {
        errno = EINVAL;
        return -1;
    }
    deadline = monotonic_ms() + (long)timeout_ms;
    rc = client_read_message(c, msg, deadline);
    if (rc < 0) {
        return -1;
    }
    if (status_is_interim(msg->status)) {
        /* The interim answers the EXPECTATION, not the request: the
         * request stays in flight and the body still has to go.
         *
         * The count lives in the connection, because the final
         * response comes in a LATER call (Stage 65.35): a peer that
         * closes after this 100 is a peer that closed after an
         * interim (Stage 65.31), and only the connection knows
         * that. */
        c->interims++;
        return 1;
    }
    /* A final response before the body: the server has answered
     * without the content -- a 417, a 401, a 405. That response IS
     * the answer, and the body is NOT wanted: writing it onto a
     * connection the peer may be closing is how a client causes a
     * reset. */
    if (c->n_inflight > 0) {
        memmove(c->in_flight, c->in_flight + 1,
                (size_t)(c->n_inflight - 1) * sizeof(c->in_flight[0]));
        c->n_inflight--;
    }
    c->body_pending = 0;
    return 0;
}

/* Stage 65.35: write the body of the request whose head went out. */
int http_client_connection_write_body(http_client_connection *c, const void *body,
                                      size_t body_len)
{
    if (c == NULL || c->fd < 0 || (body == NULL && body_len > 0)) {
        errno = EINVAL;
        return -1;
    }
    if (!c->body_pending) {
        /* No head is waiting for a body. Bytes on the wire now would
         * answer nothing, and the peer would read them as the start
         * of the next request. */
        errno = EINVAL;
        return -1;
    }
    if (body_len > 0 && write_all(c->fd, (const char *)body, body_len) != 0) {
        return -1;
    }
    c->body_pending = 0;
    return 0;
}

int http_client_connection_request(http_client_connection *c, const char *method,
                                   const char *target, const char *body, size_t body_len,
                                   http_response *out)
{
    if (http_client_connection_write_request(c, method, target, body, body_len) != 0) {
        return -1;
    }
    return http_client_connection_read_response(c, out);
}

/* ---------- one round trip ---------- */

static int client_get_inner(const char *host, uint16_t port, const char *target,
                         const char *hdr_name, const char *hdr_value,
                         http_response *out)
{
    struct sockaddr_in addr;
    int fd;
    char req[HTTP_CLIENT_MAX_REQUEST];
    char buf[4096];
    int reqlen;
    size_t sent = 0;
    http_response_parser p;
    http_parse_status st = HTTP_PARSE_NEED_MORE;
    int saw_interim = 0;

    if (host == NULL || target == NULL || out == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (client_addr(host, port, &addr) != 0) {
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }

    reqlen = http_client_request_build_with_header(req, sizeof(req), "GET", target, host,
                                           NULL, 0, 0, hdr_name, hdr_value);
    if (reqlen < 0) {
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }
    while (sent < (size_t)reqlen) {
        ssize_t n = write(fd, req + sent, (size_t)reqlen - sent);
        if (n < 0) {
            int e;
            if (errno == EINTR) {
                continue;
            }
            e = errno;
            close(fd);
            errno = e;
            return -1;
        }
        sent += (size_t)n;
    }

    http_response_parser_init(&p);
    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n == 0) {
            /* The peer closed. But the buffer may still hold a complete
             * response that was fed and not yet parsed -- the interim
             * case, where the final response arrived in the same read.
             * The parser is asked before the close is believed. */
            st = http_response_parser_next(&p, out);
            if (st == HTTP_PARSE_OK) {
                if (!status_is_interim(out->status)) {
                    break;                 /* the final response */
                }
                /* The peer closed after an interim response: the final
                 * one never came. */
                errno = HTTP_REASON_INCOMPLETE;
                st = HTTP_PARSE_ERROR;
            } else {
                /* The peer closed: an EOF-framed response is complete now. */
                st = http_response_parser_finish(&p, out);
                if (saw_interim && st != HTTP_PARSE_OK) {
                    errno = HTTP_REASON_INCOMPLETE;
                }
            }
            break;
        }
        if (n < 0) {
            int e;
            if (errno == EINTR) {
                continue;
            }
            e = errno;
            close(fd);
            errno = e;
            return -1;
        }
        if (http_response_parser_feed(&p, buf, (size_t)n) != 0) {
            int e = errno;
            close(fd);
            errno = e;
            return -1;
        }
        st = http_response_parser_next(&p, out);
        if (st == HTTP_PARSE_OK) {
            if (status_is_interim(out->status)) {
                /* Stage 65.31: the interim response is not the answer;
                 * the final response follows it. */
                saw_interim = 1;
                continue;
            }
            break;
        }
        if (st == HTTP_PARSE_ERROR) {
            int e = errno;
            close(fd);
            errno = e;
            return -1;
        }
    }
    if (st != HTTP_PARSE_OK) {
        /* The reason is the evidence: it has to survive the close. */
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }
    close(fd);
    return 0;
}

int http_client_get(const char *host, uint16_t port, const char *target,
                    http_response *out)
{
    return client_get_inner(host, port, target, NULL, NULL, out);
}

/* Stage 65.34: the conditional GET. The tag goes out as "If-None-Match"
 * and the answer is whatever the server makes of it -- a 304 when the
 * representation has not changed, a 200 when it has. */
/* Stage 65.35: one request that asks before it sends its body.
 *
 * The client's half of the 100 Continue, end to end: the head goes out
 * with the expectation, the wait is bounded, and the body goes only if
 * the server wants it.
 *
 * A timeout is an outcome, not an error: the RFC says a client MAY
 * proceed to send the content even if it has not yet received a
 * response, so the body goes anyway and the request behaves like an
 * ordinary one. */
int http_client_request_expect(const char *host, uint16_t port,
                                const char *method, const char *target,
                                const char *body, size_t body_len,
                                int timeout_ms, http_response *out)
{
    http_client_connection c;
    http_response msg;
    int waited;

    if (host == NULL || method == NULL || target == NULL || out == NULL ||
        (body == NULL && body_len > 0)) {
        errno = EINVAL;
        return -1;
    }
    if (body_len == 0) {
        /* RFC 9110 10.1.1: a client MUST NOT generate a 100-continue
         * expectation in a request that does not include content. */
        errno = EINVAL;
        return -1;
    }
    if (timeout_ms < 0) {
        errno = EINVAL;
        return -1;
    }
    if (http_client_connection_open(&c, host, port) != 0) {
        return -1;
    }
    if (http_client_connection_write_head(&c, method, target, (long)body_len,
                                          1, 1) != 0) {
        http_client_connection_close(&c);
        return -1;
    }
    waited = http_client_connection_expect_continue(&c, timeout_ms, &msg);
    if (waited < 0) {
        if (errno != ETIMEDOUT) {
            http_client_connection_close(&c);
            return -1;
        }
        /* The timeout: the body goes anyway. */
    } else if (waited == 0) {
        /* A final response before the body: it IS the answer, and the
         * body is not wanted. */
        *out = msg;
        http_client_connection_close(&c);
        return 0;
    }
    if (http_client_connection_write_body(&c, body, body_len) != 0) {
        http_client_connection_close(&c);
        return -1;
    }
    if (http_client_connection_read_response(&c, out) != 0) {
        http_client_connection_close(&c);
        return -1;
    }
    http_client_connection_close(&c);
    return 0;
}

int http_client_get_conditional(const char *host, uint16_t port,
                                const char *target, const char *etag,
                                http_response *out)
{
    if (etag == NULL) {
        return client_get_inner(host, port, target, NULL, NULL, out);
    }
    return client_get_inner(host, port, target, "If-None-Match", etag, out);
}
