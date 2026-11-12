/*
 * Stage 65.9 -- DNS from scratch, second half: the transport and the cache.
 *
 * The exchange is tested against a REAL socket and a REAL server: a
 * child process binds a UDP socket on loopback and answers queries on
 * it, the parent resolves names against it. Nothing is mocked -- the
 * packets go out, the packets come back, and the resolver has to be
 * right about all of it.
 *
 * The fake server can misbehave on purpose, because the second half of
 * the lesson is that a lookup can fail in more ways than "the name does
 * not exist":
 *
 *   mode 1  say nothing at all          -> the resolver must time out
 *   mode 2  answer with the wrong id    -> the answer is not ours
 *   mode 3  answer a different question -> the answer is not ours
 *   mode 4  set the TC bit              -> truncated, and TCP is out of scope
 *   mode 5  say NXDOMAIN                -> the name does not exist
 *   mode 6  say NOERROR with no answers -> nothing to find
 *   mode 7  answer from another socket  -> not from the server we asked
 *
 * Time is injected too: the cache's clock is a counter the test
 * advances by hand, so "the entry expired" is a fact rather than a
 * sleep.
 */

#include "../src/dns_resolver.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  %-58s", name); } while (0)
#define PASS() do { tests_passed++; printf("OK\n"); } while (0)
#define FAIL(msg) do { tests_failed++; printf("FAIL: %s\n", msg); } while (0)

/* ------------------------------------------------------------------ */
/* A clock the test owns                                              */
/* ------------------------------------------------------------------ */

static uint64_t fake_now = 1000;

static uint64_t fake_clock(void *ud)
{
    (void)ud;
    return fake_now;
}

static void clock_advance(uint64_t seconds)
{
    fake_now += seconds;
}

/* ------------------------------------------------------------------ */
/* The fake server                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    volatile int port;       /* the port the server is listening on */
    volatile int queries;    /* how many queries it has seen */
    volatile int mode;       /* how it should behave */
    volatile int query_ok;   /* 1 if the last query was well-formed */
    volatile int query_type; /* the type the last query asked for */
} fake_shared;

/* The shared state has to be REALLY shared: the parent and the forked
 * server are separate processes, so a struct on the parent's stack
 * would give the child its own copy -- and the child would never see
 * the stop flag, and the parent would wait for it forever. A shared
 * anonymous mapping is the same bytes in both processes. */
/* The mapping lives until the process exits: these are test fixtures
 * with a process lifetime, and valgrind's leak checker does not track
 * mapped memory (it tracks malloc'd memory). Freeing it mid-test would
 * be a use-after-free of the very state the assertions read. */
static fake_shared *shared_new(void)
{
    void *p = mmap(NULL, sizeof(fake_shared), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (p == MAP_FAILED) {
        return NULL;
    }
    memset(p, 0, sizeof(fake_shared));
    return (fake_shared *)p;
}

/* Write a 16-bit value, high byte first. */
static void put16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v >> 8);
    p[1] = (unsigned char)(v & 0xFF);
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

/* Build the answer to a query. The question is copied verbatim (except
 * in mode 3, where it is deliberately answered wrongly). */
