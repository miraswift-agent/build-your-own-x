/*
 * Stage 65.8 -- DNS from scratch, first half: the packets.
 *
 * The tests here are checked against *captured wire bytes*, not against
 * my own encoder. The fixtures below are the actual bytes dig sent and
 * the actual bytes the system resolver sent back, captured through a
 * UDP relay on 127.0.0.1:5354 (see the close-out). So the oracle is
 * not "what I think DNS looks like" -- it is what DNS looks like on
 * the wire, and dig's own answer text is what the parsed bytes have to
 * agree with.
 *
 * Two dig defaults are worth naming:
 *
 *   - dig sets the AD bit in its queries (0x0120 = RD | AD) because
 *     it asks for DNSSEC data by default. We set RD only (0x0100), so
 *     the flags word differs by 0x0020 and the comparison masks it.
 *     Everything else -- id, counts, the name encoding, the type, the
 *     class -- is compared byte for byte.
 *
 *   - the captured responses came back with random ids, so the tests
 *     assert on the id that is actually in the fixture rather than
 *     pretending ids do not exist.
 */
#include "../src/dns.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  %-58s", name); } while (0)
#define PASS() do { tests_passed++; printf("OK\n"); } while (0)
#define FAIL(msg) do { tests_failed++; printf("FAIL: %s\n", msg); } while (0)

/* Captured wire bytes -- dig's queries and the system resolver's
 * answers, verbatim. See the close-out for how they were captured. */
static const char *HEX_Q_DIG    = "faf401200001000000000000076578616d706c6503636f6d0000010001";
static const char *HEX_Q_CNAME  = "96530120000100000000000003777777076578616d706c6503636f6d0000050001";
static const char *HEX_R_A      = "faf481800001000200000000076578616d706c6503636f6d0000010001c00c000100010000012c0004ac4293f3c00c000100010000012c00046814179a";
static const char *HEX_R_NX     = "7f3681830001000000010000156e6f6e6578697374656e742d78797a2d313233343503636f6d0000010001c0220006000100000384003d01610c67746c642d73657276657273036e657400056e73746c640c766572697369676e2d677273c0226af519d7000007080000038400093a8000000384";
static const char *HEX_R_NODATA = "96538180000100000001000003777777076578616d706c6503636f6d0000050001c0100006000100000708003207656c6c696f7474026e730a636c6f7564666c617265c01803646e73c038901fd090000027100000096000093a8000000708";
static const char *HEX_R_MX     = "aad781800001000100000000076578616d706c6503636f6d00000f0001c00c000f00010000004a0003000000";

static size_t unhex(const char *hex, unsigned char *out, size_t cap)
{
    size_t n = 0;
    while (hex[0] != '\0' && hex[1] != '\0') {
        unsigned int v = 0;
        if (sscanf(hex, "%2x", &v) != 1) {
            break;
        }
        if (n >= cap) {
            break;
        }
        out[n++] = (unsigned char)v;
        hex += 2;
    }
    return n;
}

/* Compare two byte strings, ignoring the AD bit of the flags word
 * (offset 2) when the message is a query. */
