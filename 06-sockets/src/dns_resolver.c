/*
 * Stage 65.9 -- DNS from scratch, second half: the transport and the cache.
 *
 * See dns_resolver.h for the lesson this stage is about: a lookup is an
 * UDP exchange, not a function call.
 *
 * Nothing in this file calls a library address helper -- the component's
 * Stage 65.2 rule, enforced by a guard test that greps src/ for the
 * names of those helpers (including in comments). The server's address
 * is assembled by hand with the component's own helpers.
 */

#include "dns_resolver.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

/* DNS names are case-insensitive: "Example.COM" and "example.com" are
 * the same name, and a cache that treats them as different keys serves
 * the same answer twice. */
static int name_equal(const char *a, const char *b)
{
    size_t i;

    for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
        char ca = a[i];
        char cb = b[i];

        if (ca >= 'A' && ca <= 'Z') { ca = (char)(ca - 'A' + 'a'); }
        if (cb >= 'A' && cb <= 'Z') { cb = (char)(cb - 'A' + 'a'); }
        if (ca != cb) {
            return 0;
        }
    }
    return a[i] == '\0' && b[i] == '\0';
}

/* Copy a name into a fixed buffer, always NUL-terminated. */
static void copy_name(char *dst, size_t dst_len, const char *src)
{
    size_t n = strlen(src);

    if (n >= dst_len) {
        n = dst_len - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ------------------------------------------------------------------ */
/* The cache                                                           */
/* ------------------------------------------------------------------ */

static int cache_find(const dns_resolver *r, const char *name, uint16_t type)
{
    int i;

    for (i = 0; i < DNS_CACHE_MAX; i++) {
        const dns_cache_entry *e = &r->entries[i];

        if (!e->in_use) {
            continue;
        }
        if (e->type != type) {
            continue;
        }
        if (!name_equal(e->name, name)) {
            continue;
        }
        return i;
    }
    return -1;
}

dns_status dns_cache_lookup(dns_resolver *r, const char *name, uint16_t type,
                             uint64_t now, dns_answer *out)
{
    int idx;
    dns_cache_entry *e;

    if (out == NULL) {
        return DNS_ERR_FORMAT;
    }
    memset(out, 0, sizeof *out);

    idx = cache_find(r, name, type);
    if (idx < 0) {
        return DNS_ERR_NOTFOUND;
    }
    e = &r->entries[idx];
    if (now >= e->expires_at) {
        /* Lazy expiry: the entry is dead, and this is the moment to
         * notice it. A cache that only forgets on a write keeps dead
         * entries until the next write happens to need the room. */
        e->in_use = 0;
        return DNS_ERR_NOTFOUND;
    }

    copy_name(out->name, sizeof out->name, e->name);
    out->type = e->type;
    out->ttl = (uint32_t)(e->expires_at - now);   /* what is LEFT of it */
    out->rdlength = e->rdlength;
    if (e->rdlength > 0) {
        memcpy(out->rdata, e->rdata, e->rdlength);
    }
    out->rcode = DNS_RCODE_NOERROR;
    return DNS_OK;
}

dns_status dns_cache_store(dns_resolver *r, const char *name, uint16_t type,
                            uint16_t class_, const unsigned char *rdata,
                            size_t rdlength, uint32_t ttl, uint64_t now)
{
    int i;
    int slot = -1;
    int victim = -1;
    dns_cache_entry *e;

    if (name == NULL || rdata == NULL) {
        return DNS_ERR_FORMAT;
    }
    if (rdlength > DNS_MAX_RDATA) {
        return DNS_ERR_NOSPACE;
    }

    /* An entry for the same key is replaced, not duplicated. */
    {
        int idx = cache_find(r, name, type);

        if (idx >= 0) {
            e = &r->entries[idx];
            goto fill;
        }
    }

    /* A free slot. */
    for (i = 0; i < DNS_CACHE_MAX; i++) {
        if (!r->entries[i].in_use) {
            slot = i;
            break;
        }
    }

    /* No free slot: drop what has already expired before dropping
     * anything still good. */
    if (slot < 0) {
        for (i = 0; i < DNS_CACHE_MAX; i++) {
            e = &r->entries[i];
            if (e->in_use && now >= e->expires_at) {
                e->in_use = 0;
            }
        }
        for (i = 0; i < DNS_CACHE_MAX; i++) {
            if (!r->entries[i].in_use) {
                slot = i;
                break;
            }
        }
    }

    /* Still full: evict the entry that expires soonest. */
    if (slot < 0) {
        uint64_t soonest = 0;

        for (i = 0; i < DNS_CACHE_MAX; i++) {
            e = &r->entries[i];
            if (victim < 0 || e->expires_at < soonest) {
                victim = i;
                soonest = e->expires_at;
            }
        }
        slot = victim;
        r->entries[slot].in_use = 0;
    }

    e = &r->entries[slot];

fill:
    copy_name(e->name, sizeof e->name, name);
    e->type = type;
    e->class_ = class_;
    e->ttl = ttl;
    e->expires_at = now + (uint64_t)ttl;
    e->rdlength = rdlength;
    if (rdlength > 0) {
        memcpy(e->rdata, rdata, rdlength);
    }
    e->in_use = 1;
    return DNS_OK;
}

size_t dns_cache_count(const dns_resolver *r, uint64_t now)
{
    size_t n = 0;
    int i;

    for (i = 0; i < DNS_CACHE_MAX; i++) {
        const dns_cache_entry *e = &r->entries[i];

        if (e->in_use && now < e->expires_at) {
            n++;
        }
    }
    return n;
}

void dns_cache_clear(dns_resolver *r)
{
    memset(r->entries, 0, sizeof r->entries);
}

/* ------------------------------------------------------------------ */
/* The resolver                                                        */
/* ------------------------------------------------------------------ */

/* The query id. A real resolver picks these at random so that a
 * spoofed answer cannot guess which query it is answering; here they
 * come from a small generator seeded with the clock, which is enough
 * to make every answer distinguishable from every other answer. */
static uint16_t next_id(dns_resolver *r)
{
    uint32_t x = r->rng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->rng = x;
    return (uint16_t)(x >> 16);
}

dns_status dns_resolver_init(dns_resolver *r, const char *server_ip,
                              uint16_t port, int timeout_ms,
                              dns_clock_fn clock, void *clock_ud)
{
    uint64_t seed;

    if (r == NULL || server_ip == NULL) {
        return DNS_ERR_FORMAT;
    }
    memset(r, 0, sizeof *r);
    r->fd = -1;

    if (sock_ipv4_from_dotted(server_ip, r->server.ip) != 0) {
        return DNS_ERR_FORMAT;
    }
    /* The port is two bytes, high byte first. */
    r->server.port[0] = (uint8_t)(port >> 8);
    r->server.port[1] = (uint8_t)(port & 0xFF);
    r->server_port = port;
    r->timeout_ms = timeout_ms;
    r->clock = clock;
    r->clock_ud = clock_ud;

    seed = clock != NULL ? clock(clock_ud) : 1;
    r->rng = (uint32_t)(seed ^ 0x9E3779B9u) | 1u;
    return DNS_OK;
}

void dns_resolver_close(dns_resolver *r)
{
    if (r == NULL || r->fd < 0) {
        return;
    }
    close(r->fd);
    r->fd = -1;
}

/* The UDP socket, opened on first use. */
static int resolver_socket(dns_resolver *r)
{
    int fd;

    if (r->fd >= 0) {
        return r->fd;
    }
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }
    r->fd = fd;
    return fd;
}

/* Did this datagram come from the server we asked? */
static int source_matches(const dns_resolver *r, const struct sockaddr_in *from)
{
    sock_ipv4_bytes b;

    if (sock_addr_extract(from, &b) != 0) {
        return 0;
    }
    if (memcmp(b.ip, r->server.ip, sizeof b.ip) != 0) {
        return 0;
    }
    return sock_port_from_bytes(b.port) == r->server_port;
}

dns_status dns_resolve(dns_resolver *r, const char *name, uint16_t type,
                        dns_answer *out)
{
    uint64_t now;
    uint16_t id;
    unsigned char query[DNS_MAX_MSG];
    unsigned char resp[DNS_MAX_MSG];
    size_t qlen = 0;
    struct sockaddr_in server_addr;
    struct sockaddr_in from;
    socklen_t from_len;
    struct pollfd pfd;
    ssize_t sent;
    ssize_t n;
    int fd;
    int pr;
    dns_status st;
    dns_header h;
    dns_question q;
    size_t off;
    uint16_t i;

    if (r == NULL || name == NULL || out == NULL) {
        return DNS_ERR_FORMAT;
    }
    memset(out, 0, sizeof *out);

    now = r->clock != NULL ? r->clock(r->clock_ud) : 0;

    /* 1. The cache. A fresh entry is the whole point of having one. */
    if (dns_cache_lookup(r, name, type, now, out) == DNS_OK) {
        r->cache_hits++;
        out->from_cache = 1;
        return DNS_OK;
    }
    r->cache_misses++;

    /* 2. The query. */
    id = next_id(r);
    st = dns_build_query(name, type, id, query, sizeof query, &qlen);
    if (st != DNS_OK) {
        return st;
    }

    /* 3. The socket. */
    fd = resolver_socket(r);
    if (fd < 0) {
        return DNS_ERR_IO;
    }
    if (sock_addr_apply(&r->server, &server_addr) != 0) {
        return DNS_ERR_IO;
    }

    /* 4. Send. A datagram is all or nothing: a short send is a
     * failure, not a partial transfer to be resumed. */
    sent = sendto(fd, query, qlen, 0,
                  (const struct sockaddr *)&server_addr, sizeof server_addr);
    if (sent < 0 || (size_t)sent != qlen) {
        return DNS_ERR_IO;
    }
    r->queries_sent++;

    /* 5. Wait. A timeout is an outcome, not a failure of the program. */
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    for (;;) {
        pr = poll(&pfd, 1, r->timeout_ms);
        if (pr < 0 && errno == EINTR) {
            /* A signal arrived. The wait is retryable, and the timeout
             * starts over -- which is the conservative choice: an
             * interrupted wait is not a shorter wait. */
            continue;
        }
        break;
    }
    if (pr < 0) {
        return DNS_ERR_IO;
    }
    if (pr == 0) {
        return DNS_ERR_TIMEOUT;
    }

    /* 6. Read the answer. */
    from_len = sizeof from;
    n = recvfrom(fd, resp, sizeof resp, 0,
                 (struct sockaddr *)&from, &from_len);
    if (n < 0) {
        return DNS_ERR_IO;
    }

    /* 7. Whose answer is it? A datagram from the wrong source is not
     * our answer, however well-formed it is. */
    if (!source_matches(r, &from)) {
        return DNS_ERR_SOURCE;
    }

    /* 8. The header. */
    st = dns_parse_header(resp, (size_t)n, &h);
    if (st != DNS_OK) {
        return st;
    }
    if (!dns_is_response(&h)) {
        /* A query came back. That is not an answer to anything. */
        return DNS_ERR_FORMAT;
    }
    if (h.id != id) {
        /* The answer is for a different query. */
        return DNS_ERR_ID_MISMATCH;
    }
    if (dns_is_truncated(&h)) {
        /* The answer did not fit in a datagram. The real answer is on
         * TCP, which this stage does not do -- so this is reported,
         * not guessed at. */
        return DNS_ERR_TRUNCATED;
    }

    out->rcode = dns_rcode(&h);
    if (out->rcode == DNS_RCODE_NXDOMAIN) {
        return DNS_ERR_NXDOMAIN;
    }
    if (out->rcode != DNS_RCODE_NOERROR) {
        return DNS_ERR_RCODE;
    }

    /* 9. The question. The answer has to be about the thing we asked. */
    off = DNS_HEADER_LEN;
    st = dns_parse_question(resp, (size_t)n, off, &q, &off);
    if (st != DNS_OK) {
        return st;
    }
    if (!name_equal(q.name, name) || q.type != type) {
        return DNS_ERR_QUESTION;
    }

    /* 10. The answers. */
    for (i = 0; i < h.ancount; i++) {
        dns_record rr;

        st = dns_parse_record(resp, (size_t)n, off, &rr, &off);
        if (st != DNS_OK) {
            return st;
        }
        if (rr.type != type) {
            continue;
        }
        if (rr.rdlength > DNS_MAX_RDATA) {
            continue;
        }
        copy_name(out->name, sizeof out->name, rr.name);
        out->type = rr.type;
        out->ttl = rr.ttl;
        out->rdlength = rr.rdlength;
        if (rr.rdlength > 0) {
            memcpy(out->rdata, rr.rdata, rr.rdlength);
        }

        /* 11. The cache, under the name that was ASKED for -- a CNAME
         * chain still answers the question that was asked. */
        (void)dns_cache_store(r, name, type, rr.class_, rr.rdata,
                              rr.rdlength, rr.ttl, now);
        return DNS_OK;
    }

    /* NOERROR, the right question, and nothing in the answer section. */
    return DNS_ERR_NOTFOUND;
}
