/*
 * Stage 65.32 -- the entity tag of a representation, and the reading of
 * an If-None-Match field value.
 *
 * Stage 65.31 gave the client the rule that a 1xx, 204 or 304
 * response has no body. This stage gives the server a response that
 * uses it: the 304 (Not Modified), which answers a conditional GET
 * whose condition is false.
 *
 * Three things live here, and they are three different kinds of claim:
 *
 *   1. The VALIDATOR -- a tag for a representation. RFC 9110 8.8.3:
 *      "An entity tag is an opaque validator for differentiating
 *      between multiple representations of the same resource". The
 *      tag this component generates is WEAK, and that is not a
 *      stylistic choice: 8.8.3 says "If an origin server provides an
 *      entity tag for a representation and the generation of that
 *      entity tag does not satisfy all of the characteristics of a
 *      strong validator (Section 8.8.1), then the origin server MUST
 *      mark the entity tag as weak by prefixing its opaque value with
 *      "W/" (case-sensitive)." A hash of the representation's bytes
 *      changes when the bytes change, but it is not collision-free,
 *      so it is not a strong validator (8.8.1) -- and so the "W/" is
 *      required, not optional. The weakness indicator is a claim about
 *      the generator.
 *
 *   2. The COMPARISON -- RFC 9110 8.8.3.2 gives two of them:
 *
 *        "Strong comparison": two entity tags are equivalent if both
 *        are not weak and their opaque-tags match character-by-
 *        character.
 *
 *        "Weak comparison": two entity tags are equivalent if their
 *        opaque-tags match character-by-character, regardless of
 *        either or both being tagged as "weak".
 *
 *      The rule says which one applies: 13.1.2 -- "A recipient MUST
 *      use the weak comparison function when comparing entity tags
 *      for If-None-Match". So "W/"x"" and ""x"" are the same tag for
 *      this purpose, and a server that compared the whole field value
 *      byte for byte would refuse a perfectly good cache.
 *
 *   3. The FIELD VALUE -- RFC 9110 13.1.2: If-None-Match is "*" or a
 *      list of entity tags, and the condition is false if "*" (the
 *      server has a current representation) or if one of the listed
 *      tags matches. "Otherwise, the condition is true."
 *
 * A list is not a list of comma-separated atoms: an entity tag's
 * opaque-tag is a quoted string (8.8.3), and RFC 9110 5.6.1 lets a
 * list element be a quoted-string -- so a comma can be INSIDE an
 * entity tag. "ETag: "a,b"" is legal, and a naive split on commas
 * would read it as two broken tags. So the split here knows about
 * quotes.
 *
 * This file is internal to the component. A validator belongs to the
 * bytes it is computed from, and a comparison belongs to the bytes it
 * compares -- neither is the property of a role (Stage 65.19's
 * lesson).
 */

#ifndef HTTP_ETAG_H
#define HTTP_ETAG_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Room for the longest entity tag this component generates. */
#define HTTP_ETAG_MAX 64

/* FNV-1a, 64-bit.
 *
 * Not a cryptographic hash and not a collision-free one: a hash that
 * can collide is not a strong validator (RFC 9110 8.8.1), which is
 * exactly why the tag built from it is weak. */
