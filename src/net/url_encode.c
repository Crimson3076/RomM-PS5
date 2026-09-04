#include "net/url_encode.h"

#include <stdbool.h>

void url_encode(const char *in, char *out, size_t out_capacity) {
    static const char *hex = "0123456789ABCDEF";
    size_t pos = 0;

    if (out_capacity == 0) {
        return;
    }

    for (const unsigned char *p = (const unsigned char *)in; *p != '\0'; p++) {
        bool is_unreserved =
            (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' ||
            *p == '~';

        if (is_unreserved) {
            if (pos + 1 >= out_capacity) {
                break;
            }
            out[pos++] = (char)*p;
        } else {
            if (pos + 3 >= out_capacity) {
                break;
            }
            out[pos++] = '%';
            out[pos++] = hex[(*p >> 4) & 0xF];
            out[pos++] = hex[*p & 0xF];
        }
    }
    out[pos] = '\0';
}
