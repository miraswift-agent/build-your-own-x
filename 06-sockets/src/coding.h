/*
 * Stage 65.24 -- one reading of a transfer-coding list, shared by both
 * roles.
 *
 * The coding header is a LIST, and each coding in it is applied to the
 * body in turn: "gzip, chunked" means the content was compressed and
 * then the compressed content was chunked. Two questions come out of
 * that list, and one reading answers both:
 *
 *   - is the FINAL coding chunked? Only the last coding frames the
 *     body (RFC 9112 6.3 item 4), so only the last one has to be
 *     chunked for the length to be determinate.
 *   - does the list name a coding this role cannot apply? We apply
 *     chunked and nothing else (RFC 9112 6.1: "A recipient MUST be
 *     able to parse the chunked transfer coding").
 *
 * A list may carry EMPTY elements -- RFC 9110 5.6.1: "a recipient MUST
 * accept lists that satisfy the following syntax: #element => [ element
 * ] *( OWS "," OWS [ element ] )" -- so an empty element is not a
 * coding: it is neither the final coding nor an unknown one.
 *
 * This file is internal to the component. The scan is a decoder, and a
 * decoder belongs to the bytes rather than to the role (Stage 65.19's
 * lesson). The server and the client read the same list with the same
 * code, so they cannot drift apart -- and they HAD drifted: the client
 * was still using the old scan (the text after the last comma), so it
 * read "Transfer-Encoding: chunked," as "no coding" and handed back the
 * raw chunk framing as the body.
 */

#ifndef HTTP_CODING_H
#define HTTP_CODING_H

#include <stddef.h>
#include <string.h>
#include <strings.h>

/* What a transfer-coding header says, read once. */
typedef struct {
    int final_is_chunked;   /* the last non-empty element is "chunked" */
    int has_unknown;        /* some non-empty element is a coding we cannot apply */
} http_coding_scan;

/* Does this name name the chunked transfer coding? Coding names are
 * case-insensitive (RFC 9110 5.6.1). */
static inline int http_coding_is_chunked(const char *name, size_t len)
{
    return len == 7 && strncasecmp(name, "chunked", 7) == 0;
}

/* Read the list once and answer both questions.
 *
 * "understood" is the role's own predicate: it says whether a coding
 * name is one this role can apply. The server and the client answer
 * that question the same way today -- chunked, and nothing else -- but
 * each role owns its own answer, because the set of codings a program
 * implements is a property of the program, not of the header. */
static inline void http_coding_scan_list(const char *value,
                                         int (*understood)(const char *name, size_t len),
                                         http_coding_scan *out)
{
    const char *p;

    memset(out, 0, sizeof(*out));
    if (value == NULL) {
        return;
    }
    p = value;
    for (;;) {
        const char *comma = strchr(p, ',');
        const char *end = comma ? comma : p + strlen(p);
        const char *s = p;
        const char *e = end;

        while (s < e && (*s == ' ' || *s == '\t')) {
            s++;
        }
        while (e > s && (e[-1] == ' ' || e[-1] == '\t')) {
            e--;
        }
        if (e > s) {
            /* A non-empty element names a coding. The last one seen is
             * the final coding, and the scan remembers whether any of
             * them is one we cannot apply. */
            out->final_is_chunked = http_coding_is_chunked(s, (size_t)(e - s));
            if (!understood(s, (size_t)(e - s))) {
                out->has_unknown = 1;
            }
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }
}

#endif /* HTTP_CODING_H */
