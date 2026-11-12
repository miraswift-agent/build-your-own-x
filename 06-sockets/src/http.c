/*
 * Stage 65.6 -- HTTP/1.1 from the wire. See http.h for the lesson.
 */

#include "http.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sockets.h"

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

static int parse_content_length(http_request *r)
{
    const char *v = http_header_get(r, "Content-Length");
    char *end;
    long n;

    r->content_length = -1;
    if (!v) {
        return 0;                /* absent: no body */
    }
    if (*v == '\0') {
        return -1;
    }
    errno = 0;
    n = strtol(v, &end, 10);
    if (errno != 0 || end == v || *end != '\0' || n < 0) {
        return -1;               /* not a number: a broken request */
    }
    if (n > HTTP_MAX_BODY) {
        return -1;               /* bigger than we are willing to hold */
    }
    r->content_length = n;
    return 0;
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
    if (parse_content_length(out) != 0) {
        errno = EPROTO;
        return HTTP_PARSE_ERROR;
    }

    body_start = (size_t)hend + 4;
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

int http_response_build(char *dst, size_t dst_len, int status, const char *reason,
                        const char *content_type, const char *body,
                        size_t body_len, int keep_alive)
{
    int n;

    if (!dst || dst_len == 0) {
        errno = EINVAL;
        return -1;
    }
    n = snprintf(dst, dst_len,
                 "HTTP/1.1 %d %s\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: %s\r\n"
                 "\r\n",
                 status, reason ? reason : "",
                 content_type ? content_type : "text/plain",
                 body_len, keep_alive ? "keep-alive" : "close");
    if (n < 0) {
        return -1;
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

int http_serve_connection(int fd, const char *root_body, http_server_stats *st)
{
    http_parser p;
    http_request req;
    http_parse_status ps;
    char chunk[4096];
    char resp[HTTP_MAX_REQUEST];
    int served = 0;

    http_parser_init(&p);
    if (st) {
        st->requests = 0;
        st->bytes_out = 0;
    }

    for (;;) {
        while ((ps = http_parser_next(&p, &req)) == HTTP_PARSE_OK) {
            int keep = http_request_keep_alive(&req);
            const char *body;
            const char *ctype;
            const char *reason;
            size_t blen;
            int code;
            int n;
            sock_io_result wr;

            if (strcmp(req.target, "/") == 0) {
                code = 200;
                reason = "OK";
                ctype = "text/html";
                body = root_body ? root_body : "";
            } else {
                code = 404;
                reason = "Not Found";
                ctype = "text/plain";
                body = "404 Not Found\n";
            }
            blen = strlen(body);

            n = http_response_build(resp, sizeof(resp), code, reason, ctype,
                                    body, blen, keep);
            if (n < 0) {
                return -1;
            }
            if (sock_write_exact(fd, resp, (size_t)n, &wr) != 0) {
                return -1;
            }
            if (wr.status != SOCK_IO_COMPLETE) {
                return -1;
            }
            served++;
            if (st) {
                st->requests++;
                st->bytes_out += (unsigned long)n;
            }
            if (!keep) {
                return served;
            }
        }

        if (ps == HTTP_PARSE_ERROR) {
            /* The request will never be valid. Say so and hang up. */
            int n = http_response_build(resp, sizeof(resp), 400, "Bad Request",
                                        "text/plain", "400 Bad Request\n", 16, 0);
            sock_io_result wr;
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