static int same_except_ad(const unsigned char *a, const unsigned char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        /* The AD bit (0x0020) lives in the low byte of the flags word,
         * which is byte 3 -- the high byte holds QR/AA/TC/RD. */
        if (i == 3) {
            if ((a[i] & 0xDF) != (b[i] & 0xDF)) {
                return 0;
            }
            continue;
        }
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static void test_encoding_matches_dig(void)
{
    unsigned char want[64];
    unsigned char got[64];
    size_t wn, gn;

    wn = unhex("076578616d706c6503636f6d00", want, sizeof want);
    TEST("encodes a name exactly as dig sends it");
    if (dns_name_encode("example.com", got, sizeof got, &gn) != DNS_OK) {
        FAIL("encode failed");
        return;
    }
    if (gn != wn || memcmp(got, want, wn) != 0) {
        FAIL("encoded bytes differ from dig's");
        return;
    }
    PASS();
}

static void test_encodes_root(void)
{
    unsigned char got[8];
    size_t gn = 0;

    TEST("encodes the root name as one zero byte");
    if (dns_name_encode(".", got, sizeof got, &gn) != DNS_OK || gn != 1 || got[0] != 0) {
        FAIL("root name is not a single zero byte");
        return;
    }
    PASS();
}

static void test_rejects_long_label(void)
{
    char name[128];
    unsigned char got[256];
    size_t gn = 0;

    memset(name, 'a', sizeof name);
    name[64] = '\0';

    TEST("rejects a label longer than 63 bytes");
    if (dns_name_encode(name, got, sizeof got, &gn) != DNS_ERR_NAME) {
        FAIL("a 64-byte label was accepted");
        return;
    }
    PASS();
}

static void test_rejects_long_name(void)
{
    char name[400];
    unsigned char got[512];
    size_t gn = 0;

    memset(name, 'a', sizeof name);
    name[300] = '\0';

    TEST("rejects a name longer than 255 bytes");
    if (dns_name_encode(name, got, sizeof got, &gn) != DNS_ERR_NAME) {
        FAIL("a 300-byte name was accepted");
        return;
    }
    PASS();
}

static void test_encode_no_space(void)
{
    unsigned char got[5];
    size_t gn = 0;

    TEST("refuses to write past the buffer");
    if (dns_name_encode("example.com", got, sizeof got, &gn) != DNS_ERR_NOSPACE) {
        FAIL("the encoder wrote past the buffer");
        return;
    }
    PASS();
}

static void test_query_matches_dig(void)
{
    unsigned char want[64];
    unsigned char got[64];
    size_t wn, gn = 0;

    wn = unhex(HEX_Q_DIG, want, sizeof want);
    TEST("builds an A query byte-identical to dig's");
    if (dns_build_query("example.com", DNS_TYPE_A, (uint16_t)((want[0] << 8) | want[1]),
                        got, sizeof got, &gn) != DNS_OK) {
        FAIL("build_query failed");
        return;
    }
    if (gn != wn || !same_except_ad(got, want, wn)) {
        FAIL("our query differs from dig's on the wire");
        return;
    }
    PASS();
}

static void test_query_cname_matches_dig(void)
{
    unsigned char want[64];
    unsigned char got[64];
    size_t wn, gn = 0;

    wn = unhex(HEX_Q_CNAME, want, sizeof want);
    TEST("builds a CNAME query byte-identical to dig's");
    if (dns_build_query("www.example.com", DNS_TYPE_CNAME,
                        (uint16_t)((want[0] << 8) | want[1]),
                        got, sizeof got, &gn) != DNS_OK) {
        FAIL("build_query failed");
        return;
    }
    if (gn != wn || !same_except_ad(got, want, wn)) {
        FAIL("our query differs from dig's on the wire");
        return;
    }
    PASS();
}

static void test_decodes_plain_name(void)
{
    unsigned char msg[512];
    size_t n = unhex(HEX_R_A, msg, sizeof msg);
    char name[DNS_MAX_NAME];
    size_t next = 0;

    TEST("decodes a plain name");
    if (dns_name_decode(msg, n, DNS_HEADER_LEN, name, sizeof name, &next) != DNS_OK) {
        FAIL("decode failed");
        return;
    }
    if (strcmp(name, "example.com") != 0) {
        FAIL("wrong name decoded");
        return;
    }
    PASS();
}

static void test_decodes_root(void)
{
    unsigned char buf[2];
    char name[DNS_MAX_NAME];
    size_t next = 0;

    buf[0] = 0;
    buf[1] = 0;
    TEST("decodes the root name");
    if (dns_name_decode(buf, 1, 0, name, sizeof name, &next) != DNS_OK) {
        FAIL("decode failed");
        return;
    }
    if (strcmp(name, ".") != 0) {
        FAIL("root did not decode to \".\"");
        return;
    }
    PASS();
}

static void test_decodes_pointer(void)
{
    unsigned char msg[512];
    size_t n = unhex(HEX_R_A, msg, sizeof msg);
    char name[DNS_MAX_NAME];
    size_t next = 0;
    /* The answer's name in the captured response is c00c -- a pointer
     * back to offset 12, the question's name. */
    size_t ans = DNS_HEADER_LEN + 13 + 4;

    TEST("decodes a compression pointer");
    if (dns_name_decode(msg, n, ans, name, sizeof name, &next) != DNS_OK) {
        FAIL("decode failed");
        return;
    }
    if (strcmp(name, "example.com") != 0) {
        FAIL("the pointer did not resolve to the question's name");
        return;
    }
    PASS();
}

static void test_decodes_pointer_in_rdata(void)
{
    unsigned char msg[512];
    size_t n = unhex(HEX_R_NX, msg, sizeof msg);
    dns_header h;
    dns_question q;
    dns_record rr;
    size_t off = 0, i;
    char name[DNS_MAX_NAME];
    dns_status st = DNS_OK;

    TEST("decodes a compression pointer inside rdata");
    if (dns_parse_header(msg, n, &h) != DNS_OK) {
        FAIL("header parse failed");
        return;
    }
    if (dns_parse_question(msg, n, DNS_HEADER_LEN, &q, &off) != DNS_OK) {
        FAIL("question parse failed");
        return;
    }
    if (dns_parse_record(msg, n, off, &rr, &off) != DNS_OK) {
        FAIL("record parse failed");
        return;
    }
    /* Find the pointer inside the SOA rdata and resolve it. */
    for (i = 0; i < rr.rdlength; i++) {
        if ((rr.rdata[i] & 0xC0) == 0xC0) {
            size_t next = 0;
            st = dns_name_decode(msg, n,
                                 (size_t)(rr.rdata - msg) + i,
                                 name, sizeof name, &next);
            break;
        }
    }
    if (st != DNS_OK) {
        FAIL("no pointer found in the rdata");
        return;
    }
    /* The SOA's RNAME ends in a pointer to offset 0x22, which is the
     * "com" label of the query's name -- so the pointer resolves to
     * "com", not to the whole query name. A pointer points at a NAME,
     * and a name can start anywhere, including in the middle of
     * another name. */
    if (strcmp(name, "com") != 0) {
        FAIL("the rdata pointer did not resolve to the label it points at");
        return;
    }
    PASS();
}

static void test_detects_pointer_loop(void)
{
    unsigned char buf[4];
    char name[DNS_MAX_NAME];
    size_t next = 0;

    buf[0] = 0xC0; buf[1] = 0x00; buf[2] = 0xC0; buf[3] = 0x02;
    TEST("detects a pointer that loops");
    if (dns_name_decode(buf, sizeof buf, 0, name, sizeof name, &next) != DNS_ERR_COMPRESSION) {
        FAIL("a pointer loop was not caught");
        return;
    }
    PASS();
}

static void test_detects_pointer_out_of_range(void)
{
    unsigned char buf[2];
    char name[DNS_MAX_NAME];
    size_t next = 0;

    buf[0] = 0xC0; buf[1] = 0xFF;
    TEST("detects a pointer past the end of the buffer");
    if (dns_name_decode(buf, sizeof buf, 0, name, sizeof name, &next) != DNS_ERR_COMPRESSION) {
        FAIL("an out-of-range pointer was not caught");
        return;
    }
    PASS();
}

static void test_next_offset_after_original(void)
{
    unsigned char msg[512];
    size_t n = unhex(HEX_R_A, msg, sizeof msg);
    char name[DNS_MAX_NAME];
    size_t next = 0;

    TEST("returns the offset after the original encoding");
    if (dns_name_decode(msg, n, DNS_HEADER_LEN, name, sizeof name, &next) != DNS_OK) {
        FAIL("decode failed");
        return;
    }
    /* The question's name is 13 bytes, so the next thing starts at 25.
     * A decoder that followed the pointer and returned *its* end would
     * return 25 + something else entirely. */
    if (next != DNS_HEADER_LEN + 13) {
        FAIL("the offset after the name is wrong");
        return;
    }
    PASS();
}

static void test_parses_header(void)
{
    unsigned char msg[512];
    size_t n = unhex(HEX_R_A, msg, sizeof msg);
    dns_header h;

    TEST("parses the header");
    if (dns_parse_header(msg, n, &h) != DNS_OK) {
        FAIL("header parse failed");
        return;
    }
    if (h.id != 0xfaf4 || h.qdcount != 1 || h.ancount != 2 ||
        h.nscount != 0 || h.arcount != 0) {
        FAIL("header fields are wrong");
        return;
    }
    if (!dns_is_response(&h) || dns_rcode(&h) != DNS_RCODE_NOERROR) {
        FAIL("the response bit or rcode is wrong");
        return;
    }
    PASS();
}

static void test_parses_question(void)
{
    unsigned char msg[512];
    size_t n = unhex(HEX_R_A, msg, sizeof msg);
    dns_question q;
    size_t off = 0;

    TEST("parses the question");
    if (dns_parse_question(msg, n, DNS_HEADER_LEN, &q, &off) != DNS_OK) {
        FAIL("question parse failed");
        return;
    }
    if (strcmp(q.name, "example.com") != 0 || q.type != DNS_TYPE_A ||
        q.class_ != DNS_CLASS_IN) {
        FAIL("question fields are wrong");
        return;
    }
    if (off != DNS_HEADER_LEN + 13 + 4) {
        FAIL("the offset after the question is wrong");
        return;
    }
    PASS();
}

static void test_parses_a_record(void)
{
    unsigned char msg[512];
    size_t n = unhex(HEX_R_A, msg, sizeof msg);
    dns_question q;
    dns_record rr;
    size_t off = 0;

    TEST("parses an A record (172.66.147.243, ttl 300)");
    if (dns_parse_question(msg, n, DNS_HEADER_LEN, &q, &off) != DNS_OK) {
        FAIL("question parse failed");
        return;
    }
    if (dns_parse_record(msg, n, off, &rr, &off) != DNS_OK) {
        FAIL("record parse failed");
        return;
    }
    if (rr.type != DNS_TYPE_A || rr.class_ != DNS_CLASS_IN || rr.ttl != 300) {
        FAIL("record fields are wrong");
        return;
    }
    if (rr.rdlength != 4) {
        FAIL("rdlength is not 4");
        return;
    }
    if (rr.rdata[0] != 172 || rr.rdata[1] != 66 || rr.rdata[2] != 147 ||
        rr.rdata[3] != 243) {
        FAIL("the address does not match dig's answer");
        return;
    }
    if (strcmp(rr.name, "example.com") != 0) {
        FAIL("the record name did not resolve through the pointer");
        return;
    }
    PASS();
}

static void test_walks_every_answer(void)
{
    unsigned char msg[512];
    size_t n = unhex(HEX_R_A, msg, sizeof msg);
    dns_question q;
    dns_record rr;
    size_t off = 0;
    int count = 0;

    TEST("walks every answer in the response");
    if (dns_parse_question(msg, n, DNS_HEADER_LEN, &q, &off) != DNS_OK) {
        FAIL("question parse failed");
        return;
    }
    while (count < 2) {
        if (dns_parse_record(msg, n, off, &rr, &off) != DNS_OK) {
            FAIL("answer parse failed");
            return;
        }
        if (rr.type != DNS_TYPE_A) {
            FAIL("an answer was not an A record");
            return;
        }
        count++;
    }
    if (rr.rdata[0] != 104 || rr.rdata[1] != 20 || rr.rdata[2] != 23 ||
        rr.rdata[3] != 154) {
        FAIL("the second answer does not match dig's");
        return;
    }
    if (off != n) {
        FAIL("the answers did not end where the message does");
        return;
    }
    PASS();
}

static void test_nxdomain(void)
{
    unsigned char msg[512];
    size_t n = unhex(HEX_R_NX, msg, sizeof msg);
    dns_header h;

    TEST("reports NXDOMAIN (rcode 3) with no answers");
    if (dns_parse_header(msg, n, &h) != DNS_OK) {
        FAIL("header parse failed");
        return;
    }
    if (dns_rcode(&h) != DNS_RCODE_NXDOMAIN) {
        FAIL("the rcode is not NXDOMAIN");
        return;
    }
    if (h.ancount != 0 || h.nscount != 1) {
        FAIL("the counts are wrong");
        return;
    }
    PASS();
}

static void test_parses_soa(void)
{
    unsigned char msg[512];
    size_t n = unhex(HEX_R_NX, msg, sizeof msg);
    dns_question q;
    dns_record rr;
    size_t off = 0;

    TEST("parses the SOA in the authority section");
    if (dns_parse_question(msg, n, DNS_HEADER_LEN, &q, &off) != DNS_OK) {
        FAIL("question parse failed");
        return;
    }
    if (dns_parse_record(msg, n, off, &rr, &off) != DNS_OK) {
        FAIL("record parse failed");
        return;
    }
    if (rr.type != DNS_TYPE_SOA || rr.ttl != 900 || rr.rdlength != 61) {
        FAIL("the SOA fields are wrong");
        return;
    }
    PASS();
}

static void test_parses_null_mx(void)
{
    unsigned char msg[512];
    size_t n = unhex(HEX_R_MX, msg, sizeof msg);
    dns_question q;
    dns_record rr;
    size_t off = 0;

    TEST("parses a null MX (preference 0, ttl 74)");
    if (dns_parse_question(msg, n, DNS_HEADER_LEN, &q, &off) != DNS_OK) {
        FAIL("question parse failed");
        return;
    }
    if (dns_parse_record(msg, n, off, &rr, &off) != DNS_OK) {
        FAIL("record parse failed");
        return;
    }
    if (rr.type != DNS_TYPE_MX || rr.ttl != 74 || rr.rdlength != 3) {
        FAIL("the MX fields are wrong");
        return;
    }
    if (rr.rdata[0] != 0 || rr.rdata[1] != 0 || rr.rdata[2] != 0) {
        FAIL("the MX preference is not zero");
        return;
    }
    PASS();
}

static void test_reports_truncation(void)
{
    unsigned char msg[512];
    size_t n = unhex(HEX_R_A, msg, sizeof msg);
    dns_header h;
    dns_record rr;
    size_t off = 0;

    (void)n;
    TEST("reports truncation instead of guessing");
    if (dns_parse_header(msg, 5, &h) != DNS_ERR_TRUNCATED) {
        FAIL("a short header was not reported as truncated");
        return;
    }
    /* A record whose rdata runs past the end of the message: cut the
     * message in the middle of the first answer's address. */
    if (dns_parse_record(msg, DNS_HEADER_LEN + 13 + 4 + 2 + 10 + 2,
                         DNS_HEADER_LEN + 13 + 4, &rr, &off) != DNS_ERR_TRUNCATED) {
        FAIL("a record running off the end was not reported");
        return;
    }
    PASS();
}


static void test_nodata(void)
{
    unsigned char msg[512];
    size_t n = unhex(HEX_R_NODATA, msg, sizeof msg);
    dns_header h;
    dns_question q;
    dns_record rr;
    size_t off = 0;

    TEST("reports NODATA (no answers, an SOA in the authority)");
    if (dns_parse_header(msg, n, &h) != DNS_OK) {
        FAIL("header parse failed");
        return;
    }
    if (h.ancount != 0 || h.nscount != 1) {
        FAIL("the counts are wrong");
        return;
    }
    if (dns_parse_question(msg, n, DNS_HEADER_LEN, &q, &off) != DNS_OK) {
        FAIL("question parse failed");
        return;
    }
    if (dns_parse_record(msg, n, off, &rr, &off) != DNS_OK) {
        FAIL("record parse failed");
        return;
    }
    if (rr.type != DNS_TYPE_SOA) {
        FAIL("the authority record is not an SOA");
        return;
    }
    /* The SOA belongs to the example.com zone, so its owner name is
     * "example.com" -- reached through a pointer at offset 0x10, which
     * lands on the "example" label inside the question's name. */
    if (strcmp(rr.name, "example.com") != 0) {
        FAIL("the compressed name did not resolve");
        return;
    }
    PASS();
}

int main(void)
{
    printf("Stage 65.8 tests: DNS from scratch -- the packets\n\n");

    test_encoding_matches_dig();
    test_encodes_root();
    test_rejects_long_label();
    test_rejects_long_name();
    test_encode_no_space();
    test_query_matches_dig();
    test_query_cname_matches_dig();
    test_decodes_plain_name();
    test_decodes_root();
    test_decodes_pointer();
    test_decodes_pointer_in_rdata();
    test_detects_pointer_loop();
    test_detects_pointer_out_of_range();
    test_next_offset_after_original();
    test_parses_header();
    test_parses_question();
    test_parses_a_record();
    test_walks_every_answer();
    test_nxdomain();
    test_parses_soa();
    test_parses_null_mx();
    test_nodata();
    test_reports_truncation();

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("  (%d FAILED)\n", tests_failed);
        return 1;
    }
    printf("\n");
    return 0;
}
