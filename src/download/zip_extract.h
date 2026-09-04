/* Safe, streaming extraction for RomM's folder-download ZIPs.
 *
 * Deliberately supports ONLY the ZIP_STORED (uncompressed) method with
 * sizes known up front in each local file header — not general-purpose
 * ZIP. Per docs/architecture.md §3/§4 (verified against RomM's own
 * backend source, not just external docs), every ZIP RomM produces for a
 * folder-based game — both the live mod_zip stream and the Range-triggered
 * server-side cache build — is exactly this shape. Anything else (a real
 * DEFLATE-compressed entry, or a streaming entry using the data-descriptor
 * flag instead of an upfront size) is outside what this project has
 * verified and is rejected with a clear error rather than guessed at.
 *
 * Every entry path is validated with pathval's path_validate_join_entry()
 * before anything is written — rejects path traversal, absolute paths,
 * and malformed segments. Every output file is created with O_EXCL, so a
 * second entry claiming the same path fails cleanly instead of silently
 * overwriting the first (this is also how duplicate archive paths are
 * caught, without needing to track every path seen in memory).
 */
#ifndef ROMM_PS5_ZIP_EXTRACT_H
#define ROMM_PS5_ZIP_EXTRACT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ZIP_EXTRACT_OK = 0,
    ZIP_EXTRACT_ERR_IO,                    /* couldn't open/read/write a file */
    ZIP_EXTRACT_ERR_FORMAT,                /* malformed or inconsistent header */
    ZIP_EXTRACT_ERR_UNSUPPORTED_ENTRY,     /* compressed, or a streaming/
                                             * data-descriptor entry with no
                                             * upfront size — see file comment */
    ZIP_EXTRACT_ERR_UNSAFE_PATH,           /* traversal/absolute entry path */
    ZIP_EXTRACT_ERR_DUPLICATE_PATH,        /* two entries claim the same output path */
} ZipExtractResult;

/* Extracts every entry in the ZIP at `zip_path` into `dest_root`
 * (created if it doesn't exist; must be an absolute, already-canonical
 * path — see pathval/path_validate.h). Stops at the first error, leaving
 * whatever was already extracted in place — the caller (downloader.c) is
 * responsible for deciding whether a partial extraction should be cleaned
 * up (this module never recursively deletes anything itself, per this
 * project's filesystem-safety rules).
 *
 * `entries_extracted_out`, if non-NULL, receives the count of entries
 * successfully extracted before any error (or the total count on success).
 */
ZipExtractResult zip_extract_stored(const char *zip_path, const char *dest_root,
                                     uint32_t *entries_extracted_out);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_ZIP_EXTRACT_H */
