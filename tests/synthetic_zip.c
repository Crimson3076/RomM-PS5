#include "synthetic_zip.h"

#include <string.h>

static void put_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void put_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

size_t build_synthetic_zip(uint8_t *out, size_t out_capacity,
                            const SyntheticZipEntry *entries,
                            size_t entry_count) {
    size_t pos = 0;

    for (size_t i = 0; i < entry_count; i++) {
        const SyntheticZipEntry *e = &entries[i];
        size_t name_len = strlen(e->name);
        size_t needed = 4 + 26 + name_len + e->content_len;

        if (pos + needed > out_capacity) {
            return 0;
        }

        put_u32_le(out + pos, 0x04034b50u); /* local file header signature */
        pos += 4;

        uint8_t rest[26] = {0};
        put_u16_le(rest + 0, 10);            /* version needed */
        put_u16_le(rest + 2, e->gp_flag);
        put_u16_le(rest + 4, e->method);
        put_u16_le(rest + 6, 0);             /* mod time */
        put_u16_le(rest + 8, 0);             /* mod date */
        put_u32_le(rest + 10, 0);            /* crc-32 (unchecked) */
        put_u32_le(rest + 14, (uint32_t)e->content_len); /* compressed size */
        put_u32_le(rest + 18, (uint32_t)e->content_len); /* uncompressed size */
        put_u16_le(rest + 22, (uint16_t)name_len);
        put_u16_le(rest + 24, 0);            /* extra field length */
        memcpy(out + pos, rest, sizeof(rest));
        pos += sizeof(rest);

        memcpy(out + pos, e->name, name_len);
        pos += name_len;

        if (e->content_len > 0) {
            memcpy(out + pos, e->content, e->content_len);
            pos += e->content_len;
        }
    }

    return pos;
}