static size_t build_response(const unsigned char *query, size_t qlen,
                              unsigned char *out, int mode)
{
    dns_header h;
    dns_question q;
    size_t off;
    uint16_t flags;
    uint16_t ancount = 1;
    size_t olen = qlen;

    memcpy(out, query, qlen);
    if (dns_parse_header(query, qlen, &h) != DNS_OK) {
        return 0;
    }
    off = DNS_HEADER_LEN;
    if (dns_parse_question(query, qlen, off, &q, &off) != DNS_OK) {
        return 0;
    }

    flags = (uint16_t)(h.flags | 0x8000);          /* QR: this is a response */
    if (mode == 4) {
        flags = (uint16_t)(flags | 0x0200);          /* TC: it was cut off */
    }
    if (mode == 5) {
        flags = (uint16_t)((flags & 0xFFF0) | DNS_RCODE_NXDOMAIN);
    }
    put16(out + 2, flags);
    put16(out + 4, 1);                              /* qdcount */
    put16(out + 6, (mode == 6) ? 0 : ancount);     /* ancount */
    put16(out + 8, 0);                              /* nscount */
    put16(out + 10, 0);                             /* arcount */

    if (mode == 2) {
        put16(out, (uint16_t)(h.id ^ 0xFFFF));      /* the wrong id */
    }
    if (mode == 3) {
        /* The question's TYPE is four bytes before its end (type, then
         * class). Answer a different question. */
        put16(out + off - 4, DNS_TYPE_AAAA);
    }
    if (mode == 6) {
        return olen;                               /* no answer records */
    }

    /* One answer: a pointer back to the question's name, then the type,
     * class, ttl, length and four bytes of address. */
    out[olen++] = 0xC0;
    out[olen++] = 0x0C;
    put16(out + olen, h.qdcount ? q.type : DNS_TYPE_A);
    olen += 2;
    put16(out + olen, DNS_CLASS_IN);
    olen += 2;
    put32(out + olen, 300);
    olen += 4;
    put16(out + olen, 4);
    olen += 2;
    out[olen++] = 93;    /* 93.184.216.34 */
    out[olen++] = 184;
    out[olen++] = 216;
    out[olen++] = 34;
    return olen;
}

/* The child: answer queries until told to stop. */
static void fake_server_loop(int fd, fake_shared *sh)
{
    unsigned char buf[DNS_MAX_MSG];
    unsigned char out[DNS_MAX_MSG + 32];
    struct sockaddr_storage from;
    socklen_t from_len;
    int alt = -1;

    for (;;) {
        ssize_t n;

        from_len = sizeof from;
        n = recvfrom(fd, buf, sizeof buf, 0,
                     (struct sockaddr *)&from, &from_len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (sh->mode == 99) {
            break;              /* the test is done with us -- and this
                                  * is the stop signal, not a query */
        }
        sh->queries++;
        if (sh->mode == 1) {
            continue;           /* say nothing: let the caller time out */
        }

        {
            dns_header h;
            dns_question q;
            size_t off = DNS_HEADER_LEN;
            size_t olen;

            sh->query_ok = 0;
            if (dns_parse_header(buf, (size_t)n, &h) == DNS_OK &&
                !dns_is_response(&h) && h.qdcount == 1 &&
                dns_parse_question(buf, (size_t)n, off, &q, &off) == DNS_OK) {
                sh->query_ok = 1;
                sh->query_type = q.type;
            }

            olen = build_response(buf, (size_t)n, out, sh->mode);
            if (olen == 0) {
                continue;
            }
            if (sh->mode == 7) {
                /* Answer from a DIFFERENT socket, so the answer comes
                 * from the right address but the wrong port. */
                if (alt < 0) {
                    sock_ipv4_bytes b;
                    struct sockaddr_in sa;

                    alt = socket(AF_INET, SOCK_DGRAM, 0);
                    sock_ipv4_from_dotted("127.0.0.1", b.ip);
                    b.port[0] = 0;
                    b.port[1] = 0;
                    sock_addr_apply(&b, &sa);
                    bind(alt, (struct sockaddr *)&sa, sizeof sa);
                }
                sendto(alt, out, olen, 0, (struct sockaddr *)&from, from_len);
            } else {
                sendto(fd, out, olen, 0, (struct sockaddr *)&from, from_len);
            }
        }
    }

    if (alt >= 0) {
        close(alt);
    }
    close(fd);
}

/* Start the fake server. Returns the child pid, or -1. */
static pid_t start_fake_server(fake_shared *sh)
{
    sock_ipv4_bytes b;
    struct sockaddr_in sa;
    socklen_t sl;
    sock_ipv4_bytes got;
    int fd;
    pid_t pid;

    sh->port = 0;
    sh->queries = 0;
    sh->mode = 0;
    sh->query_ok = 0;
    sh->query_type = 0;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }
    sock_ipv4_from_dotted("127.0.0.1", b.ip);
    b.port[0] = 0;
    b.port[1] = 0;
    if (sock_addr_apply(&b, &sa) != 0) {
        close(fd);
        return -1;
    }
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
        close(fd);
        return -1;
    }
    sl = sizeof sa;
    if (getsockname(fd, (struct sockaddr *)&sa, &sl) != 0) {
        close(fd);
        return -1;
    }
    sock_addr_extract(&sa, &got);
    sh->port = (int)sock_port_from_bytes(got.port);

    pid = fork();
    if (pid < 0) {
        close(fd);
        return -1;
    }
    if (pid == 0) {
        fake_server_loop(fd, sh);
        exit(0);               /* a clean exit, so valgrind checks us too */
    }
    close(fd);                 /* the parent does not serve */
    return pid;
}

