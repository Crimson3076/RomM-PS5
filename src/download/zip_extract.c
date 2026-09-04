#include "download/zip_extract.h"
#include "log/log.h"
#include "pathval/path_validate.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOCAL_FILE_HEADER_SIGNATURE 0x04034b50u
#define ZIP64_EXTRA_FIELD_ID 0x0001u
#define SENTINEL_32 0xFFFFFFFFu
#define GP_FLAG_DATA_DESCRIPTOR 0x0008u

#define ZIP_MAX_NAME_LEN 4096
#define ZIP_COPY_CHUNK 65536

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t read_u64_le(const uint8_t *p) {
    uint64_t low = read_u32_le(p);
    uint64_t high = read_u32_le(p + 4);
    return low | (high << 32);
}

static bool read_exact(FILE *f, void *buf, size_t len) {
    return fread(buf, 1, len, f) == len;
}

/* mkdir -p, stopping cleanly on any error other than "already exists". */
static bool mkdir_p(const char *path) {
    char tmp[4096];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) {
        return false;
    }
    memcpy(tmp, path, len + 1);

    for (char *p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return false;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return false;
    }
    return true;
}

/* Ensures the parent directory of `file_path` exists. */
static bool ensure_parent_dir(const char *file_path) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", file_path);
    char *last_slash = strrchr(tmp, '/');
    if (last_slash == NULL || last_slash == tmp) {
        return true; /* no parent beyond dest_root itself */
    }
    *last_slash = '\0';
    return mkdir_p(tmp);
}

/* Extracts a ZIP64 (uncompressed, compressed) size pair from the extra
 * field if present, matching the local-header encoding order in PKWARE's
 * APPNOTE.TXT (both sizes are stored together when either 32-bit size
 * field is the 0xFFFFFFFF sentinel). Leaves *uncompressed and *compressed
 * unchanged if no ZIP64 extra field is found. */
static void apply_zip64_sizes(const uint8_t *extra, uint16_t extra_len,
                               uint64_t *uncompressed, uint64_t *compressed) {
    uint16_t pos = 0;
    while (pos + 4 <= extra_len) {
        uint16_t id = read_u16_le(extra + pos);
        uint16_t size = read_u16_le(extra + pos + 2);
        if ((uint32_t)(pos + 4 + size) > extra_len) {
            break; /* malformed extra field; ignore rather than overrun */
        }
        if (id == ZIP64_EXTRA_FIELD_ID) {
            uint16_t field_pos = 0;
            if (*uncompressed == SENTINEL_32 && field_pos + 8 <= size) {
                *uncompressed = read_u64_le(extra + pos + 4 + field_pos);
                field_pos += 8;
            }
            if (*compressed == SENTINEL_32 && field_pos + 8 <= size) {
                *compressed = read_u64_le(extra + pos + 4 + field_pos);
            }
            return;
        }
        pos = (uint16_t)(pos + 4 + size);
    }
}

