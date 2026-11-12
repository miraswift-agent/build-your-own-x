/*
 * Stage 65.9 -- DNS from scratch, second half: the transport and the cache.
 *
 * Stage 65.8 built the packets. This stage sends them and remembers them.
 *
 * The lesson: **a lookup is an UDP exchange, not a function call.**
 *
 * Every assumption that a function call invites has to be earned here:
 *
 *   - the answer may never arrive (a timeout is a result, not a hang);
 *   - the answer may arrive from somebody else (a datagram from the
 *     wrong source is not our answer);
 *   - the answer may be for a different query (the id has to match);
 *   - the answer may be well-formed and still say no (NXDOMAIN, or
 *     NOERROR with nothing in it);
 *   - and the answer may be *truncated*, which is not an error but a
 *     request to come back over TCP -- which this stage does not do,
 *     and says so.
 *
 * The second half of the lesson is the cache: once the answer exists,
 * the next lookup for the same name should not touch the network at
 * all. A cache makes time a first-class input, which is why the clock
 * here is injected rather than read from the library: the caller says
 * what time it is, and the tests get to say it too.
 */

#ifndef DNS_RESOLVER_H
#define DNS_RESOLVER_H

#include "dns.h"
#include "sockets.h"

#include <stddef.h>
#include <stdint.h>

/* How many answers the cache holds at once. */
#define DNS_CACHE_MAX 32

/* The largest rdata we keep. An A record is 4 bytes; an AAAA is 16. */
#define DNS_MAX_RDATA 16

/* The answer to a lookup: the first record that matched the question. */
typedef struct {
    char          name[DNS_MAX_NAME];  /* the owner name of the answer */
    uint16_t      type;
    uint32_t      ttl;                 /* the TTL remaining, on a cache hit */
    size_t        rdlength;
    unsigned char rdata[DNS_MAX_RDATA];
    int           from_cache;          /* 1 if this came from the cache */
    uint16_t      rcode;               /* the server's rcode, even on error */
} dns_answer;

/* A clock, injected.
 *
 * Monotonic seconds -- not wall-clock time. A cache whose expiry
 * depends on the wall clock can be made to expire by an NTP step or
 * by daylight saving, and the failure is silent: entries that should
 * be fresh look stale, or the other way round. */
typedef uint64_t (*dns_clock_fn)(void *ud);

/* One cached answer. */
typedef struct {
    char          name[DNS_MAX_NAME];
    uint16_t      type;
    uint16_t      class_;
    uint32_t      ttl;                 /* the TTL as the server gave it */
    uint64_t      expires_at;          /* monotonic seconds */
    size_t        rdlength;
    unsigned char rdata[DNS_MAX_RDATA];
    int           in_use;
} dns_cache_entry;

/* A resolver: one server, one socket, one cache. */
typedef struct {
    sock_ipv4_bytes server;            /* the server's address, built by hand */
    uint16_t        server_port;
    int             timeout_ms;
    dns_clock_fn    clock;
    void           *clock_ud;
    uint32_t        rng;               /* the query-id generator's state */
    int             fd;               /* the UDP socket, opened lazily */
    unsigned long   queries_sent;
    unsigned long   cache_hits;
    unsigned long   cache_misses;
    dns_cache_entry entries[DNS_CACHE_MAX];
} dns_resolver;

/* Set up a resolver.
 *
 * server_ip is a dotted quad ("127.0.0.53"), parsed by hand. port is a
 * host-order port number; the bytes are spelled out here. clock is the
 * clock the cache ages against -- pass NULL and every timestamp is 0
 * (which is useful for tests that do not care about expiry).
 * Returns DNS_OK, or DNS_ERR_FORMAT for an address that will not parse. */
dns_status dns_resolver_init(dns_resolver *r, const char *server_ip,
                              uint16_t port, int timeout_ms,
                              dns_clock_fn clock, void *clock_ud);

/* Close the resolver's socket. Safe to call twice. */
void dns_resolver_close(dns_resolver *r);

/* Resolve a name.
 *
 * The whole exchange: check the cache, build the query, send it, wait
 * for the answer, check that the answer is actually ours, and cache it.
 * Returns DNS_OK with *out filled, or one of the DNS_ERR_* statuses:
 *
 *   DNS_ERR_NOTFOUND   the server said NOERROR but had no matching answer
 *   DNS_ERR_NXDOMAIN   the name does not exist
 *   DNS_ERR_RCODE      the server refused (SERVFAIL, REFUSED, ...)
 *   DNS_ERR_TIMEOUT    no answer arrived in time
 *   DNS_ERR_ID_MISMATCH the answer is for a different query
 *   DNS_ERR_QUESTION   the answer is for a different question
 *   DNS_ERR_SOURCE     the answer came from somewhere else
 *   DNS_ERR_TRUNCATED  the answer was cut off (the TCP retry is out of
 *                      scope for this stage)
 *   DNS_ERR_IO         the socket failed
 *
 * *out is always safe to read afterwards: on an error it carries the
 * rcode (if a response arrived at all) and zeroes elsewhere. */
dns_status dns_resolve(dns_resolver *r, const char *name, uint16_t type,
                        dns_answer *out);

/* ------------------------------------------------------------------ */
/* The cache, on its own, with the clock passed in.                     */
/* ------------------------------------------------------------------ */

/* Look an answer up. now is the current time in the resolver's clock.
 * Returns DNS_OK with *out filled, or DNS_ERR_NOTFOUND.
 *
 * An entry whose expiry has passed is a miss AND is dropped: a cache
 * that only forgets on a write is a cache that keeps dead entries
 * forever. */
dns_status dns_cache_lookup(dns_resolver *r, const char *name, uint16_t type,
                             uint64_t now, dns_answer *out);

/* Store an answer. A ttl of 0 stores an entry that is already expired,
 * which is what "do not cache this" means. */
dns_status dns_cache_store(dns_resolver *r, const char *name, uint16_t type,
                            uint16_t class_, const unsigned char *rdata,
                            size_t rdlength, uint32_t ttl, uint64_t now);

/* How many entries are live (in use and not expired) at time now. */
size_t dns_cache_count(const dns_resolver *r, uint64_t now);

/* Drop everything. */
void dns_cache_clear(dns_resolver *r);

#endif /* DNS_RESOLVER_H */