/* Stop the fake server and wait for it. */
static void stop_fake_server(fake_shared *sh, pid_t pid)
{
    sock_ipv4_bytes b;
    struct sockaddr_in sa;
    int fd;

    if (pid <= 0) {
        return;
    }
    sh->mode = 99;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
        sock_ipv4_from_dotted("127.0.0.1", b.ip);
        b.port[0] = (uint8_t)(sh->port >> 8);
        b.port[1] = (uint8_t)(sh->port & 0xFF);
        sock_addr_apply(&b, &sa);
        sendto(fd, "x", 1, 0, (struct sockaddr *)&sa, sizeof sa);
        close(fd);
    }
    waitpid(pid, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* The cache, on its own                                               */
/* ------------------------------------------------------------------ */

static const unsigned char IP_A[4] = { 93, 184, 216, 34 };

static void test_the_cache_starts_empty(void)
{
    dns_resolver r;

    dns_resolver_init(&r, "127.0.0.1", 53, 100, fake_clock, NULL);
    TEST("the cache starts empty");
    if (dns_cache_count(&r, fake_now) != 0) {
        FAIL("a fresh resolver had entries");
        return;
    }
    PASS();
}

static void test_the_cache_finds_what_it_stored(void)
{
    dns_resolver r;
    dns_answer a;

    dns_resolver_init(&r, "127.0.0.1", 53, 100, fake_clock, NULL);
    TEST("the cache finds what it stored");
    if (dns_cache_store(&r, "example.com", DNS_TYPE_A, DNS_CLASS_IN,
                         IP_A, sizeof IP_A, 300, fake_now) != DNS_OK) {
        FAIL("the store failed");
        return;
    }
    if (dns_cache_lookup(&r, "example.com", DNS_TYPE_A, fake_now, &a) != DNS_OK) {
        FAIL("the entry was not found");
        return;
    }
    if (a.rdlength != 4 || memcmp(a.rdata, IP_A, 4) != 0) {
        FAIL("the wrong bytes came back");
        return;
    }
    if (a.ttl != 300) {
        FAIL("the TTL was not what was stored");
        return;
    }
    PASS();
}

static void test_the_cache_is_case_insensitive(void)
{
    dns_resolver r;
    dns_answer a;

    dns_resolver_init(&r, "127.0.0.1", 53, 100, fake_clock, NULL);
    TEST("the cache treats names case-insensitively");
    dns_cache_store(&r, "Example.COM", DNS_TYPE_A, DNS_CLASS_IN,
                    IP_A, sizeof IP_A, 300, fake_now);
    if (dns_cache_lookup(&r, "example.com", DNS_TYPE_A, fake_now, &a) != DNS_OK) {
        FAIL("a differently-cased name was a different key");
        return;
    }
    PASS();
}

static void test_an_expired_entry_is_a_miss(void)
{
    dns_resolver r;
    dns_answer a;

    dns_resolver_init(&r, "127.0.0.1", 53, 100, fake_clock, NULL);
    TEST("an expired entry is a miss, and is dropped");
    dns_cache_store(&r, "example.com", DNS_TYPE_A, DNS_CLASS_IN,
                    IP_A, sizeof IP_A, 10, fake_now);
    if (dns_cache_lookup(&r, "example.com", DNS_TYPE_A, fake_now, &a) != DNS_OK) {
        FAIL("a fresh entry was not found");
        return;
    }
    clock_advance(10);
    if (dns_cache_lookup(&r, "example.com", DNS_TYPE_A, fake_now, &a) != DNS_ERR_NOTFOUND) {
        FAIL("an expired entry was still a hit");
        return;
    }
    if (dns_cache_count(&r, fake_now) != 0) {
        FAIL("the expired entry was not dropped");
        return;
    }
    PASS();
}

static void test_a_zero_ttl_is_never_a_hit(void)
{
    dns_resolver r;
    dns_answer a;

    dns_resolver_init(&r, "127.0.0.1", 53, 100, fake_clock, NULL);
    TEST("a TTL of zero is never a hit");
    dns_cache_store(&r, "example.com", DNS_TYPE_A, DNS_CLASS_IN,
                    IP_A, sizeof IP_A, 0, fake_now);
    if (dns_cache_lookup(&r, "example.com", DNS_TYPE_A, fake_now, &a) != DNS_ERR_NOTFOUND) {
        FAIL("a zero-TTL entry was cached");
        return;
    }
    PASS();
}

static void test_the_key_is_the_name_and_the_type(void)
{
    dns_resolver r;
    dns_answer a;

    dns_resolver_init(&r, "127.0.0.1", 53, 100, fake_clock, NULL);
    TEST("the key is the name AND the type");
    dns_cache_store(&r, "example.com", DNS_TYPE_A, DNS_CLASS_IN,
                    IP_A, sizeof IP_A, 300, fake_now);
    if (dns_cache_lookup(&r, "other.com", DNS_TYPE_A, fake_now, &a) != DNS_ERR_NOTFOUND) {
        FAIL("a different name was a hit");
        return;
    }
    if (dns_cache_lookup(&r, "example.com", DNS_TYPE_AAAA, fake_now, &a) != DNS_ERR_NOTFOUND) {
        FAIL("a different type was a hit");
        return;
    }
    PASS();
}

static void test_storing_the_same_key_replaces(void)
{
    dns_resolver r;
    dns_answer a;
    unsigned char other[4] = { 1, 2, 3, 4 };

    dns_resolver_init(&r, "127.0.0.1", 53, 100, fake_clock, NULL);
    TEST("storing the same key replaces the entry");
    dns_cache_store(&r, "example.com", DNS_TYPE_A, DNS_CLASS_IN,
                    IP_A, sizeof IP_A, 300, fake_now);
    dns_cache_store(&r, "example.com", DNS_TYPE_A, DNS_CLASS_IN,
                    other, sizeof other, 300, fake_now);
    if (dns_cache_count(&r, fake_now) != 1) {
        FAIL("the entry was duplicated");
        return;
    }
    if (dns_cache_lookup(&r, "example.com", DNS_TYPE_A, fake_now, &a) != DNS_OK) {
        FAIL("the entry was not found");
        return;
    }
    if (memcmp(a.rdata, other, 4) != 0) {
        FAIL("the old bytes are still there");
        return;
    }
    PASS();
}

static void test_the_cache_evicts_the_oldest_when_full(void)
{
    dns_resolver r;
    dns_answer a;
    char name[32];
    int i;

    dns_resolver_init(&r, "127.0.0.1", 53, 100, fake_clock, NULL);
    TEST("a full cache evicts the entry that expires soonest");
    for (i = 0; i < DNS_CACHE_MAX; i++) {
        snprintf(name, sizeof name, "host%d.example", i);
        dns_cache_store(&r, name, DNS_TYPE_A, DNS_CLASS_IN,
                        IP_A, sizeof IP_A, (uint32_t)(100 + i), fake_now);
    }
    if (dns_cache_count(&r, fake_now) != DNS_CACHE_MAX) {
        FAIL("the cache did not fill up");
        return;
    }
    /* The entry with the smallest TTL is "host0.example" -- it should go
     * first, not the one that was stored first. */
    dns_cache_store(&r, "new.example", DNS_TYPE_A, DNS_CLASS_IN,
                    IP_A, sizeof IP_A, 999, fake_now);
    if (dns_cache_lookup(&r, "host0.example", DNS_TYPE_A, fake_now, &a) != DNS_ERR_NOTFOUND) {
        FAIL("the soonest-to-expire entry survived");
        return;
    }
    if (dns_cache_lookup(&r, "host1.example", DNS_TYPE_A, fake_now, &a) != DNS_OK) {
        FAIL("the wrong entry was evicted");
        return;
    }
    PASS();
}

static void test_the_cache_drops_expired_entries_first(void)
{
    dns_resolver r;
    dns_answer a;
    char name[32];
    int i;

    dns_resolver_init(&r, "127.0.0.1", 53, 100, fake_clock, NULL);
    TEST("a full cache drops expired entries before live ones");
    for (i = 0; i < DNS_CACHE_MAX; i++) {
        snprintf(name, sizeof name, "host%d.example", i);
        dns_cache_store(&r, name, DNS_TYPE_A, DNS_CLASS_IN,
                        IP_A, sizeof IP_A, 300, fake_now);
    }
    clock_advance(301);
    /* Everything is expired now. Adding one entry should reuse the
     * space rather than evict a live entry -- and there are no live
     * entries, so the point is that the count stays at one. */
    dns_cache_store(&r, "new.example", DNS_TYPE_A, DNS_CLASS_IN,
                    IP_A, sizeof IP_A, 300, fake_now);
    if (dns_cache_count(&r, fake_now) != 1) {
        FAIL("the expired entries were not reclaimed");
        return;
    }
    if (dns_cache_lookup(&r, "host0.example", DNS_TYPE_A, fake_now, &a) != DNS_ERR_NOTFOUND) {
        FAIL("an expired entry came back");
        return;
    }
    PASS();
}

/* ------------------------------------------------------------------ */
/* The exchange                                                        */
/* ------------------------------------------------------------------ */

static void test_it_resolves_a_name_over_a_socket(void)
{
    fake_shared *sh = shared_new();
    pid_t pid;
    dns_resolver r;
    dns_answer a;

    TEST("resolves a name over a real UDP socket");
    pid = start_fake_server(sh);
    if (pid < 0) {
        FAIL("could not start the fake server");
        return;
    }
    if (dns_resolver_init(&r, "127.0.0.1", (uint16_t)sh->port, 500,
                          fake_clock, NULL) != DNS_OK) {
        FAIL("the resolver would not initialize");
        stop_fake_server(sh, pid);
        return;
    }
    if (dns_resolve(&r, "example.com", DNS_TYPE_A, &a) != DNS_OK) {
        FAIL("the resolve failed");
        dns_resolver_close(&r);
        stop_fake_server(sh, pid);
        return;
    }
    if (a.rdlength != 4 || memcmp(a.rdata, IP_A, 4) != 0) {
        FAIL("the wrong address came back");
        dns_resolver_close(&r);
        stop_fake_server(sh, pid);
        return;
    }
    if (a.ttl != 300) {
        FAIL("the wrong TTL came back");
        dns_resolver_close(&r);
        stop_fake_server(sh, pid);
        return;
    }
    if (a.from_cache) {
        FAIL("the answer came from the cache on the first lookup");
        dns_resolver_close(&r);
        stop_fake_server(sh, pid);
        return;
    }
    dns_resolver_close(&r);
    stop_fake_server(sh, pid);
    PASS();
}

static void test_the_query_is_a_dns_query(void)
{
    fake_shared *sh = shared_new();
    pid_t pid;
    dns_resolver r;
    dns_answer a;

    TEST("the query we send is a well-formed DNS query");
    pid = start_fake_server(sh);
    if (pid < 0) {
        FAIL("could not start the fake server");
        return;
    }
    dns_resolver_init(&r, "127.0.0.1", (uint16_t)sh->port, 500,
                      fake_clock, NULL);
    dns_resolve(&r, "example.com", DNS_TYPE_A, &a);
    dns_resolver_close(&r);
    stop_fake_server(sh, pid);
    if (!sh->query_ok) {
        FAIL("the server did not recognise our query");
        return;
    }
    if (sh->query_type != DNS_TYPE_A) {
        FAIL("the query asked for the wrong type");
        return;
    }
    PASS();
}

static void test_the_second_lookup_is_served_from_the_cache(void)
{
    fake_shared *sh = shared_new();
    pid_t pid;
    dns_resolver r;
    dns_answer a;

    TEST("the second lookup of the same name does not touch the network");
    pid = start_fake_server(sh);
    if (pid < 0) {
        FAIL("could not start the fake server");
        return;
    }
    dns_resolver_init(&r, "127.0.0.1", (uint16_t)sh->port, 500,
                      fake_clock, NULL);
    if (dns_resolve(&r, "example.com", DNS_TYPE_A, &a) != DNS_OK) {
        FAIL("the first resolve failed");
        dns_resolver_close(&r);
        stop_fake_server(sh, pid);
        return;
    }
    if (dns_resolve(&r, "example.com", DNS_TYPE_A, &a) != DNS_OK) {
        FAIL("the second resolve failed");
        dns_resolver_close(&r);
        stop_fake_server(sh, pid);
        return;
    }
    dns_resolver_close(&r);
    stop_fake_server(sh, pid);
    if (sh->queries != 1) {
        FAIL("the server saw more than one query");
        return;
    }
    if (!a.from_cache) {
        FAIL("the second answer was not marked as cached");
        return;
    }
    PASS();
}

static void test_an_expired_entry_is_looked_up_again(void)
{
    fake_shared *sh = shared_new();
    pid_t pid;
    dns_resolver r;
    dns_answer a;

    TEST("an expired cache entry is looked up again");
    pid = start_fake_server(sh);
    if (pid < 0) {
        FAIL("could not start the fake server");
        return;
    }
    dns_resolver_init(&r, "127.0.0.1", (uint16_t)sh->port, 500,
                      fake_clock, NULL);
    dns_resolve(&r, "example.com", DNS_TYPE_A, &a);
    clock_advance(301);       /* the server's TTL was 300 */
    dns_resolve(&r, "example.com", DNS_TYPE_A, &a);
    dns_resolver_close(&r);
    stop_fake_server(sh, pid);
    if (sh->queries != 2) {
        FAIL("the expired entry was served from the cache");
        return;
    }
    if (a.from_cache) {
        FAIL("the second answer was marked as cached");
        return;
    }
    PASS();
}

/* The failure modes: one test each, with the mode set before the
 * resolve. */
static void run_misbehaviour_test(const char *name, int mode,
                                  dns_status expected, int timeout_ms)
{
    fake_shared *sh = shared_new();
    pid_t pid;
    dns_resolver r;
    dns_answer a;
    dns_status st;

    TEST(name);
    pid = start_fake_server(sh);
    if (pid < 0) {
        FAIL("could not start the fake server");
        return;
    }
    sh->mode = mode;
    dns_resolver_init(&r, "127.0.0.1", (uint16_t)sh->port, timeout_ms,
                      fake_clock, NULL);
    st = dns_resolve(&r, "example.com", DNS_TYPE_A, &a);
    dns_resolver_close(&r);
    stop_fake_server(sh, pid);
    if (st != expected) {
        printf("FAIL: expected %s, got %s\n",
               dns_status_name(expected), dns_status_name(st));
        tests_failed++;
        return;
    }
    PASS();
}

static void test_a_silent_server_times_out(void)
{
    run_misbehaviour_test("a server that says nothing times out", 1,
                          DNS_ERR_TIMEOUT, 150);
}

static void test_the_wrong_id_is_refused(void)
{
    run_misbehaviour_test("an answer with the wrong id is refused", 2,
                          DNS_ERR_ID_MISMATCH, 500);
}

static void test_the_wrong_question_is_refused(void)
{
    run_misbehaviour_test("an answer to a different question is refused", 3,
                          DNS_ERR_QUESTION, 500);
}

static void test_a_truncated_answer_is_reported(void)
{
    run_misbehaviour_test("a truncated answer is reported, not guessed at", 4,
                          DNS_ERR_TRUNCATED, 500);
}

static void test_nxdomain_is_nxdomain(void)
{
    run_misbehaviour_test("NXDOMAIN is reported as NXDOMAIN", 5,
                          DNS_ERR_NXDOMAIN, 500);
}

static void test_no_answers_is_not_found(void)
{
    run_misbehaviour_test("NOERROR with no answers is not found", 6,
                          DNS_ERR_NOTFOUND, 500);
}

static void test_an_answer_from_another_socket_is_refused(void)
{
    run_misbehaviour_test("an answer from another socket is refused", 7,
                          DNS_ERR_SOURCE, 500);
}

static void test_a_malformed_server_address_is_refused(void)
{
    dns_resolver r;

    TEST("a malformed server address is refused");
    if (dns_resolver_init(&r, "not-an-address", 53, 100, fake_clock, NULL) != DNS_ERR_FORMAT) {
        FAIL("a malformed address was accepted");
        return;
    }
    PASS();
}

static void test_an_illegal_name_is_refused_before_the_network(void)
{
    fake_shared *sh = shared_new();
    pid_t pid;
    dns_resolver r;
    dns_answer a;

    TEST("an illegal name is refused before anything is sent");
    pid = start_fake_server(sh);
    if (pid < 0) {
        FAIL("could not start the fake server");
        return;
    }
    dns_resolver_init(&r, "127.0.0.1", (uint16_t)sh->port, 500,
                      fake_clock, NULL);
    if (dns_resolve(&r, "a..b.example", DNS_TYPE_A, &a) == DNS_OK) {
        FAIL("an illegal name was resolved");
        dns_resolver_close(&r);
        stop_fake_server(sh, pid);
        return;
    }
    dns_resolver_close(&r);
    stop_fake_server(sh, pid);
    if (sh->queries != 0) {
        FAIL("a query went out for an illegal name");
        return;
    }
    PASS();
}

int main(void)
{
    printf("Stage 65.9 tests: DNS from scratch -- the transport and the cache\n\n");

    test_the_cache_starts_empty();
    test_the_cache_finds_what_it_stored();
    test_the_cache_is_case_insensitive();
    test_an_expired_entry_is_a_miss();
    test_a_zero_ttl_is_never_a_hit();
    test_the_key_is_the_name_and_the_type();
    test_storing_the_same_key_replaces();
    test_the_cache_evicts_the_oldest_when_full();
    test_the_cache_drops_expired_entries_first();

    test_it_resolves_a_name_over_a_socket();
    test_the_query_is_a_dns_query();
    test_the_second_lookup_is_served_from_the_cache();
    test_an_expired_entry_is_looked_up_again();
    test_a_silent_server_times_out();
    test_the_wrong_id_is_refused();
    test_the_wrong_question_is_refused();
    test_a_truncated_answer_is_reported();
    test_nxdomain_is_nxdomain();
    test_no_answers_is_not_found();
    test_an_answer_from_another_socket_is_refused();
    test_a_malformed_server_address_is_refused();
    test_an_illegal_name_is_refused_before_the_network();

    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
