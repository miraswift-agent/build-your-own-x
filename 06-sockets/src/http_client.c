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

#include "http_client.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>

#include "sockets.h"

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

/* The Content-Length, or -1 when the header is absent. A Content-Length
 * that is not a number is a protocol error, not a missing header. */
static int header_content_length(const http_response *r, long *out)
{
    const char *v = http_response_header_get(r, "Content-Length");
    const char *p;
    long value;

    if (v == NULL) {
        *out = -1;
        return 0;
    }
    if (*v == '\0') {
        return -1;
    }
    value = 0;
    for (p = v; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        value = value * 10L + (*p - '0');
        if (value > 100000000L) {
            return -1;
        }
    }
    *out = value;
    return 0;
}

/* Whether the response's final transfer-coding is "chunked".
 *
 * The header is a comma-separated list of codings, and the LAST one is
 * the coding that framed the body: "gzip, chunked" is a chunked body
 * inside a gzip coding. */
static int header_is_chunked(const http_response *r)
{
    const char *v = http_response_header_get(r, "Transfer-Encoding");
    const char *end;
    size_t len;

    if (v == NULL) {
        return 0;
    }
    /* Walk to the last coding. */
    for (;;) {
        const char *comma = strchr(v, ',');
        if (comma == NULL) {
            break;
        }
        v = comma + 1;
    }
    while (*v == ' ' || *v == '\t') {
        v++;
    }
    end = v + strlen(v);
    while (end > v && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    len = (size_t)(end - v);
    return len == 7 && strncasecmp(v, "chunked", 7) == 0;
}

/* ---------- the head: status line, headers, and where the body starts ---------- */

static http_parse_status parse_head(http_response_parser *p, http_response *out,
                                    size_t *body_start)
{
    size_t line_end;
    size_t blank;
    size_t hdr_start;
    size_t hdr_end;
    long cl;

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
        errno = EPROTO;
        return HTTP_PARSE_ERROR;
    }
    out->content_length = cl;
    out->framed_by_chunked = header_is_chunked(out);
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
        /* The terminal chunk: the body is complete. */
        memcpy(out->body, p->decoded, p->decoded_len);
        out->body[p->decoded_len] = '\0';
        out->body_len = p->decoded_len;
        out->framed_by_length = 0;
        out->framed_by_chunked = 1;
        p->len = 0;
        p->body_started = 0;
        p->body_fed_upto = 0;
        return HTTP_PARSE_OK;
    }
}

/* ---------- the parser ---------- */

void http_response_parser_init(http_response_parser *p)
{
    p->len = 0;
    p->body_started = 0;
    p->body_start = 0;
    p->body_fed_upto = 0;
    p->decoded_len = 0;
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
        /* The peer closed and we never got a status line. That is not a
         * short response; there was no response. */
        errno = EPROTO;
        return HTTP_PARSE_ERROR;
    }
    if (st != HTTP_PARSE_OK) {
        return st;
    }
    if (out->framed_by_chunked) {
        /* The peer closed before the terminal chunk. A chunked body that
         * stops is truncated, not short. */
        errno = EPROTO;
        return HTTP_PARSE_ERROR;
    }
    if (out->content_length >= 0) {
        /* The peer closed before the promised bytes arrived. That is a
         * truncated response, not a short one. */
        errno = EPROTO;
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

int http_client_request_build(char *dst, size_t dst_len,
                              const char *method, const char *target,
                              const char *host, const char *body, size_t body_len)
{
    int n;
    size_t need;

    if (dst == NULL || method == NULL || target == NULL || host == NULL ||
        (body == NULL && body_len > 0)) {
        errno = EINVAL;
        return -1;
    }
    need = strlen(method) + strlen(target) + strlen(host) + 128;
    if (body_len > 0) {
        need += body_len + 32;
    }
    if (dst_len < need) {
        errno = ENOBUFS;
        return -1;
    }
    n = snprintf(dst, dst_len,
                 "%s %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: mira-http/1.0\r\n"
                 "Connection: close\r\n",
                 method, target, host);
    if (n < 0 || (size_t)n >= dst_len) {
        errno = ENOBUFS;
        return -1;
    }
    if (body_len > 0) {
        int m = snprintf(dst + n, dst_len - (size_t)n,
                         "Content-Length: %zu\r\n\r\n", body_len);
        if (m < 0 || (size_t)(n + m) >= dst_len) {
            errno = ENOBUFS;
            return -1;
        }
        n += m;
        memcpy(dst + n, body, body_len);
        n += (int)body_len;
    } else {
        int m = snprintf(dst + n, dst_len - (size_t)n, "\r\n");
        if (m < 0 || (size_t)(n + m) >= dst_len) {
            errno = ENOBUFS;
            return -1;
        }
        n += m;
    }
    return n;
}

/* ---------- one round trip ---------- */

int http_client_get(const char *host, uint16_t port, const char *target,
                    http_response *out)
{
    uint8_t ip[4];
    sock_ipv4_bytes b;
    struct sockaddr_in addr;
    int fd;
    char req[HTTP_CLIENT_MAX_REQUEST];
    char buf[4096];
    int reqlen;
    size_t sent = 0;
    http_response_parser p;
    http_parse_status st = HTTP_PARSE_NEED_MORE;

    if (host == NULL || target == NULL || out == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (sock_ipv4_from_dotted(host, ip) != 0) {
        return -1;
    }
    /* The port bytes come from the helper that already knows how to spell
     * them; the address is ours, octet by octet. */
    if (sock_ipv4_loopback_bytes(port, &b) != 0) {
        return -1;
    }
    memcpy(b.ip, ip, 4);
    if (sock_addr_apply(&b, &addr) != 0) {
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

    reqlen = http_client_request_build(req, sizeof(req), "GET", target, host, NULL, 0);
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
            /* The peer closed: an EOF-framed response is complete now. */
            st = http_response_parser_finish(&p, out);
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
            break;
        }
        if (st == HTTP_PARSE_ERROR) {
            int e = errno;
            close(fd);
            errno = e;
            return -1;
        }
    }
    close(fd);
    if (st != HTTP_PARSE_OK) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}
