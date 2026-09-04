#include "pathval/path_validate.h"

#include <string.h>

static bool segment_is_dotdot(const char *seg, size_t len) {
    return len == 2 && seg[0] == '.' && seg[1] == '.';
}

static bool segment_is_dot(const char *seg, size_t len) {
    return len == 1 && seg[0] == '.';
}

/* Shared segment-by-segment scan used by both public entry points.
 * `entry_path` is assumed non-NULL and non-empty by the caller.
 * A single trailing '/' (a directory entry) is allowed; any other empty
 * segment ("a//b", a leading "/") is not. */
static PathValidateResult scan_segments(const char *entry_path) {
    if (entry_path[0] == '/') {
        return PATH_ERR_NOT_ABSOLUTE;
    }
    if (strchr(entry_path, '\\') != NULL) {
        return PATH_ERR_BACKSLASH;
    }

    const char *seg_start = entry_path;
    for (const char *p = entry_path;; p++) {
        if (*p == '/' || *p == '\0') {
            size_t seg_len = (size_t)(p - seg_start);
            bool is_trailing_slash =
                (*p == '\0' && seg_len == 0 && p != entry_path);

            if (seg_len == 0 && !is_trailing_slash) {
                return PATH_ERR_EMPTY_SEGMENT;
            }
            if (seg_len > 0) {
                if (segment_is_dotdot(seg_start, seg_len) ||
                    segment_is_dot(seg_start, seg_len)) {
                    /* Reject "." too: not dangerous on its own, but any
                     * non-canonical segment here means the caller didn't
                     * normalize the entry before validating it. */
                    return PATH_ERR_TRAVERSAL;
                }
            }
            if (*p == '\0') {
                break;
            }
            seg_start = p + 1;
        }
    }
    return PATH_OK;
}

bool path_validate_entry_is_safe(const char *entry_path) {
    if (entry_path == NULL || entry_path[0] == '\0') {
        return false;
    }
    return scan_segments(entry_path) == PATH_OK;
}

PathValidateResult path_validate_join_entry(const char *dest_root,
                                             const char *entry_path,
                                             char *out, size_t out_capacity) {
    if (dest_root == NULL || dest_root[0] != '/') {
        return PATH_ERR_NOT_ABSOLUTE;
    }
    if (entry_path == NULL || entry_path[0] == '\0') {
        return PATH_ERR_EMPTY;
    }

    PathValidateResult scan_result = scan_segments(entry_path);
    if (scan_result != PATH_OK) {
        return scan_result;
    }

    size_t root_len = strlen(dest_root);
    size_t entry_len = strlen(entry_path);
    bool root_has_trailing_slash = root_len > 0 && dest_root[root_len - 1] == '/';
    size_t sep_len = root_has_trailing_slash ? 0 : 1;
    size_t needed = root_len + sep_len + entry_len + 1; /* + NUL */

    if (needed > out_capacity) {
        return PATH_ERR_TOO_LONG;
    }

    memcpy(out, dest_root, root_len);
    size_t pos = root_len;
    if (sep_len) {
        out[pos++] = '/';
    }
    memcpy(out + pos, entry_path, entry_len);
    pos += entry_len;
    out[pos] = '\0';

    return PATH_OK;
}
