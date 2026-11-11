/*
 * Stage 65.2 tests: addresses by hand.
 *
 * The lesson: an address is a byte string, not a name.
 *
 * Stage 65.1 asked the kernel to turn a name ("127.0.0.1") and a number (a port)
 * into bytes. This stage does that work itself: the port becomes two bytes in a
 * known order, the address becomes four bytes, and the whole endpoint becomes a
 * struct of bytes we can point at and compare.
 *
 * Every expectation in this file is computed by hand. The test does not ask the
 * library to produce the answer it is checking against -- that would be checking
 * the implementation against itself.
 *
 * Hermetic: loopback only.
 */

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../src/sockets.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-52s", #name); \
    } while (0)

#define PASS() \
    do { \
        tests_passed++; \
        printf("✓\n"); \
    } while (0)

#define FAIL(msg) \
    do { \
        printf("✗ %s\n", msg); \
    } while (0)

/* The library helpers this stage exists to replace. The component must not
 * call any of them; see the last test in this file. */
static const char *const FORBIDDEN[] = {
    "htons", "ntohs", "inet_pton", "inet_ntop", "inet_addr", "getaddrinfo", NULL
};

/* ------------------------------------------------------------------ */
/* 1. The loopback address is four bytes, not a string.                 */
/* ------------------------------------------------------------------ */
static void test_loopback_bytes_are_127_0_0_1(void)
{
    sock_ipv4_bytes b;

    TEST("loopback_bytes_are_127_0_0_1");
    if (sock_ipv4_loopback_bytes(0, &b) != 0) {
        FAIL("sock_ipv4_loopback_bytes failed");
        return;
    }
    if (b.ip[0] != 127 || b.ip[1] != 0 || b.ip[2] != 0 || b.ip[3] != 1) {
        FAIL("ip bytes are not 127.0.0.1");
        return;
    }
    PASS();
}

/* ------------------------------------------------------------------ */
/* 2. The port is two bytes, high byte first.                          */
/* ------------------------------------------------------------------ */
static void test_port_bytes_are_big_endian(void)
{
    sock_ipv4_bytes b;

    TEST("port_bytes_are_big_endian");
    if (sock_ipv4_loopback_bytes(0x1234, &b) != 0) {
        FAIL("sock_ipv4_loopback_bytes failed");
        return;
    }
    if (b.port[0] != 0x12 || b.port[1] != 0x34) {
        FAIL("port bytes are not big-endian");
        return;
    }
    PASS();
}

/* ------------------------------------------------------------------ */
/* 3. ...for every port, not just a lucky one.                          */
/* ------------------------------------------------------------------ */
static void test_port_bytes_are_big_endian_across_the_range(void)
{
    uint32_t p;
    sock_ipv4_bytes b;
    int bad = 0;

    TEST("port_bytes_are_big_endian_across_the_range");
    for (p = 0; p <= 0xFFFFu; p++) {
        if (sock_ipv4_loopback_bytes((uint16_t)p, &b) != 0) {
            FAIL("sock_ipv4_loopback_bytes failed");
            return;
        }
        if (b.port[0] != (uint8_t)((p >> 8) & 0xFF) ||
            b.port[1] != (uint8_t)(p & 0xFF)) {
            bad = 1;
            break;
        }
    }
    if (bad) {
        FAIL("some port did not produce big-endian bytes");
        return;
    }
    PASS();
}

/* ------------------------------------------------------------------ */
/* 4. The bytes we build are the bytes the struct carries.             */
/* ------------------------------------------------------------------ */
static void test_applied_address_carries_our_bytes(void)
{
    sock_ipv4_bytes b;
    struct sockaddr_in addr;
    const uint8_t *raw_port;
    const uint8_t *raw_ip;

    TEST("applied_address_carries_our_bytes");
    if (sock_ipv4_loopback_bytes(0xBEEF, &b) != 0) {
        FAIL("sock_ipv4_loopback_bytes failed");
        return;
    }
    if (sock_addr_apply(&b, &addr) != 0) {
        FAIL("sock_addr_apply failed");
        return;
    }
    raw_port = (const uint8_t *)&addr.sin_port;
    raw_ip = (const uint8_t *)&addr.sin_addr;
    if (raw_port[0] != 0xBE || raw_port[1] != 0xEF) {
        FAIL("the struct's port bytes are not the bytes we specified");
        return;
    }
    if (raw_ip[0] != 127 || raw_ip[1] != 0 || raw_ip[2] != 0 || raw_ip[3] != 1) {
        FAIL("the struct's address bytes are not the bytes we specified");
        return;
    }
    PASS();
}

/* ------------------------------------------------------------------ */
/* 5. The exception: the family is a host-order number, not a byte      */
/*    string. POSIX stores sin_family in host byte order.                */
/* ------------------------------------------------------------------ */
static void test_the_family_field_is_host_order(void)
{
    sock_ipv4_bytes b;
    struct sockaddr_in addr;

    TEST("the_family_field_is_host_order");
    if (sock_ipv4_loopback_bytes(0, &b) != 0) {
        FAIL("sock_ipv4_loopback_bytes failed");
        return;
    }
    if (sock_addr_apply(&b, &addr) != 0) {
        FAIL("sock_addr_apply failed");
        return;
    }
    /* If the family were stored in network order this would read 0x0200. */
    if (addr.sin_family != AF_INET) {
        FAIL("sin_family is not AF_INET in host order");
        return;
    }
    PASS();
}