static inline unsigned long long http_fnv1a64(const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    unsigned long long h = 1469598103934665603ULL;
    size_t i;

    for (i = 0; i < len; i++) {
        h ^= (unsigned long long)p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/* The weak entity tag for a representation: W/"fnv1a64-<16 hex>".
 *
 * Returns the number of characters written, or -1. */
static inline int http_etag_weak(char *dst, size_t dst_len,
                                 const void *data, size_t len)
{
    int n;

    if (dst == NULL || dst_len == 0) {
        return -1;
    }
    n = snprintf(dst, dst_len, "W/\"fnv1a64-%016llx\"",
                 http_fnv1a64(data, len));
    if (n < 0 || (size_t)n >= dst_len) {
        return -1;
    }
    return n;
}

/* The opaque tag inside an entity tag.
 *
 * An entity tag is [ weak ] opaque-tag, where the weakness indicator
 * is "W/" (case-sensitive, RFC 9110 8.8.3) and the opaque tag is a
 * quoted string.
 *
 * Returns the opaque tag's length -- which may be zero, because the
 * RFC's own examples include ETag: "" -- or -1 if the value is not a
 * well-formed entity tag. */
static inline int http_etag_opaque(const char *tag, size_t tag_len,
                                    const char **opaque)
{
    const char *p = tag;
    size_t len = tag_len;

    if (tag == NULL) {
        return -1;
    }
    if (len >= 2 && p[0] == 'W' && p[1] == '/') {
        p += 2;
        len -= 2;
    }
    if (len < 2 || p[0] != '"' || p[len - 1] != '"') {
        return -1;
    }
    *opaque = p + 1;
    return (int)(len - 2);
}

/* Weak comparison of two entity tags (RFC 9110 8.8.3.2).
 *
 * The opaque tags have to match character for character; whether
 * either tag is weak does not matter. */
static inline int http_etag_weak_equal_n(const char *a, size_t a_len,
                                         const char *b)
{
    const char *oa = NULL;
    const char *ob = NULL;
    int la = http_etag_opaque(a, a_len, &oa);
    int lb = http_etag_opaque(b, b == NULL ? 0 : strlen(b), &ob);

    if (la < 0 || lb < 0) {
        return 0;
    }
    if (la != lb) {
        return 0;
    }
    return la == 0 || memcmp(oa, ob, (size_t)la) == 0;
}

static inline int http_etag_weak_equal(const char *a, const char *b)
{
    if (a == NULL) {
        return 0;
    }
    return http_etag_weak_equal_n(a, strlen(a), b);
}

/* Does the If-None-Match field value match the given entity tag?
 *
 * RFC 9110 13.1.2:
 *
 *   "If the field value is "*", the condition is false if the origin
 *   server has a current representation for the target resource."
 *
 *   "If the field value is a list of entity tags, the condition is
 *   false if one of the listed tags matches the entity tag of the
 *   selected representation."
 *
 *   "Otherwise, the condition is true."
 *
 * Returns 1 when the condition is FALSE -- a match, so the
 * representation has not changed -- and 0 when it is true.
 *
 * A value that is neither "*" nor a list of entity tags matches
 * nothing: it is not a match, so the condition is true and the server
 * performs the method. */
static inline int http_if_none_match_matches(const char *value, const char *etag)
{
    const char *p;
    const char *end;

    if (value == NULL || etag == NULL) {
        return 0;
    }
    p = value;
    end = value + strlen(value);

    /* "*" -- the whole field value, not an element of a list. RFC
     * 9110 13.1.2's note says a list containing "*" is syntactically
     * invalid, so there is no such thing to match here. */
    while (p < end && (*p == ' ' || *p == '\t')) {
        p++;
    }
    if (p < end && *p == '*') {
        const char *t = p + 1;
        while (t < end && (*t == ' ' || *t == '\t')) {
            t++;
        }
        if (t == end) {
            return 1;
        }
    }

    /* A list of entity tags: one match is enough. The commas that
     * separate the elements are the ones OUTSIDE a quoted string --
     * an opaque tag may contain a comma (RFC 9110 5.6.1). */
    p = value;
    while (p < end) {
        const char *s = p;
        const char *e;
        int in_quotes = 0;

        while (p < end) {
            if (*p == '"') {
                in_quotes = !in_quotes;
            } else if (*p == ',' && !in_quotes) {
                break;
            }
            p++;
        }
        e = p;
        while (s < e && (*s == ' ' || *s == '\t')) {
            s++;
        }
        while (e > s && (e[-1] == ' ' || e[-1] == '\t')) {
            e--;
        }
        /* An empty element is not an entity tag (RFC 9110 5.6.1), and
         * a malformed one is not either -- so it matches nothing. */
        if (e > s && http_etag_weak_equal_n(s, (size_t)(e - s), etag)) {
            return 1;
        }
        if (p < end) {
            p++;   /* step over the comma */
        }
    }
    return 0;
}

#endif /* HTTP_ETAG_H */
