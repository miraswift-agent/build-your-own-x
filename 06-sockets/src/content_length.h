/*
 * Stage 65.25 -- one reading of a Content-Length field value, shared by
 * both roles.
 *
 * Stage 65.26 -- the COMBINED field value, when the field is repeated.
 *
 * The length is the other way a message says where its body ends, and
 * RFC 9112 6.3 item 5 is the rule:
 *
 *   "If a message is received without Transfer-Encoding and with an
 *    invalid Content-Length header field, then the message framing is
 *    invalid and the recipient MUST treat it as an unrecoverable error,
 *    unless the field value can be successfully parsed as a
 *    comma-separated list (Section 5.6.1 of [HTTP]), all values in the
 *    list are valid, and all values in the list are the same (in which
 *    case, the message is processed with that single value used as the
 *    Content-Length field value)."
 *
 * So the field value is not one number -- it is a LIST of numbers, and
 * the list has one legal shape: every value valid, every value the
 * same. That is the exception, and it is an exception the RFC writes
 * out because the common case (a duplicated header combined by an
 * upstream processor) is not a number but a pair of them.
 *
 * What a length may LOOK like is RFC 9110 8.6: "Content-Length =
 * 1*DIGIT". A sign is not a digit, a space is not a digit, and an empty
 * element is not a value (RFC 9110 5.6.1: a recipient must accept
 * lists that contain empty elements -- the same rule the
 * transfer-coding list follows).
 *
 * Stage 65.26 adds the other half of "the field value": a message can
 * carry the field MORE THAN ONCE, and then there is no single field
 * value to read -- there is a COMBINED one. RFC 9110 5.2:
 *
 *   "When a field name is repeated within a section, its combined
 *    field value consists of the list of corresponding field line
 *    values within that section, concatenated in order, with each
 *    field line value separated by a comma."
 *
 * So "Content-Length: 5" twice has the combined value "5, 5" -- the
 * list of equal values Stage 65.25 already accepts as one value -- and
 * "Content-Length: 5" with "Content-Length: 6" has the combined value
 * "5, 6" -- a list whose values differ, which is an invalid framing.
 *
 * Reading the combined value does not need the combined string: the
 * combined value's element list is the concatenation of the field
 * lines' element lists, so reading the elements of each line in order
 * IS reading the combined value. (The one place the two readings
 * could differ -- an empty field line -- differs only in a case the
 * list rules already answer: an empty element is not a value, and it
 * is skipped, which is what the concatenation does too.)
 *
 * This file is internal to the component. The reader is a decoder, and
 * a decoder belongs to the bytes rather than to the role (Stage 65.19's
 * lesson). The two roles HAD drifted here: the server's strtol
 * accepted a sign ("+5") and the client's digit loop did not, so the
 * same header meant two different things on the two sides of a
 * socket. They had ALSO drifted on repetition: both took the first
 * field line and ignored the rest, so two lines naming different
 * lengths were framed by the first -- a wrong accept, and the
 * request-smuggling shape.
 *
 * What each role does with an invalid length is the role's own: the
 * server answers 400 and closes, the user agent closes and discards
 * (RFC 9112 6.3 item 5). What a length may be is not.
 */

#ifndef HTTP_CONTENT_LENGTH_H
#define HTTP_CONTENT_LENGTH_H

#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

#include "http.h"

/* The result of reading a Content-Length field value. */
enum {
    HTTP_CL_ABSENT  = 0,   /* the header is not there */
    HTTP_CL_OK      = 1,   /* a usable length */
    HTTP_CL_INVALID = -1   /* the framing is invalid */
};

/* Read a LIST of field values as ONE combined field value.
 *
 * The values are read in order, and each one is a comma-separated list
 * of numbers (RFC 9110 5.6.1). The result is the list's one value:
 * every element valid, every element the same.
 *
 *   values {"5", "5"}      -> 5      the combined value "5, 5"
 *   values {"5", "6"}      -> invalid  the combined value "5, 6"
 *   values {"5, 5", "5"}   -> 5      the combined value "5, 5, 5"
 *   values {""}            -> invalid  present, but no value in it
 *   values {}              -> absent   no field line at all
 *
 * The numbers are compared as NUMBERS, not as text: "05, 5" names one
 * value, 5, because a length is a decimal number of octets (RFC 9110
 * 8.6) and 05 and 5 are the same number.
 */