/* ------------------------------------------------------------------ */
/* 6. The kernel round-trips our bytes: what we hand it is what it      */
/*    stores and hands back.                                            */
/* ------------------------------------------------------------------ */
static void test_the_kernel_round_trips_our_bytes(void)
{
    sock_listener l;
    struct sockaddr_in bound;
    socklen_t bound_len = sizeof(bound);
    sock_ipv4_bytes got;

    TEST("the_kernel_round_trips_our_bytes");
    if (sock_listen_loopback(&l, 0, 8) != 0) {
        FAIL("sock_listen_loopback failed");
        return;
    }
    if (getsockname(l.listen_fd, (struct sockaddr *)&bound, &bound_len) != 0) {
        FAIL("getsockname failed");
        sock_close(l.listen_fd);
        return;
    }
    if (sock_addr_extract(&bound, &got) != 0) {
        FAIL("sock_addr_extract failed");
        sock_close(l.listen_fd);
        return;
    }
    if (got.ip[0] != 127 || got.ip[1] != 0 || got.ip[2] != 0 || got.ip[3] != 1) {
        FAIL("the kernel stored a different address than we specified");
        sock_close(l.listen_fd);
        return;
    }
    if (got.port[0] != (uint8_t)((l.port >> 8) & 0xFF) ||
        got.port[1] != (uint8_t)(l.port & 0xFF)) {
        FAIL("the kernel stored a different port than we were told about");
        sock_close(l.listen_fd);
        return;
    }
    sock_close(l.listen_fd);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 7. Bytes out, bytes back: extract and re-apply must be identity.    */
/* ------------------------------------------------------------------ */
static void test_extracted_bytes_rebuild_the_same_address(void)
{
    sock_listener l;
    struct sockaddr_in bound;
    socklen_t bound_len = sizeof(bound);
    sock_ipv4_bytes got;
    struct sockaddr_in rebuilt;
    const uint8_t *a;
    const uint8_t *b;

    TEST("extracted_bytes_rebuild_the_same_address");
    if (sock_listen_loopback(&l, 0, 8) != 0) {
        FAIL("sock_listen_loopback failed");
        return;
    }
    if (getsockname(l.listen_fd, (struct sockaddr *)&bound, &bound_len) != 0) {
        FAIL("getsockname failed");
        sock_close(l.listen_fd);
        return;
    }
    if (sock_addr_extract(&bound, &got) != 0) {
        FAIL("sock_addr_extract failed");
        sock_close(l.listen_fd);
        return;
    }
    if (sock_addr_apply(&got, &rebuilt) != 0) {
        FAIL("sock_addr_apply failed");
        sock_close(l.listen_fd);
        return;
    }
    a = (const uint8_t *)&bound.sin_port;
    b = (const uint8_t *)&rebuilt.sin_port;
    if (memcmp(a, b, 2) != 0) {
        FAIL("rebuilt port bytes differ from the kernel's");
        sock_close(l.listen_fd);
        return;
    }
    a = (const uint8_t *)&bound.sin_addr;
    b = (const uint8_t *)&rebuilt.sin_addr;
    if (memcmp(a, b, 4) != 0) {
        FAIL("rebuilt address bytes differ from the kernel's");
        sock_close(l.listen_fd);
        return;
    }
    sock_close(l.listen_fd);
    PASS();
}

/* ------------------------------------------------------------------ */
/* 8. The contract of this stage: the component builds its addresses    */
/*    by hand.                                                          */
/*                                                                     */
/*    The lesson is in the *construction*, and the construction is      */
/*    invisible in the output -- the library helpers produce exactly    */
/*    the same bytes we produce by hand. So the only way to test the     */
/*    lesson is to look at the source.                                   */
/* ------------------------------------------------------------------ */
static int scan_dir_for_forbidden(const char *dirpath, char *hit, size_t hitlen)
{
    DIR *dir = opendir(dirpath);
    struct dirent *entry;
    int found = 0;

    if (dir == NULL) {
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        char path[512];
        FILE *f;
        static char text[65536];
        size_t len;
        size_t i;

        if (strlen(entry->d_name) < 3) {
            continue;
        }
        if (strcmp(entry->d_name + strlen(entry->d_name) - 2, ".c") != 0 &&
            strcmp(entry->d_name + strlen(entry->d_name) - 2, ".h") != 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", dirpath, entry->d_name);
        f = fopen(path, "rb");
        if (f == NULL) {
            continue;
        }
        len = fread(text, 1, sizeof(text) - 1, f);
        fclose(f);
        text[len] = '\0';
        for (i = 0; i < len; i++) {
            int t;
            for (t = 0; FORBIDDEN[t] != NULL; t++) {
                size_t flen = strlen(FORBIDDEN[t]);
                if (i + flen <= len && memcmp(text + i, FORBIDDEN[t], flen) == 0) {
                    snprintf(hit, hitlen, "%s still uses %s", path, FORBIDDEN[t]);
                    found = 1;
                    goto done;
                }
            }
        }
    }
done:
    closedir(dir);
    return found;
}

static void test_the_component_builds_its_addresses_by_hand(void)
{
    char hit[256];
    int r;

    TEST("the_component_builds_its_addresses_by_hand");
    r = scan_dir_for_forbidden("src", hit, sizeof(hit));
    if (r < 0) {
        r = scan_dir_for_forbidden("../src", hit, sizeof(hit));
    }
    if (r < 0) {
        FAIL("could not find the component's src/ directory");
        return;
    }
    if (r > 0) {
        FAIL(hit);
        return;
    }
    PASS();
}

int main(void)
{
    printf("Stage 65.2 — addresses by hand\n\n");

    test_loopback_bytes_are_127_0_0_1();
    test_port_bytes_are_big_endian();
    test_port_bytes_are_big_endian_across_the_range();
    test_applied_address_carries_our_bytes();
    test_the_family_field_is_host_order();
    test_the_kernel_round_trips_our_bytes();
    test_extracted_bytes_rebuild_the_same_address();
    test_the_component_builds_its_addresses_by_hand();

    printf("\n%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
