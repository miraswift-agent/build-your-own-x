/*
 * Stage 65.8 -- DNS from scratch, first half: the packets.
 *
 * The lesson: a DNS message is not a string. It is a length-framed
 * structure whose names may be *compressed*, and the compression is
 * where every hand-rolled DNS implementation breaks.
 *
 * Stage 65.4 taught that a short read is normal. Stage 65.6 taught that
 * HTTP frames itself in the text. DNS frames itself in binary, with
 * every multi-byte field in network byte order, and with a name field
 * that may be either a sequence of length-prefixed labels or a 2-byte
 * pointer back to an earlier name. RFC 1035 calls the second form
 * "message compression", and it exists because a response repeats the
 * question's name in every answer record.
 *
 * So the three rules this stage is about:
 *
 *   - a name is a sequence of labels, each a length byte plus bytes,
 *     terminated by a zero byte; the root is the empty name;
 *   - the top two bits of a length byte being 11 means "the next 14
 *     bits are an offset back to an earlier name" -- and a name ends
 *     AT the pointer, so the bytes after the pointer are not part of
 *     it;
 *   - the length of a record's data is *given* (RDLENGTH), so you
 *     never scan for a terminator inside it. This is the Content-Length
 *     lesson again, in binary.
 *
 * Nothing here opens a socket. The transport (UDP, then the cache) is
 * the half after this one; this half is the packets.
 */

#ifndef DNS_H
#define DNS_H

#include <stddef.h>
#include <stdint.h>

#define DNS_MAX_NAME    256   /* a name is at most 255 bytes on the wire */
#define DNS_MAX_LABEL   63
#define DNS_MAX_MSG     512   /* the classic UDP limit */
#define DNS_HEADER_LEN  12

#define DNS_TYPE_A      1
#define DNS_TYPE_NS     2
#define DNS_TYPE_CNAME  5
#define DNS_TYPE_SOA    6
#define DNS_TYPE_PTR    12
#define DNS_TYPE_MX     15
#define DNS_TYPE_TXT    16
#define DNS_TYPE_AAAA   28

#define DNS_CLASS_IN    1

/* The rcode lives in the low four bits of the flags word. */
#define DNS_RCODE_MASK  0x000F
#define DNS_RCODE_NOERROR  0
#define DNS_RCODE_FORMERR  1
#define DNS_RCODE_SERVFAIL 2
#define DNS_RCODE_NXDOMAIN 3
#define DNS_RCODE_NOTIMP   4
#define DNS_RCODE_REFUSED  5

typedef enum {
    DNS_OK = 0,
    DNS_ERR_FORMAT,       /* the bytes are not a DNS message */
    DNS_ERR_TRUNCATED,    /* the message ended in the middle of something */
    DNS_ERR_NAME,         /* an illegal name (label too long, empty label) */
    DNS_ERR_COMPRESSION,  /* a pointer that loops or points nowhere */
    DNS_ERR_NOSPACE,      /* the output buffer is too small */
    DNS_ERR_NOTFOUND,     /* the thing asked for is not in the message */
    /* Stage 65.9 -- the transport. A lookup is an exchange, and the
     * exchange has outcomes that are not "malformed message". */
    DNS_ERR_TIMEOUT,      /* no answer arrived in time */
    DNS_ERR_IO,           /* the socket failed */
    DNS_ERR_ID_MISMATCH,  /* the answer is for a different query */
    DNS_ERR_QUESTION,     /* the answer is for a different question */
    DNS_ERR_SOURCE,       /* the answer came from somewhere else */
    DNS_ERR_NXDOMAIN,     /* the name does not exist */
    DNS_ERR_RCODE         /* the server refused (SERVFAIL, REFUSED, ...) */
} dns_status;

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header;

typedef struct {
    char     name[DNS_MAX_NAME];
    uint16_t type;
    uint16_t class_;
} dns_question;

typedef struct {
    char     name[DNS_MAX_NAME];
    uint16_t type;
    uint16_t class_;
    uint32_t ttl;
    uint16_t rdlength;
    /* Points into the caller's buffer -- it is not copied, and it is
     * only valid as long as that buffer is. */
    const unsigned char *rdata;
} dns_record;

const char *dns_status_name(dns_status st);
const char *dns_type_name(uint16_t type);
const char *dns_rcode_name(uint16_t rcode);

/* True if the QR bit says this is a response rather than a query. */
int dns_is_response(const dns_header *h);
uint16_t dns_rcode(const dns_header *h);
/* True if the TC bit says the message was truncated. */
int dns_is_truncated(const dns_header *h);

/* Encode "example.com" as 07 'e' 'x' ... 03 'c' 'o' 'm' 00.
 * The root "." is a single zero byte. */
dns_status dns_name_encode(const char *name, unsigned char *out, size_t out_len,
                           size_t *written);

/* Decode a name at off, following compression pointers if there are
 * any. next_off (optional) receives the offset just past the name's
 * *original* encoding -- which is not where a pointer's target ends. */
dns_status dns_name_decode(const unsigned char *buf, size_t len, size_t off,
                           char *out, size_t out_len, size_t *next_off);

/* Build a standard recursive query. */
dns_status dns_build_query(const char *name, uint16_t type, uint16_t id,
                           unsigned char *out, size_t out_len, size_t *written);

dns_status dns_parse_header(const unsigned char *buf, size_t len, dns_header *h);
dns_status dns_parse_question(const unsigned char *buf, size_t len, size_t off,
                               dns_question *q, size_t *next_off);
dns_status dns_parse_record(const unsigned char *buf, size_t len, size_t off,
                             dns_record *rr, size_t *next_off);

#endif /* DNS_H */
