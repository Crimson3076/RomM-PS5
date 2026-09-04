/* Builds a minimal ZIP_STORED byte buffer for testing zip_extract.c and
 * downloader.c's folder-game path, without needing a real ZIP tool or a
 * central directory (zip_extract_stored never reads one — see its own
 * file comment — so a buffer holding only back-to-back local file
 * headers, ending in a clean EOF, is a fully valid input for it). */
#ifndef ROMM_PS5_TEST_SYNTHETIC_ZIP_H
#define ROMM_PS5_TEST_SYNTHETIC_ZIP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    const uint8_t *content;
    size_t content_len;
    uint16_t method;    /* 0 = stored; anything else exercises rejection */
    uint16_t gp_flag;   /* 0 normally; test cases can set the data-
                          * descriptor bit (0x0008) */
} SyntheticZipEntry;

/* Appends `entries[0..entry_count)` as local-file-header records into
 * `out`. Returns the number of bytes written, or 0 if `out_capacity` was
 * too small. */
size_t build_synthetic_zip(uint8_t *out, size_t out_capacity,
                            const SyntheticZipEntry *entries,
                            size_t entry_count);

#endif /* ROMM_PS5_TEST_SYNTHETIC_ZIP_H */
