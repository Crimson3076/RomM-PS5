/* Download-manager state machine: legal transitions and progress
 * bookkeeping, deliberately kept free of any real network/filesystem I/O
 * so it stays trivially host-testable. src/download/downloader.c is the
 * module that actually performs a download and drives this state machine;
 * it owns the HttpClient, file I/O, and extraction calls.
 */
#ifndef ROMM_PS5_DOWNLOAD_MANAGER_H
#define ROMM_PS5_DOWNLOAD_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DL_STATE_IDLE = 0,
    DL_STATE_DOWNLOADING,
    DL_STATE_EXTRACTING,
    DL_STATE_VALIDATING,
    DL_STATE_COMPLETED,
    DL_STATE_FAILED,
    DL_STATE_CANCELLED,
} DownloadState;

typedef struct {
    DownloadState state;
    uint64_t bytes_transferred;
    uint64_t bytes_total; /* 0 = unknown */
    uint64_t speed_bytes_per_sec; /* 0 = not yet measured; set directly by
                                    * the caller (src/download/downloader.c),
                                    * not by this module's own functions —
                                    * computing a rate needs wall-clock time,
                                    * which is outside this module's job of
                                    * "is this transition/update legal". */
    char failure_reason[128];
} DownloadProgress;

/* Initializes `p` to DL_STATE_IDLE with zeroed counters. */
void download_progress_init(DownloadProgress *p);

/* Attempts a state transition. Returns true and applies it if the
 * transition is legal from the current state, false (leaving `p`
 * unchanged) otherwise. Callers must check the return value — a rejected
 * transition is a caller bug, not a retryable condition.
 *
 * Legal transitions:
 *   IDLE       -> DOWNLOADING
 *   DOWNLOADING -> EXTRACTING | VALIDATING | FAILED | CANCELLED
 *   EXTRACTING  -> VALIDATING | FAILED | CANCELLED
 *   VALIDATING  -> COMPLETED | FAILED | CANCELLED
 *   COMPLETED   -> IDLE   (starting a new download reuses the slot)
 *   FAILED      -> IDLE | DOWNLOADING (retry)
 *   CANCELLED   -> IDLE | DOWNLOADING (retry)
 *
 * A single-file download with no extraction step may go DOWNLOADING ->
 * VALIDATING directly, skipping EXTRACTING.
 */
bool download_progress_transition(DownloadProgress *p, DownloadState next);

/* Updates transferred/total byte counts. Only legal while DOWNLOADING;
 * returns false (no-op) otherwise, since progress outside that state means
 * a caller bug (e.g. a stray callback firing after cancel). */
bool download_progress_update_bytes(DownloadProgress *p,
                                     uint64_t bytes_transferred,
                                     uint64_t bytes_total);

/* Records a human-readable failure reason and transitions to FAILED in one
 * step. `reason` is copied and truncated to fit `failure_reason`. Returns
 * false (no-op) if FAILED is not reachable from the current state. */
bool download_progress_fail(DownloadProgress *p, const char *reason);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_DOWNLOAD_MANAGER_H */
