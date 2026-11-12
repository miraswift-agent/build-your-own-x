#include "dns.h"

#include <string.h>

/* DNS puts every multi-byte field in network byte order -- big-endian.
 * The shifts in put16/get16 below do that by hand, on purpose: Stage
 * 65.2's rule is that this component builds its addresses and its
 * byte order itself, with no library helper anywhere in src/, and a
 * guard test in test_stage_65_2.c enforces it. */

static void put16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)((v >> 8) & 0xFF);
    p[1] = (unsigned char)(v & 0xFF);
}

static uint16_t get16(const unsigned char *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

const char *dns_status_name(dns_status st)
{
    switch (st) {
    case DNS_OK:               return "ok";
    case DNS_ERR_FORMAT:       return "format";
    case DNS_ERR_TRUNCATED:    return "truncated";
    case DNS_ERR_NAME:         return "name";
    case DNS_ERR_COMPRESSION:  return "compression";
    case DNS_ERR_NOSPACE:      return "nospace";
    case DNS_ERR_NOTFOUND:     return "notfound";
    }
    return "unknown";
}

const char *dns_type_name(uint16_t type)
{
    switch (type) {
    case DNS_TYPE_A:     return "A";
    case DNS_TYPE_NS:    return "NS";
    case DNS_TYPE_CNAME: return "CNAME";
    case DNS_TYPE_SOA:   return "SOA";
    case DNS_TYPE_PTR:   return "PTR";
    case DNS_TYPE_MX:    return "MX";
    case DNS_TYPE_TXT:   return "TXT";
    case DNS_TYPE_AAAA:  return "AAAA";
    }
    return "?";
}

const char *dns_rcode_name(uint16_t rcode)
{
    switch (rcode) {
    case DNS_RCODE_NOERROR:  return "noerror";
    case DNS_RCODE_FORMERR:  return "formerr";
    case DNS_RCODE_SERVFAIL: return "servfail";
    case DNS_RCODE_NXDOMAIN: return "nxdomain";
    case DNS_RCODE_NOTIMP:   return "notimp";
    case DNS_RCODE_REFUSED:  return "refused";
    }
    return "?";
}

int dns_is_response(const dns_header *h)
{
    return h != NULL && (h->flags & 0x8000) != 0;
}

uint16_t dns_rcode(const dns_header *h)
{
    return h == NULL ? 0 : (uint16_t)(h->flags & DNS_RCODE_MASK);
}

int dns_is_truncated(const dns_header *h)
{
    return h != NULL && (h->flags & 0x0200) != 0;
}

dns_status dns_name_encode(const char *name, unsigned char *out, size_t out_len,
                           size_t *written)
{
    size_t o = 0;
    size_t i;
    size_t len;

    if (name == NULL || out == NULL) {
        return DNS_ERR_FORMAT;
    }
    len = strlen(name);

    /* The root name is the empty name: one zero byte and nothing else. */
    if (len == 0 || (len == 1 && name[0] == '.')) {
        if (out_len < 1) {
            return DNS_ERR_NOSPACE;
        }
        out[0] = 0;
        if (written != NULL) {
            *written = 1;
        }
        return DNS_OK;
    }
    if (len > DNS_MAX_NAME - 1) {
        return DNS_ERR_NAME;
    }

    i = 0;
    while (i < len) {
        size_t j = i;
        size_t labellen;

        while (j < len && name[j] != '.') {
            j++;
        }
        labellen = j - i;
        if (labellen == 0) {
            return DNS_ERR_NAME;            /* an empty label */
        }
        if (labellen > DNS_MAX_LABEL) {
            return DNS_ERR_NAME;
        }
        if (o + 1 + labellen > out_len) {
            return DNS_ERR_NOSPACE;
        }
        out[o++] = (unsigned char)labellen;
        memcpy(out + o, name + i, labellen);
        o += labellen;
        i = j + 1;
    }
    if (o + 1 > out_len) {
        return DNS_ERR_NOSPACE;
    }
    out[o++] = 0;
    if (written != NULL) {
        *written = o;
    }
    return DNS_OK;
}

dns_status dns_name_decode(const unsigned char *buf, size_t len, size_t off,
                           char *out, size_t out_len, size_t *next_off)
{
    size_t o = 0;         /* bytes written to out */
    size_t p = off;       /* where we are reading */
    size_t consumed = 0;  /* bytes of the name as it appears at off */
    int jumped = 0;
    int hops = 0;

    if (buf == NULL || out == NULL || out_len < 2) {
        return DNS_ERR_FORMAT;
    }
    out[0] = '\0';

    for (;;) {
        unsigned char l;

        if (p >= len) {
            return DNS_ERR_TRUNCATED;
        }
        l = buf[p];

        if ((l & 0xC0) == 0xC0) {
            size_t target;

            if (p + 1 >= len) {
                return DNS_ERR_TRUNCATED;
            }
            target = (size_t)(((size_t)(l & 0x3F) << 8) | (size_t)buf[p + 1]);
            if (!jumped) {
                /* The name ends AT the pointer: the two bytes of the
                 * pointer are the last two bytes of the original name. */
                consumed = (p + 2) - off;
                jumped = 1;
            }
            if (target >= len) {
                return DNS_ERR_COMPRESSION;
            }
            if (target == p) {
                return DNS_ERR_COMPRESSION;  /* a pointer to itself */
            }
            if (++hops > 128) {
                return DNS_ERR_COMPRESSION;  /* a pointer loop */
            }
            p = target;
            continue;
        }
        if ((l & 0xC0) != 0) {
            return DNS_ERR_FORMAT;          /* a reserved label type */
        }
        if (l == 0) {
            if (!jumped) {
                consumed = (p + 1) - off;
            }
            break;
        }
        if (p + 1 + (size_t)l > len) {
            return DNS_ERR_TRUNCATED;
        }
        if (o + (size_t)l + 1 > out_len) {
            return DNS_ERR_NOSPACE;
        }
        if (o > 0) {
            out[o++] = '.';
        }
        memcpy(out + o, buf + p + 1, l);
        o += (size_t)l;
        p += 1 + (size_t)l;
    }

    if (o == 0) {
        out[0] = '.';
        out[1] = '\0';
    } else {
        out[o] = '\0';
    }
    if (next_off != NULL) {
        *next_off = off + consumed;
    }
    return DNS_OK;
}

dns_status dns_build_query(const char *name, uint16_t type, uint16_t id,
                           unsigned char *out, size_t out_len, size_t *written)
{
    size_t n = DNS_HEADER_LEN;
    size_t used = 0;
    dns_status st;

    if (out == NULL || out_len < DNS_HEADER_LEN) {
        return DNS_ERR_NOSPACE;
    }
    out[0] = (unsigned char)((id >> 8) & 0xFF);
    out[1] = (unsigned char)(id & 0xFF);
    out[2] = 0x01;              /* flags: RD (recursion desired) */
    out[3] = 0x00;
    out[4] = 0x00; out[5] = 0x01; /* QDCOUNT = 1 */
    out[6] = 0x00; out[7] = 0x00; /* ANCOUNT = 0 */
    out[8] = 0x00; out[9] = 0x00; /* NSCOUNT = 0 */
    out[10] = 0x00; out[11] = 0x00; /* ARCOUNT = 0 */

    st = dns_name_encode(name, out + n, out_len - n, &used);
    if (st != DNS_OK) {
        return st;
    }
    n += used;
    if (n + 4 > out_len) {
        return DNS_ERR_NOSPACE;
    }
    put16(out + n, type);
    n += 2;
    put16(out + n, DNS_CLASS_IN);
    n += 2;
    if (written != NULL) {
        *written = n;
    }
    return DNS_OK;
}

dns_status dns_parse_header(const unsigned char *buf, size_t len, dns_header *h)
{
    if (buf == NULL || h == NULL) {
        return DNS_ERR_FORMAT;
    }
    if (len < DNS_HEADER_LEN) {
        return DNS_ERR_TRUNCATED;
    }
    h->id = get16(buf);
    h->flags = get16(buf + 2);
    h->qdcount = get16(buf + 4);
    h->ancount = get16(buf + 6);
    h->nscount = get16(buf + 8);
    h->arcount = get16(buf + 10);
    return DNS_OK;
}

dns_status dns_parse_question(const unsigned char *buf, size_t len, size_t off,
                               dns_question *q, size_t *next_off)
{
    dns_status st;
    size_t after = 0;

    if (buf == NULL || q == NULL) {
        return DNS_ERR_FORMAT;
    }
    /* dns_name_decode hands back an ABSOLUTE offset, not a length --
     * "after" is where the next field starts. */
    st = dns_name_decode(buf, len, off, q->name, sizeof q->name, &after);
    if (st != DNS_OK) {
        return st;
    }
    if (after > len || len - after < 4) {
        return DNS_ERR_TRUNCATED;
    }
    q->type = get16(buf + after);
    q->class_ = get16(buf + after + 2);
    if (next_off != NULL) {
        *next_off = after + 4;
    }
    return DNS_OK;
}

dns_status dns_parse_record(const unsigned char *buf, size_t len, size_t off,
                             dns_record *rr, size_t *next_off)
{
    dns_status st;
    size_t after = 0;
    size_t rdlen;
    size_t rdstart;

    if (buf == NULL || rr == NULL) {
        return DNS_ERR_FORMAT;
    }
    /* Again: "after" is an absolute offset, not a length. */
    st = dns_name_decode(buf, len, off, rr->name, sizeof rr->name, &after);
    if (st != DNS_OK) {
        return st;
    }
    if (after > len || len - after < 10) {
        return DNS_ERR_TRUNCATED;
    }
    off = after;
    rr->type = get16(buf + off);
    rr->class_ = get16(buf + off + 2);
    rr->ttl = ((uint32_t)get16(buf + off + 4) << 16) | (uint32_t)get16(buf + off + 6);
    rdlen = (size_t)get16(buf + off + 8);
    rdstart = off + 10;
    if (rdlen > len - rdstart) {
        return DNS_ERR_TRUNCATED;
    }
    rr->rdlength = (uint16_t)rdlen;
    rr->rdata = buf + rdstart;
    if (next_off != NULL) {
        *next_off = rdstart + rdlen;
    }
    return DNS_OK;
}
