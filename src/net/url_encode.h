#ifndef ROMM_PS5_URL_ENCODE_H
#define ROMM_PS5_URL_ENCODE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Percent-encodes `in` into `out` for use as a single URL path segment or
 * query-string value. Unreserved characters (RFC 3986: ALPHA / DIGIT /
 * "-" / "." / "_" / "~") pass through unchanged; everything else becomes
 * %XX. Truncates silently (never overflows `out_capacity`) rather than
 * producing a partially-escaped byte sequence. */
void url_encode(const char *in, char *out, size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_URL_ENCODE_H */
