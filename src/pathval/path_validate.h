/* Pure path-safety checks. No filesystem access here — these functions only
 * reason about strings, so they are cheap to unit test on the host and must
 * stay that way. Used to validate both destination paths chosen in the UI
 * and archive entry names before any write happens. */
#ifndef ROMM_PS5_PATH_VALIDATE_H
#define ROMM_PS5_PATH_VALIDATE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PATH_OK = 0,
    PATH_ERR_EMPTY,
    PATH_ERR_NOT_ABSOLUTE,    /* archive entry must be relative; destination root must be absolute */
    PATH_ERR_TRAVERSAL,       /* contains a ".." segment that would escape the root */
    PATH_ERR_EMPTY_SEGMENT,   /* an interior "//" empty segment we won't accept in an entry name */
    PATH_ERR_BACKSLASH,       /* contains '\\'; rejected defensively even on POSIX targets */
    PATH_ERR_TOO_LONG,        /* would exceed out_capacity */
} PathValidateResult;

/* Validates an archive entry's relative path (as it would appear inside a
 * downloaded ZIP) and, if valid, joins it onto `dest_root` into `out`.
 *
 * Rejects:
 *   - absolute entry paths ("/etc/passwd", "C:\\...")
 *   - any ".." path segment, anywhere in the entry
 *   - empty entries and empty interior path segments ("a//b")
 *   - a result that would not fit in `out_capacity`
 *
 * `dest_root` must itself be an absolute, already-canonical path; this
 * function does not canonicalize `dest_root` (that's storage's job — see
 * storage.h) — it only guarantees the *joined* result cannot leave it via
 * the entry name.
 *
 * On success, `out` holds `dest_root` + '/' + `entry_path`, NUL-terminated.
 *
 * Scope note: this operates on a NUL-terminated C string, so it cannot
 * detect a NUL embedded before the archive format's own reported entry-name
 * length (a way a malformed ZIP entry could try to smuggle bytes past a
 * length-aware caller). The archive-extraction module (planned for
 * Milestone 4) must apply that length-aware check itself, on the raw bytes,
 * before ever converting an entry name to a C string and reaching this
 * function.
 */
PathValidateResult path_validate_join_entry(const char *dest_root,
                                             const char *entry_path,
                                             char *out, size_t out_capacity);

/* True if `entry_path` is safe to join per the rules above, without
 * performing the join. Convenience wrapper for call sites that only need a
 * yes/no (e.g. pre-flight scanning every entry in an archive before writing
 * any of them). */
bool path_validate_entry_is_safe(const char *entry_path);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_PATH_VALIDATE_H */
