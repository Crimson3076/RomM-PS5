/* Storage-destination discovery. Enumerates the candidate paths from the
 * task's fixed list, keeping only the ones that exist and are writable, and
 * reports free space for each.
 *
 * Real PS5 free-space reporting is UNVERIFIED (see docs/architecture.md
 * §1) — this module uses statvfs(), which is the standard POSIX call and
 * is expected to work on the PS5's FreeBSD-derived userland, but that has
 * not been confirmed on real hardware. Treat the reported free-space value
 * as best-effort until hardware testing (Milestone 4) proves it.
 */
#ifndef ROMM_PS5_STORAGE_H
#define ROMM_PS5_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_PATH_MAX 256

typedef struct {
    char path[STORAGE_PATH_MAX];
    uint64_t free_bytes;
    uint64_t total_bytes;
} StorageDestination;

/* The fixed candidate destination list from the project spec. Not all of
 * these will exist on any given console; storage_discover() filters down to
 * the ones that are actually present and writable. */
extern const char *const STORAGE_CANDIDATE_PATHS[];
extern const size_t STORAGE_CANDIDATE_PATH_COUNT;

/* Probes `candidates[0..candidate_count)` and writes the subset that exist
 * and are writable into `out[0..out_capacity)`, filling `free_bytes`/
 * `total_bytes` via statvfs(). Returns the number of destinations written
 * (<= out_capacity); a candidate list longer than out_capacity silently
 * stops filling `out` once it's full rather than overflowing it, and the
 * return value reflects only what was written.
 *
 * `prefix`, if non-NULL, is prepended to every candidate path before
 * probing — this is the desktop-testability hook: tests point `prefix` at
 * a temp directory instead of probing the real root filesystem. Pass NULL
 * on-device.
 */
size_t storage_discover(const char *const *candidates, size_t candidate_count,
                         const char *prefix, StorageDestination *out,
                         size_t out_capacity);

/* Single-path free-space query, for a caller (src/download/downloader.c)
 * that already knows its destination directory and just needs a fresh
 * space check before starting a transfer, rather than the fixed candidate
 * list storage_discover() enumerates. Returns false (leaving
 * *free_bytes_out unchanged) if `path` doesn't exist or statvfs() fails. */
bool storage_get_free_bytes(const char *path, uint64_t *free_bytes_out);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_STORAGE_H */
