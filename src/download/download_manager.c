#include "download/download_manager.h"

#include <string.h>

void download_progress_init(DownloadProgress *p) {
    memset(p, 0, sizeof(*p));
    p->state = DL_STATE_IDLE;
}

static bool is_legal_transition(DownloadState from, DownloadState to) {
    switch (from) {
    case DL_STATE_IDLE:
        return to == DL_STATE_DOWNLOADING;
    case DL_STATE_DOWNLOADING:
        return to == DL_STATE_EXTRACTING || to == DL_STATE_VALIDATING ||
               to == DL_STATE_FAILED || to == DL_STATE_CANCELLED;
    case DL_STATE_EXTRACTING:
        return to == DL_STATE_VALIDATING || to == DL_STATE_FAILED ||
               to == DL_STATE_CANCELLED;
    case DL_STATE_VALIDATING:
        return to == DL_STATE_COMPLETED || to == DL_STATE_FAILED ||
               to == DL_STATE_CANCELLED;
    case DL_STATE_COMPLETED:
        return to == DL_STATE_IDLE;
    case DL_STATE_FAILED:
        return to == DL_STATE_IDLE || to == DL_STATE_DOWNLOADING;
    case DL_STATE_CANCELLED:
        return to == DL_STATE_IDLE || to == DL_STATE_DOWNLOADING;
    default:
        return false;
    }
}

bool download_progress_transition(DownloadProgress *p, DownloadState next) {
    if (!is_legal_transition(p->state, next)) {
        return false;
    }
    p->state = next;
    if (next == DL_STATE_IDLE || next == DL_STATE_DOWNLOADING) {
        /* Fresh attempt (first start or retry): counters and any stale
         * failure reason from a previous attempt must not leak forward.
         * A caller resuming a partial download corrects bytes_transferred
         * right back to the on-disk partial size immediately after this
         * call, via download_progress_update_bytes() — legal because
         * that function only requires DL_STATE_DOWNLOADING, which is
         * already true by the time this line runs. */
        p->bytes_transferred = 0;
        p->bytes_total = 0;
        p->speed_bytes_per_sec = 0;
        p->failure_reason[0] = '\0';
    }
    return true;
}

bool download_progress_update_bytes(DownloadProgress *p,
                                     uint64_t bytes_transferred,
                                     uint64_t bytes_total) {
    if (p->state != DL_STATE_DOWNLOADING) {
        return false;
    }
    p->bytes_transferred = bytes_transferred;
    p->bytes_total = bytes_total;
    return true;
}

bool download_progress_fail(DownloadProgress *p, const char *reason) {
    if (!is_legal_transition(p->state, DL_STATE_FAILED)) {
        return false;
    }
    p->state = DL_STATE_FAILED;
    if (reason != NULL) {
        size_t cap = sizeof(p->failure_reason);
        strncpy(p->failure_reason, reason, cap - 1);
        p->failure_reason[cap - 1] = '\0';
    } else {
        p->failure_reason[0] = '\0';
    }
    return true;
}
