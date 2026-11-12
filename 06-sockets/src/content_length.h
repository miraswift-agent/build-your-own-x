/*
 * Stage 65.25 -- one reading of a Content-Length field value, shared by
 * both roles.
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
 * This file is internal to the component. The reader is a decoder, and
 * a decoder belongs to the bytes rather than to the role (Stage 65.19's
 * lesson). The two roles HAD drifted here: the server's strtol
 * accepted a sign ("+5") and the client's digit loop did not, so the
 * same header meant two different things on the two sides of a
 * socket.
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

/* The result of reading a Content-Length field value. */
enum {
    HTTP_CL_ABSENT  = 0,   /* the header is not there */
    HTTP_CL_OK      = 1,   /* a usable length */
    HTTP_CL_INVALID = -1   /* the framing is invalid */
};

/* Read the field value.
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
 *
 * The values are compared as NUMBERS, not as text: "05, 5" names one
 * value, 5, because a length is a decimal number of octets (RFC 9110
 * 8.6) and 05 and 5 are the same number.
 */
static inline int http_content_length_parse(const char *value, long *out)
{
    const char *p;
    long seen = 0;
    int have = 0;

    if (value == NULL) {
        return HTTP_CL_ABSENT;
    }
    p = value;
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
    if (!have) {
        return HTTP_CL_INVALID;   /* present, but no value in it */
    }
    *out = seen;
    return HTTP_CL_OK;
}

#endif /* HTTP_CONTENT_LENGTH_H */