ZipExtractResult zip_extract_stored(const char *zip_path, const char *dest_root,
                                     uint32_t *entries_extracted_out) {
    if (entries_extracted_out != NULL) {
        *entries_extracted_out = 0;
    }

    FILE *zf = fopen(zip_path, "rb");
    if (zf == NULL) {
        log_error("zip_extract: could not open %s", zip_path);
        return ZIP_EXTRACT_ERR_IO;
    }

    if (mkdir_p(dest_root) == false) {
        log_error("zip_extract: could not create destination root %s",
                  dest_root);
        fclose(zf);
        return ZIP_EXTRACT_ERR_IO;
    }

    uint32_t extracted = 0;
    uint8_t *copy_buf = malloc(ZIP_COPY_CHUNK);
    if (copy_buf == NULL) {
        fclose(zf);
        return ZIP_EXTRACT_ERR_IO;
    }

    ZipExtractResult result = ZIP_EXTRACT_OK;

    for (;;) {
        uint8_t sig_buf[4];
        size_t got = fread(sig_buf, 1, sizeof(sig_buf), zf);
        if (got == 0) {
            break; /* clean EOF right at an entry boundary */
        }
        if (got < 4) {
            result = ZIP_EXTRACT_ERR_FORMAT;
            break;
        }
        if (read_u32_le(sig_buf) != LOCAL_FILE_HEADER_SIGNATURE) {
            /* Central directory (or anything else): extraction is
             * complete. RomM's ZIPs need nothing from it — see the
             * file-level comment. */
            break;
        }

        /* Local file header, signature (4 bytes) already consumed above.
         * Remaining 26 bytes, with `rest` offsets shown (verified against
         * PKWARE's APPNOTE.TXT field layout — this is a public, stable
         * spec, unlike the SCE syscalls elsewhere in this project, so
         * it's implemented directly from the spec rather than needing an
         * external reference to copy):
         *   rest[0-1]   version needed to extract
         *   rest[2-3]   general purpose bit flag
         *   rest[4-5]   compression method
         *   rest[6-7]   last mod file time
         *   rest[8-9]   last mod file date
         *   rest[10-13] crc-32
         *   rest[14-17] compressed size
         *   rest[18-21] uncompressed size
         *   rest[22-23] file name length
         *   rest[24-25] extra field length
         */
        uint8_t rest[26];
        if (!read_exact(zf, rest, sizeof(rest))) {
            result = ZIP_EXTRACT_ERR_FORMAT;
            break;
        }

        uint16_t gp_flag = read_u16_le(rest + 2);
        uint16_t method = read_u16_le(rest + 4);
        uint64_t compressed_size = read_u32_le(rest + 14);
        uint64_t uncompressed_size = read_u32_le(rest + 18);
        uint16_t name_len = read_u16_le(rest + 22);
        uint16_t extra_len = read_u16_le(rest + 24);

        if (name_len == 0 || name_len >= ZIP_MAX_NAME_LEN) {
            log_error("zip_extract: entry name length %u out of range",
                      name_len);
            result = ZIP_EXTRACT_ERR_FORMAT;
            break;
        }

        char name[ZIP_MAX_NAME_LEN];
        if (!read_exact(zf, name, name_len)) {
            result = ZIP_EXTRACT_ERR_FORMAT;
            break;
        }
        name[name_len] = '\0';

        /* extra_len is uint16_t (max 65535), so it can never exceed this
         * buffer's size (65536) — no bounds check needed before the read. */
        uint8_t extra[65536];
        if (!read_exact(zf, extra, extra_len)) {
            result = ZIP_EXTRACT_ERR_FORMAT;
            break;
        }

        if (compressed_size == SENTINEL_32 || uncompressed_size == SENTINEL_32) {
            apply_zip64_sizes(extra, extra_len, &uncompressed_size,
                               &compressed_size);
        }

        if ((gp_flag & GP_FLAG_DATA_DESCRIPTOR) != 0 && compressed_size == 0) {
            log_error("zip_extract: entry '%s' uses a streaming data "
                      "descriptor with no upfront size; unsupported",
                      name);
            result = ZIP_EXTRACT_ERR_UNSUPPORTED_ENTRY;
            break;
        }
        if (method != 0 /* stored */) {
            log_error("zip_extract: entry '%s' uses compression method %u, "
                      "only stored (0) is supported",
                      name, method);
            result = ZIP_EXTRACT_ERR_UNSUPPORTED_ENTRY;
            break;
        }
        if (compressed_size != uncompressed_size) {
            log_error("zip_extract: entry '%s' has mismatched stored sizes "
                      "(%llu vs %llu)",
                      name, (unsigned long long)compressed_size,
                      (unsigned long long)uncompressed_size);
            result = ZIP_EXTRACT_ERR_FORMAT;
            break;
        }

        if (!path_validate_entry_is_safe(name)) {
            log_error("zip_extract: rejected unsafe entry path '%s'", name);
            result = ZIP_EXTRACT_ERR_UNSAFE_PATH;
            break;
        }

        char out_path[ZIP_MAX_NAME_LEN + 512];
        PathValidateResult path_result = path_validate_join_entry(
            dest_root, name, out_path, sizeof(out_path));
        if (path_result != PATH_OK) {
            log_error("zip_extract: path validation failed for '%s' (%d)",
                      name, path_result);
            result = ZIP_EXTRACT_ERR_UNSAFE_PATH;
            break;
        }

        size_t out_path_len = strlen(out_path);
        bool is_directory_entry = (out_path_len > 0 &&
                                    out_path[out_path_len - 1] == '/');

        if (is_directory_entry) {
            if (!mkdir_p(out_path)) {
                log_error("zip_extract: could not create directory %s",
                          out_path);
                result = ZIP_EXTRACT_ERR_IO;
                break;
            }
            extracted++;
            continue;
        }

        if (!ensure_parent_dir(out_path)) {
            log_error("zip_extract: could not create parent directory for %s",
                      out_path);
            result = ZIP_EXTRACT_ERR_IO;
            break;
        }

        int fd = open(out_path, O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fd < 0) {
            if (errno == EEXIST) {
                log_error("zip_extract: duplicate archive path '%s'", name);
                result = ZIP_EXTRACT_ERR_DUPLICATE_PATH;
            } else {
                log_error("zip_extract: could not create %s (errno %d)",
                          out_path, errno);
                result = ZIP_EXTRACT_ERR_IO;
            }
            break;
        }

        FILE *out = fdopen(fd, "wb");
        if (out == NULL) {
            close(fd);
            result = ZIP_EXTRACT_ERR_IO;
            break;
        }

        uint64_t remaining = uncompressed_size;
        bool copy_ok = true;
        while (remaining > 0) {
            size_t chunk = remaining < ZIP_COPY_CHUNK ? (size_t)remaining
                                                       : ZIP_COPY_CHUNK;
            if (!read_exact(zf, copy_buf, chunk)) {
                copy_ok = false;
                break;
            }
            if (fwrite(copy_buf, 1, chunk, out) != chunk) {
                copy_ok = false;
                break;
            }
            remaining -= chunk;
        }

        fclose(out);

        if (!copy_ok) {
            log_error("zip_extract: short read/write extracting %s", name);
            result = ZIP_EXTRACT_ERR_IO;
            break;
        }

        extracted++;
    }

    free(copy_buf);
    fclose(zf);

    if (entries_extracted_out != NULL) {
        *entries_extracted_out = extracted;
    }

    return result;
}
