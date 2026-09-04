#include "storage/storage.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

const char *const STORAGE_CANDIDATE_PATHS[] = {
    "/data/etaHEN/games",
    "/data/homebrew",
    "/mnt/usb0/etaHEN/games",
    "/mnt/usb1/etaHEN/games",
    "/mnt/usb2/etaHEN/games",
    "/mnt/usb3/etaHEN/games",
    "/mnt/usb4/etaHEN/games",
    "/mnt/usb5/etaHEN/games",
    "/mnt/usb6/etaHEN/games",
    "/mnt/usb7/etaHEN/games",
    "/mnt/usb0/homebrew",
    "/mnt/usb1/homebrew",
    "/mnt/usb2/homebrew",
    "/mnt/usb3/homebrew",
    "/mnt/usb4/homebrew",
    "/mnt/usb5/homebrew",
    "/mnt/usb6/homebrew",
    "/mnt/usb7/homebrew",
};

const size_t STORAGE_CANDIDATE_PATH_COUNT =
    sizeof(STORAGE_CANDIDATE_PATHS) / sizeof(STORAGE_CANDIDATE_PATHS[0]);

static bool probe_one(const char *full_path, StorageDestination *dest) {
    struct stat st;
    if (stat(full_path, &st) != 0) {
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        return false;
    }
    if (access(full_path, W_OK) != 0) {
        return false;
    }

    struct statvfs vfs;
    if (statvfs(full_path, &vfs) != 0) {
        return false;
    }

    size_t path_len = strlen(full_path);
    if (path_len >= sizeof(dest->path)) {
        return false; /* refuse to silently truncate a destination path */
    }
    memcpy(dest->path, full_path, path_len + 1);

    /* f_frsize is the fragment size the block counts below are expressed
     * in; f_bsize is not guaranteed to match it. */
    uint64_t frag_size = (uint64_t)(vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize);
    dest->free_bytes = frag_size * (uint64_t)vfs.f_bavail;
    dest->total_bytes = frag_size * (uint64_t)vfs.f_blocks;
    return true;
}

bool storage_get_free_bytes(const char *path, uint64_t *free_bytes_out) {
    struct statvfs vfs;
    if (statvfs(path, &vfs) != 0) {
        return false;
    }
    uint64_t frag_size = (uint64_t)(vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize);
    *free_bytes_out = frag_size * (uint64_t)vfs.f_bavail;
    return true;
}

size_t storage_discover(const char *const *candidates, size_t candidate_count,
                         const char *prefix, StorageDestination *out,
                         size_t out_capacity) {
    size_t written = 0;
    char full_path[STORAGE_PATH_MAX];

    for (size_t i = 0; i < candidate_count && written < out_capacity; i++) {
        int n;
        if (prefix != NULL && prefix[0] != '\0') {
            n = snprintf(full_path, sizeof(full_path), "%s%s", prefix,
                          candidates[i]);
        } else {
            n = snprintf(full_path, sizeof(full_path), "%s", candidates[i]);
        }
        if (n < 0 || (size_t)n >= sizeof(full_path)) {
            continue; /* candidate path too long; skip rather than truncate */
        }

        if (probe_one(full_path, &out[written])) {
            written++;
        }
    }

    return written;
}