static inline int http_content_length_scan(const char *const *values,
                                            int nvalues, long *out)
{
    long seen = 0;
    int have = 0;
    int v;

    if (nvalues <= 0) {
        return HTTP_CL_ABSENT;
    }
    for (v = 0; v < nvalues; v++) {
        const char *p = values[v];

        if (!p) {
            continue;
        }
        for (;;) {
            const char *comma = strchr(p, ',');
            const char *end = comma ? comma : p + strlen(p);
            const char *s = p;
            const char *e = end;
            long n = 0;

            while (s < e && (*s == ' ' || *s == '\t')) {
                s++;
            }
            while (e > s && (e[-1] == ' ' || e[-1] == '\t')) {
                e--;
            }
            if (e > s) {
                /* A non-empty element is a value, and a value is 1*DIGIT. */
                const char *d;

                for (d = s; d < e; d++) {
                    if (*d < '0' || *d > '9') {
                        return HTTP_CL_INVALID;
                    }
                    if (n > (LONG_MAX - (*d - '0')) / 10) {
                        return HTTP_CL_INVALID;   /* too long to be a length */
                    }
                    n = n * 10 + (*d - '0');
                }
                if (have && n != seen) {
                    return HTTP_CL_INVALID;       /* the values differ */
                }
                seen = n;
                have = 1;
            }
            if (!comma) {
                break;
            }
            p = comma + 1;
        }
    }
    if (!have) {
        return HTTP_CL_INVALID;   /* present, but no value in it */
    }
    *out = seen;
    return HTTP_CL_OK;
}

/* Read ONE field value.
 *
 *   "5"      -> 5
 *   "5, 5"   -> 5      the list's one value
 *   "5,6"    -> 5      the list's one value
 *   "5,"     -> 5      an empty element is not a value
 *   "5, , 5" -> 5      the same, with the empty element in the middle
 *   "5, 6"   -> invalid  the values differ
 *   "5, abc" -> invalid  one value is not valid
 *   "+5"     -> invalid  a sign is not a digit
 *   "-1"     -> invalid  a negative length is not a length
 *   ""       -> invalid  present, but no value in it
 *   NULL     -> absent   the field is not there
 */
static inline int http_content_length_parse(const char *value, long *out)
{
    const char *values[1];

    if (value == NULL) {
        return HTTP_CL_ABSENT;
    }
    values[0] = value;
    return http_content_length_scan(values, 1, out);
}

/* Read the COMBINED field value of every Content-Length field line in
 * a header block (RFC 9110 5.2).
 *
 * This is the reader for a message that carries the field more than
 * once: the field lines are combined in order, with a comma between
 * them, and the combined value is then read as one list.
 *
 *   one line "5"                  -> 5
 *   two lines "5" and "5"         -> 5      the combined value "5, 5"
 *   two lines "5" and "6"         -> invalid  the combined value "5, 6"
 *   two lines "5" and "abc"       -> invalid
 *   no Content-Length line at all  -> absent
 *
 * The field name is matched case-insensitively (RFC 9110 5.1), so
 * "Content-Length" and "content-length" are the same field.
 */
static inline int http_content_length_collect(const http_header *headers,
                                               int nheaders, long *out)
{
    const char *values[HTTP_MAX_HEADERS];
    int n = 0;
    int i;

    for (i = 0; i < nheaders && i < HTTP_MAX_HEADERS; i++) {
        if (strcasecmp(headers[i].name, "Content-Length") == 0) {
            values[n++] = headers[i].value;
        }
    }
    return http_content_length_scan(values, n, out);
}

#endif /* HTTP_CONTENT_LENGTH_H */
