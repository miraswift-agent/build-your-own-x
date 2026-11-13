/*
 * Stage 65.6 -- HTTP/1.1 from the wire. See http.h for the lesson.
 */

/* _DEFAULT_SOURCE and _POSIX_C_SOURCE expose gmtime_r under -std=c11. */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "http.h"

#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <strings.h>

#include "sockets.h"
#include "coding.h"
#include "content_length.h"
#include "etag.h"

static int ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static int copy_token(char *dst, size_t dst_len, const char *src, size_t n)
{
    if (n == 0 || n >= dst_len) {
        return -1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
    return 0;
}

void http_parser_init(http_parser *p)
{
    p->len = 0;
}

int http_parser_feed(http_parser *p, const void *data, size_t len)
{
    if (!p || (!data && len > 0)) {
        errno = EINVAL;
        return -1;
    }
    if (p->len + len > sizeof(p->buf)) {
        errno = ENOBUFS;
        return -1;
    }
    if (len > 0) {
        memcpy(p->buf + p->len, data, len);
        p->len += len;
    }
    return 0;
}

/* The offset of the blank line that ends the header block, or -1. */
static long find_header_end(const http_parser *p)
{
    size_t i;
    if (p->len < 4) {
        return -1;
    }
    for (i = 0; i + 3 < p->len; i++) {
        if (p->buf[i] == '\r' && p->buf[i + 1] == '\n' &&
            p->buf[i + 2] == '\r' && p->buf[i + 3] == '\n') {
            return (long)i;
        }
    }
    return -1;
}

/* METHOD SP TARGET SP VERSION */
static int parse_request_line(const char *s, size_t n, http_request *out)
{
    size_t sp1, start2, sp2, start3;

    sp1 = 0;
    while (sp1 < n && s[sp1] != ' ') {
        sp1++;
    }
    if (sp1 == 0 || sp1 >= n) {
        return -1;               /* no method, or no space at all */
    }

    start2 = sp1 + 1;
    while (start2 < n && s[start2] == ' ') {
        start2++;
    }
    sp2 = start2;
    while (sp2 < n && s[sp2] != ' ') {
        sp2++;
    }
    if (sp2 == start2 || sp2 >= n) {
        return -1;               /* empty target, or no version */
    }

    start3 = sp2 + 1;
    while (start3 < n && s[start3] == ' ') {
        start3++;
    }
    if (start3 >= n) {
        return -1;               /* no version */
    }

    if (copy_token(out->method, sizeof(out->method), s, sp1) != 0) {
        return -1;
    }
    if (copy_token(out->target, sizeof(out->target), s + start2, sp2 - start2) != 0) {
        return -1;
    }
    if (copy_token(out->version, sizeof(out->version), s + start3, n - start3) != 0) {
        return -1;
    }
    if (strncmp(out->version, "HTTP/", 5) != 0) {
        return -1;               /* not an HTTP request line */
    }
    return 0;
}

/* "Name: value" lines, ending at the blank line the caller trimmed. */
static int parse_headers(const char *s, size_t n, http_request *out)
{
    size_t i = 0;

    out->nheaders = 0;
    while (i < n) {
        size_t eol = i;
        size_t linelen, c, nstart, nend, vstart, vend;
        const char *line;

        while (eol + 1 < n && !(s[eol] == '\r' && s[eol + 1] == '\n')) {
            eol++;
        }
        if (eol + 1 >= n) {
            return -1;           /* a header line without CRLF */
        }
        linelen = eol - i;
        if (linelen == 0) {
            break;
        }
        line = s + i;

        for (c = 0; c < linelen; c++) {
            if (line[c] == ':') {
                break;
            }
        }
        if (c == linelen) {
            return -1;           /* a header with no colon is not a header */
        }
        if (out->nheaders >= HTTP_MAX_HEADERS) {
            return -1;
        }

        nstart = 0;
        nend = c;
        while (nend > nstart && (line[nend - 1] == ' ' || line[nend - 1] == '\t')) {
            nend--;
        }
        if (copy_token(out->headers[out->nheaders].name,
                       sizeof(out->headers[out->nheaders].name),
                       line + nstart, nend - nstart) != 0) {
            return -1;
        }

        vstart = c + 1;
        while (vstart < linelen && (line[vstart] == ' ' || line[vstart] == '\t')) {
            vstart++;
        }
        vend = linelen;
        while (vend > vstart && (line[vend - 1] == ' ' || line[vend - 1] == '\t')) {
            vend--;
        }
        if (copy_token(out->headers[out->nheaders].value,
                       sizeof(out->headers[out->nheaders].value),
                       line + vstart, vend - vstart) != 0) {
            return -1;
        }
        out->nheaders++;
        i = eol + 2;
    }
    return 0;
}

/* Stage 65.25: the Content-Length, read by the shared reader.
 *
 * The field value is not one number -- it is a LIST of numbers, and the
 * list has one legal shape: every value valid, every value the same
 * (RFC 9112 6.3 item 5). "5, 5" is one value, 5, and the message is
 * processed with it; "5, 6" is invalid, and so is a value that is not
 * 1*DIGIT (RFC 9110 8.6). The old strtol accepted a sign ("+5"), so
 * the same header meant two different things on the two sides of a
 * socket.
 *
 * Stage 65.26: the reader reads the COMBINED field value, so a request
 * that carries the field more than once is read by all of its field
 * lines. RFC 9110 5.2: a repeated field name's combined field value is
 * its field line values concatenated in order, separated by commas. Two
 * lines naming 5 are one value; two lines naming 5 and 6 are an
 * invalid framing -- and taking the first line instead (what we used
 * to do) is a wrong accept, and the request-smuggling shape.
 *
 * The return says WHICH failure it was, because the two failures are
 * different claims: -1 is "the framing is invalid" (the RFC's
 * unrecoverable error) and -2 is "a length we will not hold" (our
 * capacity, not the message's fault). */
static int parse_content_length(http_request *r)
{
    long n;
    int st;

    r->content_length = -1;
    st = http_content_length_collect(r->headers, r->nheaders, &n);
    if (st == HTTP_CL_ABSENT) {
        return 0;                /* absent: no body */
    }
    if (st != HTTP_CL_OK) {
        return -1;               /* invalid: the framing is invalid */
    }
    if (n > HTTP_MAX_BODY) {
        return -2;               /* bigger than we are willing to hold */
    }
    r->content_length = n;
    return 0;
}

/* The transfer codings this server understands.
 *
 * Chunked is the only one: it is the only coding we can decode (the
 * decoder Stage 65.11 wrote). RFC 9112 6.1: "A server that receives a
 * request message with a transfer coding it does not understand SHOULD
 * respond with 501 (Not Implemented)."
 *
 * The set is the server's own -- the list is read by the shared scan
 * in coding.h, but what this server can APPLY is a property of this
 * program, not of the header. */
static int server_coding_is_understood(const char *coding, size_t len)
{
    return http_coding_is_chunked(coding, len);
}

/* Is the body framed by chunked transfer-encoding? */
static int request_is_chunked(const http_request *r)
{
    http_coding_scan cs;

    http_coding_scan_list(http_header_get(r, "Transfer-Encoding"),
                          server_coding_is_understood, &cs);
    return cs.final_is_chunked;
}

http_parse_status http_parser_next(http_parser *p, http_request *out)
{
    long hend;
    size_t first_crlf, i, body_start, want, total, rest;

    if (!p || !out) {
        errno = EINVAL;
        return HTTP_PARSE_ERROR;
    }
    memset(out, 0, sizeof(*out));
    out->content_length = -1;

    hend = find_header_end(p);
    if (hend < 0) {
        if (p->len >= sizeof(p->buf)) {
            errno = EMSGSIZE;
            return HTTP_PARSE_ERROR;   /* too big to ever be a request */
        }
        return HTTP_PARSE_NEED_MORE;
    }

    first_crlf = (size_t)-1;
    for (i = 0; i <= (size_t)hend; i++) {
        if (p->buf[i] == '\r' && p->buf[i + 1] == '\n') {
            first_crlf = i;
            break;
        }
    }
    if (first_crlf == (size_t)-1) {
        errno = EPROTO;
        return HTTP_PARSE_ERROR;
    }

    if (parse_request_line(p->buf, first_crlf, out) != 0) {
        errno = EPROTO;
        return HTTP_PARSE_ERROR;
    }
    {
        /* The header lines live between the end of the request line and
         * the blank line. When the request has no headers at all, the
         * blank line IS the request line's own CRLF followed by an
         * empty one -- so the region is empty, and computing its length
         * by subtraction underflows. That is the bug this comment
         * exists to prevent: every request without headers parsed as
         * an error, and 14 of the 21 tests failed for one reason. */
        size_t hstart = first_crlf + 2;
        size_t hend2 = (size_t)hend + 2;
        size_t hlen = (hend2 > hstart) ? (hend2 - hstart) : 0;
        if (parse_headers(p->buf + hstart, hlen, out) != 0) {
            errno = EPROTO;
            return HTTP_PARSE_ERROR;
        }
    }
    {
        int clrc = parse_content_length(out);
        if (clrc != 0) {
            /* The reason travels out in errno: EBADMSG for an invalid
             * Content-Length (the framing is invalid -- RFC 9112 6.3
             * item 5) and EPROTO for a length we will not hold. The
             * server answers 400 for both -- the request is wrong --
             * and closes the connection either way. */
            errno = (clrc == -1) ? EBADMSG : EPROTO;
            return HTTP_PARSE_ERROR;
        }
    }

    /* Stage 65.21: a request that carries BOTH a Transfer-Encoding and
     * a Content-Length has two answers to "where does the body end",
     * and two recipients can pick different ones -- which is the shape
     * request smuggling is made of (RFC 9112 11.2).
     *
     * RFC 9112 6.3 item 3 says two things about that message: the
     * coding overrides the length (which is what a recipient that has
     * to process the message anyway does -- our CLIENT, and an
     * intermediary that strips the length before forwarding), and such
     * a message "ought to be handled as an error".
     *
     * We are an origin server, and an origin server can say no: the
     * loud half. The request is refused HERE, before any body is
     * framed, so the answer does not wait for bytes we have already
     * decided to refuse. The server turns the refusal into 400 and
     * closes the connection -- a connection whose framing is ambiguous
     * cannot be trusted with a second message.
     *
     * The rule is about the PRESENCE of the two headers, not about
     * which coding the Transfer-Encoding names: "here is a coding" and
     * "here is a length" disagree whatever the coding is called. */
    if (out->content_length >= 0 &&
        http_header_get(out, "Transfer-Encoding") != NULL) {
        errno = EPROTO;
        return HTTP_PARSE_ERROR;
    }

    /* Stage 65.22: RFC 9112 6.3 item 4 -- "If a Transfer-Encoding
     * header field is present in a request and the chunked transfer
     * coding is not the final encoding, the message body length cannot
     * be determined reliably; the server MUST respond with the 400 (Bad
     * Request) status code and then close the connection."
     *
     * The rule is about the FINAL coding, and that word carries the
     * whole rule: "gzip, chunked" frames the body with chunks (the
     * coding in front of it is applied first, and the chunked coding
     * draws the boundary), so that request is answered; "chunked,
     * gzip" does not, so that request is refused. The same two codings
     * in the other order give the opposite answer.
     *
     * A request whose body length cannot be determined is a request
     * whose body boundary two recipients can disagree about -- the
     * shape request smuggling is made of (RFC 9112 11.2), and the same
     * shape Stage 65.21 refused from the other side.
     *
     * The refusal lives in the PARSER, beside 65.21's rule, because the
     * framing is what is wrong: the request never becomes a request,
     * and the answer does not wait for a body we cannot measure. */
    {
        const char *te = http_header_get(out, "Transfer-Encoding");

        if (te != NULL) {
            http_coding_scan cs;

            http_coding_scan_list(te, server_coding_is_understood, &cs);

            /* Stage 65.22: the body length cannot be determined. */
            if (!cs.final_is_chunked) {
                errno = EPROTO;
                return HTTP_PARSE_ERROR;
            }

            /* Stage 65.23: RFC 9112 6.1 -- "A server that receives a
             * request message with a transfer coding it does not
             * understand SHOULD respond with 501 (Not
             * Implemented)."
             *
             * The framing is determinate here -- the final coding is
             * chunked, so we know where the body ends -- but a coding
             * in FRONT of it is one we cannot apply. "gzip, chunked"
             * means the content was compressed and then the compressed
             * content was chunked; we can take the chunks apart and we
             * cannot decompress them, so the body we would hand back
             * would still be gzip coded. Answering 200 with it would
             * be a server claiming to have processed a message it did
             * not understand.
             *
             * The reason is NOT a protocol error: the request is well
             * formed and the framing is fine. It is "not implemented",
             * which is why the answer is 501 and not 400 -- the request
             * is not wrong, this server is not capable. So the reason
             * travels out of the parser in errno (ENOTSUP) and the
             * server chooses the status from it.
             *
             * The order matters, and the order comes from the RFCs: the
             * 65.22 rule above is a MUST and this one is a SHOULD, so
             * "Transfer-Encoding: gzip" is a 400 (the length cannot be
             * determined) and not a 501. */
            if (cs.has_unknown) {
                errno = ENOTSUP;
                return HTTP_PARSE_ERROR;
            }
        }
    }

    body_start = (size_t)hend + 4;

    if (request_is_chunked(out)) {
        /* The body draws its own boundaries: it is a sequence of chunks,
         * and the terminal chunk is followed by the bytes that belong to
         * the NEXT message.
         *
         * A coding frames the body when it is the ONLY framing on the
         * wire. (A request that carries both a coding and a length was
         * refused above -- RFC 9112 6.3 item 3, and the loud half of
         * it.)
         *
         * The decoder is fed from the buffer every time, so a request
         * that arrives in pieces is simply not ready until it is whole. */
        http_chunk_decoder d;
        const char *pending = NULL;
        size_t pending_len;
        size_t total = 0;

        http_chunk_decoder_init(&d);
        if (p->len > body_start) {
            if (http_chunk_decoder_feed(&d, p->buf + body_start,
                                        p->len - body_start) != 0) {
                errno = EPROTO;
                return HTTP_PARSE_ERROR;
            }
        }
        for (;;) {
            char chunk[HTTP_MAX_BODY];
            size_t clen = 0;
            http_chunk_status st;

            st = http_chunk_decoder_next(&d, chunk, sizeof(chunk), &clen);
            if (st == HTTP_CHUNK_NEED_MORE) {
                /* The body has not all arrived. This is not an error:
                 * the request is simply not ready yet, and the buffer is
                 * left untouched. */
                return HTTP_PARSE_NEED_MORE;
            }
            if (st == HTTP_CHUNK_ERROR) {
                errno = EPROTO;
                return HTTP_PARSE_ERROR;
            }
            if (st == HTTP_CHUNK_DONE) {
                break;
            }
            if (total + clen > HTTP_MAX_BODY) {
                errno = EMSGSIZE;
                return HTTP_PARSE_ERROR;   /* bigger than we will hold */
            }
            memcpy(out->body + total, chunk, clen);
            total += clen;
        }
        out->body_len = total;

        /* What follows the terminal chunk is the start of the next
         * message, not part of this one. */
        pending_len = http_chunk_decoder_pending(&d, &pending);
        rest = pending_len;
        if (rest > 0) {
            memmove(p->buf, pending, rest);
        }
        p->len = rest;
        return HTTP_PARSE_OK;
    }

    want = (out->content_length > 0) ? (size_t)out->content_length : 0;
    if (p->len - body_start < want) {
        /* The body has not all arrived. This is not an error: the request
         * is simply not ready yet, and the buffer is left untouched. */
        return HTTP_PARSE_NEED_MORE;
    }
    if (want > 0) {
        memcpy(out->body, p->buf + body_start, want);
    }
    out->body_len = want;

    total = body_start + want;
    rest = p->len - total;
    if (rest > 0) {
        memmove(p->buf, p->buf + total, rest);
    }
    p->len = rest;
    return HTTP_PARSE_OK;
}

const char *http_header_get(const http_request *r, const char *name)
{
    int i;
    if (!r || !name) {
        return NULL;
    }
    for (i = 0; i < r->nheaders; i++) {
        if (ieq(r->headers[i].name, name)) {
            return r->headers[i].value;
        }
    }
    return NULL;
}

int http_request_keep_alive(const http_request *r)
{
    const char *conn;
    if (!r) {
        return 0;
    }
    conn = http_header_get(r, "Connection");
    if (conn) {
        if (ieq(conn, "close")) {
            return 0;
        }
        if (ieq(conn, "keep-alive")) {
            return 1;
        }
    }
    /* HTTP/1.1 keeps the connection open by default; HTTP/1.0 closes it.
     * Getting this backwards shows up in a browser as a hang, not an
     * error, which is why it is worth a test of its own. */
    return strcmp(r->version, "HTTP/1.1") == 0;
}

/* The Date header's value for a time, in IMF-fixdate form.
 *
 * The day and month names are written out by hand rather than taken
 * from strftime: %a and %b are locale-dependent, and a header a cache
 * parses must not change with the environment. */
int http_date_format(time_t when, char *out, size_t out_cap)
{
    static const char *const days[7] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *const months[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    struct tm tm;
    int n;

    if (out == NULL || out_cap == 0) {
        errno = EINVAL;
        return -1;
    }
    if (gmtime_r(&when, &tm) == NULL) {
        errno = EINVAL;
        return -1;
    }
    n = snprintf(out, out_cap, "%s, %02d %s %04d %02d:%02d:%02d GMT",
                 days[tm.tm_wday], tm.tm_mday, months[tm.tm_mon],
                 tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);
    if (n < 0) {
        errno = EINVAL;
        return -1;
    }
    if ((size_t)n >= out_cap) {
        errno = ENOBUFS;
        return -1;
    }
    return n;
}

int http_response_build(char *dst, size_t dst_len, int status, const char *reason,
                        const char *content_type, const char *body,
                        size_t body_len, int keep_alive, const char *date)
{
    return http_response_build_allow(dst, dst_len, status, reason, content_type,
                                      body, body_len, keep_alive, date, NULL);
}

/* Stage 65.29: the same response, with an Allow header when the caller
 * has one.
 *
 * One builder, not two: the header order, the length, the date, the
 * connection -- all of it is the same code path, so a response with an
 * Allow header cannot drift from a response without one. */
int http_response_build_allow(char *dst, size_t dst_len, int status, const char *reason,
                               const char *content_type, const char *body,
                               size_t body_len, int keep_alive, const char *date,
                               const char *allow)
{
    return http_response_build_full(dst, dst_len, status, reason, content_type,
                                    body, body_len, keep_alive, date, allow,
                                    NULL);
}

/* Stage 65.32: the builder with every header this component can send.
 *
 * The optional headers are the Allow list and the entity tag; both are
 * NULL for "no such header". And body_len == HTTP_NO_LENGTH means the
 * response carries NO Content-Length header field and no content --
 * which is what a 304 is: RFC 9110 15.4.5, "A 304 response is
 * terminated by the end of the header section; it cannot contain
 * content or trailers."
 *
 * One implementation, so the header order and the framing cannot
 * differ between the responses that have an ETag and the ones that do
 * not. */
int http_response_build_full(char *dst, size_t dst_len, int status, const char *reason,
                              const char *content_type, const char *body,
                              size_t body_len, int keep_alive, const char *date,
                              const char *allow, const char *etag)
{
    char ctype_line[80];
    char allow_line[160];
    char etag_line[HTTP_ETAG_MAX + 16];
    char length_line[48];
    const char *ctype_hdr = "";
    const char *allow_hdr = "";
    const char *etag_hdr = "";
    const char *length_hdr = "";
    int n;

    if (!dst || dst_len == 0 || !date) {
        errno = EINVAL;
        return -1;
    }
    /* Stage 65.30: a message with no content has no media type.
     *
     * RFC 9110 8.3: "A sender SHOULD NOT generate a Content-Type
     * header field in a message that does not contain content." An
     * empty string says "no Content-Type"; NULL keeps the old default
     * of text/plain, so the callers that want a type do not have to
     * change. */
    if (content_type == NULL) {
        content_type = "text/plain";
    }
    if (content_type[0] != '\0') {
        int cn = snprintf(ctype_line, sizeof(ctype_line),
                          "Content-Type: %s\r\n", content_type);
        if (cn < 0 || (size_t)cn >= sizeof(ctype_line)) {
            errno = ENOBUFS;
            return -1;
        }
        ctype_hdr = ctype_line;
    }
    if (allow != NULL && *allow != '\0') {
        int an = snprintf(allow_line, sizeof(allow_line), "Allow: %s\r\n", allow);
        if (an < 0 || (size_t)an >= sizeof(allow_line)) {
            errno = ENOBUFS;
            return -1;
        }
        allow_hdr = allow_line;
    }
    /* Stage 65.32: the entity tag of the selected representation. */
    if (etag != NULL && *etag != '\0') {
        int en = snprintf(etag_line, sizeof(etag_line), "ETag: %s\r\n", etag);
        if (en < 0 || (size_t)en >= sizeof(etag_line)) {
            errno = ENOBUFS;
            return -1;
        }
        etag_hdr = etag_line;
    }
    /* The length -- unless the response says it has none. */
    if (body_len != HTTP_NO_LENGTH) {
        int ln = snprintf(length_line, sizeof(length_line),
                          "Content-Length: %zu\r\n", body_len);
        if (ln < 0 || (size_t)ln >= sizeof(length_line)) {
            errno = ENOBUFS;
            return -1;
        }
        length_hdr = length_line;
    }
    n = snprintf(dst, dst_len,
                 "HTTP/1.1 %d %s\r\n"
                 "Date: %s\r\n"
                 "%s"
                 "%s"
                 "%s"
                 "%s"
                 "Connection: %s\r\n"
                 "\r\n",
                 status, reason ? reason : "",
                 date,
                 ctype_hdr,
                 allow_hdr,
                 etag_hdr,
                 length_hdr,
                 keep_alive ? "keep-alive" : "close");
    if (n < 0) {
        return -1;
    }
    if (body_len == HTTP_NO_LENGTH) {
        /* No content, and no claim about any. */
        if ((size_t)n >= dst_len) {
            errno = ENOBUFS;
            return -1;
        }
        return n;
    }
    if ((size_t)n + body_len >= dst_len) {
        errno = ENOBUFS;
        return -1;
    }
    if (body_len > 0) {
        memcpy(dst + n, body, body_len);
    }
    return n + (int)body_len;
}

/* The host name from a Host header value.
 *
 * "example.com:8080" names the host "example.com": the port is not
 * part of the name. A bracketed IPv6 literal keeps its brackets --
 * "[::1]:8080" names the host "[::1]".
 *
 * Returns 0 on success, -1 if the value is empty or too long. */
static int host_name_from_value(const char *value, char *out, size_t out_cap)
{
    size_t len;

    if (value == NULL || value[0] == '\0') {
        return -1;
    }
    if (value[0] == '[') {
        const char *close = strchr(value, ']');
        if (close == NULL) {
            return -1;
        }
        len = (size_t)(close - value) + 1;
    } else {
        const char *colon = strchr(value, ':');
        len = (colon != NULL) ? (size_t)(colon - value) : strlen(value);
    }
    if (len == 0 || len + 1 > out_cap) {
        return -1;
    }
    memcpy(out, value, len);
    out[len] = '\0';
    return 0;
}

/* The document for a request: the route that matches the host, or the
 * default. A host name is case-insensitive, so the comparison is too. */
static const char *route_document(const http_host_route *routes, size_t route_count,
                                   const char *host, const char *default_body)
{
    size_t i;

    if (routes != NULL && host != NULL) {
        for (i = 0; i < route_count; i++) {
            if (routes[i].host != NULL && strcasecmp(routes[i].host, host) == 0) {
                return routes[i].body ? routes[i].body : "";
            }
        }
    }
    return default_body ? default_body : "";
}

/* Stage 65.29: the methods this server knows at all.
 *
 * A method outside this set is one the server does not recognize, and
 * RFC 9110 15.6.2 names the answer for that: 501, because it "is the
 * appropriate response when the server does not recognize the request
 * method and is not capable of supporting it for any resource." */
#define HTTP_METHOD_GET  0x1u
#define HTTP_METHOD_HEAD 0x2u
#define HTTP_METHOD_POST 0x4u
/* Stage 65.30: OPTIONS applies to every resource this server serves,
 * so it is in every resource's set. That is also why it belongs in the
 * Allow list: RFC 9110 10.2.1 says the field "lists the set of methods
 * advertised as supported by the target resource", and after this
 * stage OPTIONS is one of them. */
#define HTTP_METHOD_OPTIONS 0x8u

/* The methods a resource supports. RFC 9110 9.1: the standardized
 * methods "are not resource-specific ... though each resource
 * determines for itself whether those semantics are implemented or
 * allowed." So this is per resource and not per server -- which is
 * what makes the Allow header a list rather than a constant.
 *
 * One table, so the mask and the list that goes on the wire cannot
 * drift apart: both are read from here. */
static const struct {
    const char *target;
    unsigned methods;
} resource_table[] = {
    { "/",       HTTP_METHOD_GET | HTTP_METHOD_HEAD | HTTP_METHOD_OPTIONS },
    { "/stream", HTTP_METHOD_GET | HTTP_METHOD_HEAD | HTTP_METHOD_OPTIONS },
    { "/echo",   HTTP_METHOD_GET | HTTP_METHOD_HEAD | HTTP_METHOD_POST | HTTP_METHOD_OPTIONS }
};

static unsigned resource_methods(const char *target)
{
    size_t i;

    for (i = 0; i < sizeof(resource_table) / sizeof(resource_table[0]); i++) {
        if (strcmp(target, resource_table[i].target) == 0) {
            return resource_table[i].methods;
        }
    }
    return 0u;   /* not a resource this server serves */
}

/* The bit of a method this server knows, or 0 for a method it does
 * not. The method token is case-sensitive (RFC 9110 9.1: "The method
 * token is case-sensitive because it might be used as a gateway to
 * object-based systems with case-sensitive method names"), so "get" is
 * not GET -- an unknown method is not a misspelling of a known one. */
static unsigned method_bit(const char *method)
{
    if (strcmp(method, "GET") == 0) {
        return HTTP_METHOD_GET;
    }
    if (strcmp(method, "HEAD") == 0) {
        return HTTP_METHOD_HEAD;
    }
    if (strcmp(method, "POST") == 0) {
        return HTTP_METHOD_POST;
    }
    if (strcmp(method, "OPTIONS") == 0) {
        return HTTP_METHOD_OPTIONS;
    }
    return 0u;
}

/* The Allow header's value: the methods the resource supports, in the
 * order the server knows them. */
static int allow_header_build(char *dst, size_t dst_len, unsigned mask)
{
    static const struct {
        unsigned bit;
        const char *name;
    } methods[] = {
        { HTTP_METHOD_GET, "GET" },
        { HTTP_METHOD_HEAD, "HEAD" },
        { HTTP_METHOD_POST, "POST" },
        { HTTP_METHOD_OPTIONS, "OPTIONS" }
    };
    size_t used = 0;
    size_t i;

    if (!dst || dst_len == 0) {
        errno = EINVAL;
        return -1;
    }
    dst[0] = '\0';
    for (i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
        int n;
        if ((mask & methods[i].bit) == 0) {
            continue;
        }
        n = snprintf(dst + used, dst_len - used, "%s%s",
                     used > 0 ? ", " : "", methods[i].name);
        if (n < 0 || (size_t)n >= dst_len - used) {
            errno = ENOBUFS;
            return -1;
        }
        used += (size_t)n;
    }
    return (int)used;
}

int http_resource_allow(char *dst, size_t dst_len, const char *target)
{
    unsigned mask;

    if (!dst || dst_len == 0 || !target) {
        errno = EINVAL;
        return -1;
    }
    mask = resource_methods(target);
    if (mask == 0u) {
        dst[0] = '\0';
        return 0;   /* not a resource this server serves */
    }
    return allow_header_build(dst, dst_len, mask);
}

/* Stage 65.28: how many bytes of a built response go on the wire.
 *
 * A response to a HEAD request carries the head the matching GET would
 * have carried, and NO content: RFC 9110 9.3.2 ("the server MUST NOT
 * send content in the response") and RFC 9112 6.3 item 1 (such a
 * response "is always terminated by the first empty line after the
 * header fields, regardless of the header fields present in the
 * message").
 *
 * The head is built exactly as it would be for the GET -- including
 * the Content-Length, which is a claim about the body the GET would
 * have returned -- and then the content is left off the wire. So the
 * bytes to send are the bytes up to and including the empty line that
 * ends the header section.
 *
 * Returns 0 when there is no header section in the buffer, which the
 * caller treats as an error: a response with no head is not a
 * response. */
static size_t response_bytes_to_send(const char *resp, size_t n, int is_head)
{
    size_t i;

    if (!is_head) {
        return n;
    }
    for (i = 0; i + 4 <= n; i++) {
        if (memcmp(resp + i, "\r\n\r\n", 4) == 0) {
            return i + 4;
        }
    }
    return 0;
}

/* The one serve loop. require_host says whether this server routes by
 * the Host header -- and so whether a request without one can be
 * answered at all. */
static int serve_connection(int fd, const http_host_route *routes, size_t route_count,
                             const char *default_body, int require_host,
                             http_server_stats *st)
{
    http_parser p;
    http_request req;
    http_parse_status ps;
    char chunk[4096];
    char resp[HTTP_MAX_REQUEST];
    char cbuf[64];
    int served = 0;

    http_parser_init(&p);
    if (st) {
        st->requests = 0;
        st->bytes_out = 0;
    }

    for (;;) {
        while ((ps = http_parser_next(&p, &req)) == HTTP_PARSE_OK) {
            int keep = http_request_keep_alive(&req);
            char host[HTTP_MAX_HVALUE + 1];
            const char *host_name = NULL;
            const char *body;
            size_t blen;
            int n;
            size_t to_send;
            sock_io_result wr;
            char date[64];

            /* Stage 65.28: is this a HEAD request? The method token is
             * case-sensitive (RFC 9110 9.1: "The method token is
             * case-sensitive because it might be used as a gateway to
             * object-based systems with case-sensitive method names"),
             * so "head" is not HEAD. */
            int is_head = (strcmp(req.method, "HEAD") == 0);

            if (http_date_format(time(NULL), date, sizeof(date)) < 0) {
                return -1;
            }

            if (require_host) {
                const char *hostv = http_header_get(&req, "Host");
                if (hostv == NULL ||
                    host_name_from_value(hostv, host, sizeof(host)) != 0) {
                    /* A server with several sites cannot route a request
                     * that does not say which site it is for. */
                    n = http_response_build(resp, sizeof(resp), 400, "Bad Request",
                                            "text/plain", "400 Bad Request\n", 16,
                                            keep, date);
                    if (n < 0) {
                        return -1;
                    }
                    to_send = response_bytes_to_send(resp, (size_t)n, is_head);
                    if (to_send == 0) {
                        return -1;
                    }
                    if (sock_write_exact(fd, resp, to_send, &wr) != 0) {
                        return -1;
                    }
                    served++;
                    if (st) {
                        st->requests++;
                        st->bytes_out += (unsigned long)to_send;
                    }
                    if (!keep) {
                        return served;
                    }
                    continue;
                }
                host_name = host;
            }

            /* Stage 65.29: a request has a method, and the method says
             * what the response is.
             *
             * Three questions, and they have to be asked in this order:
             *
             *   - Does the target resource exist?  No -> 404. There is
             *     no resource whose methods could be listed, so there
             *     is no Allow header.
             *
             *   - Does the server know the method at all?  No -> 501
             *     (RFC 9110 15.6.2: "the server does not recognize the
             *     request method and is not capable of supporting it
             *     for any resource").
             *
             *   - Does THIS resource support the method?  No -> 405,
             *     and the Allow header is a MUST (RFC 9110 15.5.6).
             *
             * The request itself was fine in every one of these cases:
             * its body was framed and consumed by the parser, so the
             * connection is still in sync and the refusal does not have
             * to close it. That is the difference from the parser-level
             * refusals of Stages 65.21-65.23, which refused the
             * MESSAGE -- there the body was never framed, so the
             * connection could not be reused. */
            {
                unsigned supported = resource_methods(req.target);
                unsigned bit = method_bit(req.method);

                if (supported != 0u && bit == 0u) {
                    /* The method is not one this server knows. */
                    n = http_response_build(resp, sizeof(resp), 501,
                                            "Not Implemented", "text/plain",
                                            "501 Not Implemented\n", 20,
                                            keep, date);
                    if (n < 0) {
                        return -1;
                    }
                    to_send = response_bytes_to_send(resp, (size_t)n, is_head);
                    if (to_send == 0) {
                        return -1;
                    }
                    if (sock_write_exact(fd, resp, to_send, &wr) != 0) {
                        return -1;
                    }
                    served++;
                    if (st) {
                        st->requests++;
                        st->bytes_out += (unsigned long)to_send;
                    }
                    if (!keep) {
                        return served;
                    }
                    continue;
                }
                if (supported != 0u && (supported & bit) == 0u) {
                    /* The method is known, and this resource does not
                     * support it. The Allow header is a MUST, and it
                     * names THIS resource's methods. */
                    char allow[64];
                    if (http_resource_allow(allow, sizeof(allow), req.target) < 0) {
                        return -1;
                    }
                    n = http_response_build_allow(resp, sizeof(resp), 405,
                                                  "Method Not Allowed", "text/plain",
                                                  "405 Method Not Allowed\n", 23,
                                                  keep, date, allow);
                    if (n < 0) {
                        return -1;
                    }
                    to_send = response_bytes_to_send(resp, (size_t)n, is_head);
                    if (to_send == 0) {
                        return -1;
                    }
                    if (sock_write_exact(fd, resp, to_send, &wr) != 0) {
                        return -1;
                    }
                    served++;
                    if (st) {
                        st->requests++;
                        st->bytes_out += (unsigned long)to_send;
                    }
                    if (!keep) {
                        return served;
                    }
                    continue;
                }
            }

            if (strcmp(req.method, "OPTIONS") == 0 &&
                resource_methods(req.target) != 0u) {
                /* Stage 65.30: OPTIONS asks what can be done with a
                 * target resource, and the answer is the list this
                 * server already keeps.
                 *
                 * RFC 9110 9.3.7: "A server generating a successful
                 * response to OPTIONS SHOULD send any header that might
                 * indicate optional features implemented by the server
                 * and applicable to the target resource (e.g., Allow)."
                 *
                 * So: 200, the Allow header, and no content. The
                 * framing is the ordinary kind -- Content-Length: 0 --
                 * which is why this needs no client-side machinery. A
                 * HEAD response is different: its length describes
                 * another message, so the client has to be told.
                 *
                 * The target has to exist. A path this server does not
                 * serve has no methods to list, so it falls through to
                 * the 404 below -- which carries no Allow header,
                 * because there is no resource whose methods could be
                 * listed. */
                char allow[64];

                if (http_resource_allow(allow, sizeof(allow), req.target) < 0) {
                    return -1;
                }
                n = http_response_build_allow(resp, sizeof(resp), 200, "OK",
                                              "", NULL, 0, keep, date, allow);
                if (n < 0) {
                    return -1;
                }
                to_send = response_bytes_to_send(resp, (size_t)n, is_head);
                if (to_send == 0) {
                    return -1;
                }
                if (sock_write_exact(fd, resp, to_send, &wr) != 0) {
                    return -1;
                }
                if (wr.status != SOCK_IO_COMPLETE) {
                    return -1;
                }
                served++;
                if (st) {
                    st->requests++;
                    st->bytes_out += (unsigned long)to_send;
                }
                if (!keep) {
                    return served;
                }
                continue;
            }

            if (strcmp(req.target, "/stream") == 0) {
                /* A body whose length is not known until it is done --
                 * the only honest reason to use chunked
                 * transfer-encoding. */
                static const char *const pieces[] = {
                    "Hello, ", "chunked ", "world!\n"
                };
                size_t k;
                size_t sent = 0;

                n = http_response_build_chunked(resp, sizeof(resp), 200, "OK",
                                                "text/plain", keep, date);
                if (n < 0) {
                    return -1;
                }
                to_send = response_bytes_to_send(resp, (size_t)n, is_head);
                if (to_send == 0) {
                    return -1;
                }
                if (sock_write_exact(fd, resp, to_send, &wr) != 0) {
                    return -1;
                }
                sent += to_send;
                /* Stage 65.28: a response to HEAD carries no content --
                 * and the chunks ARE the content. The head alone is the
                 * whole response (RFC 9112 6.3 item 1). */
                if (!is_head) {
                    for (k = 0; k < sizeof(pieces) / sizeof(pieces[0]); k++) {
                        int cn = http_chunk_encode(pieces[k], strlen(pieces[k]),
                                                   cbuf, sizeof(cbuf));
                        if (cn < 0) {
                            return -1;
                        }
                        if (sock_write_exact(fd, cbuf, (size_t)cn, &wr) != 0) {
                            return -1;
                        }
                        sent += (size_t)cn;
                    }
                    {
                        int cn = http_chunk_encoder_finish(cbuf, sizeof(cbuf));
                        if (cn < 0) {
                            return -1;
                        }
                        if (sock_write_exact(fd, cbuf, (size_t)cn, &wr) != 0) {
                            return -1;
                        }
                        sent += (size_t)cn;
                    }
                }
                served++;
                if (st) {
                    st->requests++;
                    st->bytes_out += (unsigned long)sent;
                }
                if (!keep) {
                    return served;
                }
                continue;
            }

            if (strcmp(req.target, "/echo") == 0) {
                /* The body we read, given back.
                 *
                 * The framing was never broken: the request parser knows
                 * Content-Length and consumes the body bytes, so the
                 * bytes that follow the body are already the start of
                 * the next request. What the server could not do was
                 * ACT on what the request said -- the parsed body sat
                 * in a field nobody read, and a POST got the same
                 * answer as a GET.
                 *
                 * The body is bytes, not a C string: the length goes
                 * with it. */
                /* Stage 65.28: the content of a HEAD request has no
                 * generally defined semantics (RFC 9110 9.3.2), so the
                 * response is the one the matching GET would have
                 * produced -- and a GET carries no content, so the echo
                 * is empty and the length is zero. */
                n = http_response_build(resp, sizeof(resp), 200, "OK",
                                        "application/octet-stream",
                                        is_head ? "" : req.body,
                                        is_head ? 0 : req.body_len,
                                        keep, date);
                if (n < 0) {
                    return -1;
                }
                to_send = response_bytes_to_send(resp, (size_t)n, is_head);
                if (to_send == 0) {
                    return -1;
                }
                if (sock_write_exact(fd, resp, to_send, &wr) != 0) {
                    return -1;
                }
                if (wr.status != SOCK_IO_COMPLETE) {
                    return -1;
                }
                served++;
                if (st) {
                    st->requests++;
                    st->bytes_out += (unsigned long)to_send;
                }
                if (!keep) {
                    return served;
                }
                continue;
            }

            /* The document route: the one resource whose representation
             * is a fixed document, and so the one resource this
             * server can give a validator for.
             *
             * Stage 65.32: a conditional GET.
             *
             * RFC 9110 13.1.2: "When an origin server receives a
             * request that selects a representation and that request
             * includes an If-None-Match header field, the origin server
             * MUST evaluate the If-None-Match condition per Section
             * 13.2 prior to performing the method." And: "An origin
             * server that evaluates an If-None-Match condition MUST NOT
             * perform the requested method if the condition evaluates
             * to false; instead, the origin server MUST respond with
             * either a) the 304 (Not Modified) status code if the
             * request method is GET or HEAD or b) the 412 (Precondition
             * Failed) status code for all other request methods."
             *
             * The 304 carries the validator and the date -- RFC 9110
             * 15.4.5: "The server generating a 304 response MUST
             * generate any of the following header fields that would
             * have been sent in a 200 (OK) response to the same
             * request: Content-Location, Date, ETag, and Vary" -- and
             * no content and no length: "A 304 response is terminated
             * by the end of the header section; it cannot contain
             * content or trailers."
             *
             * The 412 case cannot arise here, and that is not an
             * oversight: the only method that selects a representation
             * is GET or HEAD. POST is only supported on /echo, whose
             * representation is the request's own body and so has no
             * validator, and POST / is a 405 answered before this
             * point. RFC 9110 13.2.1 says a server "MUST ignore the
             * conditional request header fields ... when received with
             * a request method that does not involve the selection or
             * modification of a selected representation". */
            if (strcmp(req.target, "/") == 0) {
                char etag[HTTP_ETAG_MAX];
                const char *inm;

                body = route_document(routes, route_count, host_name, default_body);
                blen = strlen(body);
                /* The validator for the representation. It is weak, and
                 * it says so: a hash of the bytes is not collision-free,
                 * so it is not a strong validator (RFC 9110 8.8.1). */
                if (http_etag_weak(etag, sizeof(etag), body, blen) < 0) {
                    return -1;
                }
                inm = http_header_get(&req, "If-None-Match");
                if (inm != NULL && http_if_none_match_matches(inm, etag)) {
                    n = http_response_build_full(resp, sizeof(resp), 304,
                                                 "Not Modified", "", NULL,
                                                 HTTP_NO_LENGTH, keep, date,
                                                 NULL, etag);
                } else {
                    n = http_response_build_full(resp, sizeof(resp), 200, "OK",
                                                 "text/html", body, blen, keep,
                                                 date, NULL, etag);
                }
            } else {
                n = http_response_build(resp, sizeof(resp), 404, "Not Found",
                                        "text/plain", "404 Not Found\n", 14,
                                        keep, date);
            }
            if (n < 0) {
                return -1;
            }
            to_send = response_bytes_to_send(resp, (size_t)n, is_head);
            if (to_send == 0) {
                return -1;
            }
            if (sock_write_exact(fd, resp, to_send, &wr) != 0) {
                return -1;
            }
            if (wr.status != SOCK_IO_COMPLETE) {
                return -1;
            }
            served++;
            if (st) {
                st->requests++;
                st->bytes_out += (unsigned long)to_send;
            }
            if (!keep) {
                return served;
            }
        }

        if (ps == HTTP_PARSE_ERROR) {
            /* The request will never be served. Say so and hang up.
             *
             * Two reasons, two answers. A request we cannot parse or
             * cannot trust is 400: the request is wrong. A request
             * carrying a transfer coding we do not implement is 501:
             * the request is fine, this server is not capable (RFC
             * 9112 6.1). The reason travels out of the parser in
             * errno, and it is read HERE, before anything else can
             * clobber it. */
            char date[64];
            int n;
            sock_io_result wr;
            int code = 400;
            const char *reason = "Bad Request";
            const char *body = "400 Bad Request\n";
            size_t blen = 16;

            if (errno == ENOTSUP) {
                code = 501;
                reason = "Not Implemented";
                body = "501 Not Implemented\n";
                blen = 20;
            }

            if (http_date_format(time(NULL), date, sizeof(date)) < 0) {
                return -1;
            }
            n = http_response_build(resp, sizeof(resp), code, reason,
                                    "text/plain", body, blen, 0, date);
            if (n > 0) {
                (void)sock_write_exact(fd, resp, (size_t)n, &wr);
            }
            return -1;
        }

        {
            sock_io_result rr;
            if (sock_read_some(fd, chunk, sizeof(chunk), &rr) != 0) {
                return -1;
            }
            if (rr.status == SOCK_IO_EOF) {
                return served;         /* the peer went away */
            }
            if (rr.status == SOCK_IO_ERROR) {
                return -1;
            }
            if (rr.status == SOCK_IO_INTERRUPTED) {
                continue;
            }
            if (rr.status == SOCK_IO_WOULD_BLOCK) {
                return served;         /* nothing right now */
            }
            if (rr.transferred == 0) {
                continue;
            }
            if (http_parser_feed(&p, chunk, rr.transferred) != 0) {
                return -1;
            }
        }
    }
}

/* Serve HTTP requests on an already-accepted connection until the peer
 * closes or asks to close. The server answers "/" with root_body and
 * everything else with 404. The Host header is ignored: this server
 * has one document, so there is nothing to route. */
int http_serve_connection(int fd, const char *root_body, http_server_stats *st)
{
    return serve_connection(fd, NULL, 0, root_body, 0, st);
}

/* Serve HTTP requests, choosing the document by the Host header. */
int http_serve_connection_routed(int fd, const http_host_route *routes,
                                  size_t route_count, const char *default_body,
                                  http_server_stats *st)
{
    return serve_connection(fd, routes, route_count, default_body, 1, st);
}

/* ---- Stage 65.11: chunked transfer-encoding, the decoder ---- */

static int hexval(int ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

/* The offset of the CR that ends the first line at or after `from`, or
 * (size_t)-1 when that line has not arrived yet. */
static size_t find_crlf(const char *buf, size_t len, size_t from)
{
    size_t i;

    for (i = from; i + 1 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n') {
            return i;
        }
    }
    return (size_t)-1;
}

void http_chunk_decoder_init(http_chunk_decoder *d)
{
    d->len = 0;
    d->pos = 0;
    d->done = 0;
}

size_t http_chunk_decoder_pending(const http_chunk_decoder *d, const char **out)
{
    if (d == NULL || out == NULL) {
        return 0;
    }
    *out = d->buf + d->pos;
    return d->len - d->pos;
}

int http_chunk_decoder_feed(http_chunk_decoder *d, const void *data, size_t len)
{
    if (!d || (!data && len > 0)) {
        errno = EINVAL;
        return -1;
    }
    /* Drop what has already been decoded, so the buffer holds only what
     * is still owed. */
    if (d->pos > 0) {
        memmove(d->buf, d->buf + d->pos, d->len - d->pos);
        d->len -= d->pos;
        d->pos = 0;
    }
    if (len > sizeof(d->buf) - d->len) {
        errno = ENOBUFS;
        return -1;
    }
    if (len > 0) {
        memcpy(d->buf + d->len, data, len);
        d->len += len;
    }
    return 0;
}

http_chunk_status http_chunk_decoder_next(http_chunk_decoder *d, char *out,
                                           size_t out_cap, size_t *out_len)
{
    size_t avail, line_end, hdr, size, i;
    unsigned long value = 0;
    int digits = 0;

    if (!d || !out_len) {
        errno = EINVAL;
        return HTTP_CHUNK_ERROR;
    }
    *out_len = 0;
    if (d->done) {
        return HTTP_CHUNK_DONE;
    }
    avail = d->len - d->pos;
    if (avail == 0) {
        return HTTP_CHUNK_NEED_MORE;
    }

    /* The chunk size line: hexadecimal, then optional extensions, then
     * CRLF. The extensions are ignored -- but they have to be skipped
     * over, or the size parse sees a ';' and stops too early. */
    line_end = find_crlf(d->buf, d->len, d->pos);
    if (line_end == (size_t)-1) {
        return HTTP_CHUNK_NEED_MORE;
    }
    for (i = d->pos; i < line_end; i++) {
        int v;

        if (d->buf[i] == ';') {
            break;
        }
        v = hexval((unsigned char)d->buf[i]);
        if (v < 0) {
            errno = EINVAL;
            return HTTP_CHUNK_ERROR;
        }
        if (value > (ULONG_MAX - (unsigned long)v) / 16UL) {
            errno = EINVAL;
            return HTTP_CHUNK_ERROR;
        }
        value = value * 16UL + (unsigned long)v;
        digits++;
    }
    if (digits == 0) {
        errno = EINVAL;
        return HTTP_CHUNK_ERROR;
    }
    size = (size_t)value;
    hdr = line_end + 2 - d->pos;

    if (size == 0) {
        /* The terminal chunk. What follows may be trailers, and the
         * trailers end with a blank line. */
        size_t after = d->pos + hdr;

        for (;;) {
            size_t tline = find_crlf(d->buf, d->len, after);

            if (tline == (size_t)-1) {
                return HTTP_CHUNK_NEED_MORE;
            }
            if (tline == after) {
                after = tline + 2;
                break;
            }
            after = tline + 2;
        }
        d->pos = after;
        d->done = 1;
        return HTTP_CHUNK_DONE;
    }

    /* The payload, then the CRLF that ends it. */
    if (avail < hdr + size + 2) {
        return HTTP_CHUNK_NEED_MORE;
    }
    if (size > out_cap) {
        errno = ENOBUFS;
        return HTTP_CHUNK_ERROR;
    }
    if (d->buf[d->pos + hdr + size] != '\r' ||
        d->buf[d->pos + hdr + size + 1] != '\n') {
        errno = EINVAL;
        return HTTP_CHUNK_ERROR;
    }
    memcpy(out, d->buf + d->pos + hdr, size);
    *out_len = size;
    d->pos += hdr + size + 2;
    return HTTP_CHUNK_OK;
}

/* ---- Stage 65.12: chunked transfer-encoding, the encoder ---- */

int http_chunk_encode(const void *data, size_t len, char *out, size_t out_cap)
{
    char head[HTTP_MAX_CHUNK_LINE];
    size_t need;
    int hn;

    if (!out || out_cap == 0 || !data) {
        errno = EINVAL;
        return -1;
    }
    if (len == 0) {
        /* A zero-length chunk is the terminal chunk. Writing one here
         * would end the body by accident. */
        errno = EINVAL;
        return -1;
    }
    hn = snprintf(head, sizeof(head), "%zx\r\n", len);
    if (hn < 0 || (size_t)hn >= sizeof(head)) {
        errno = EINVAL;
        return -1;
    }
    need = (size_t)hn + len + 2;
    if (need > out_cap) {
        errno = ENOSPC;
        return -1;
    }
    memcpy(out, head, (size_t)hn);
    memcpy(out + hn, data, len);
    out[hn + len] = '\r';
    out[hn + len + 1] = '\n';
    return (int)need;
}

int http_chunk_encoder_finish(char *out, size_t out_cap)
{
    if (!out || out_cap < 5) {
        errno = ENOSPC;
        return -1;
    }
    memcpy(out, "0\r\n\r\n", 5);
    return 5;
}

int http_response_build_chunked(char *dst, size_t dst_len, int status,
                                const char *reason, const char *content_type,
                                int keep_alive, const char *date)
{
    int n;

    if (!dst || dst_len == 0 || !date) {
        errno = EINVAL;
        return -1;
    }
    n = snprintf(dst, dst_len,
                 "HTTP/1.1 %d %s\r\n"
                 "Date: %s\r\n"
                 "Content-Type: %s\r\n"
                 "Transfer-Encoding: chunked\r\n"
                 "Connection: %s\r\n"
                 "\r\n",
                 status, reason ? reason : "",
                 date,
                 content_type ? content_type : "text/plain",
                 keep_alive ? "keep-alive" : "close");
    if (n < 0) {
        errno = EINVAL;
        return -1;
    }
    if ((size_t)n >= dst_len) {
        errno = ENOBUFS;
        return -1;
    }
    return n;
}
