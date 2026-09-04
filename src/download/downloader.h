/* Drives a single real download: HTTP transfer into a temp partial file,
 * storage-space refusal, Range-based resume (best-effort — see
 * docs/architecture.md for why folder-game resume specifically cannot be
 * promised), safe ZIP extraction for folder-based games, and a final
 * rename only after validation succeeds. One download at a time (MVP
 * scope) — this module holds no queue.
 */
#ifndef ROMM_PS5_DOWNLOADER_H
#define ROMM_PS5_DOWNLOADER_H

#include "download/download_manager.h"
#include "net/http_client.h"
#include "romm_api/romm_api.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const HttpClient *http;
    const char *server_url;   /* no trailing slash, e.g. "http://192.168.1.50:3000" */
    const char *auth_header;  /* "Bearer rmm_..." */
    const char *dest_root;    /* e.g. "/data/etaHEN/games"; must already exist */
} DownloaderConfig;

/* Called periodically (roughly once per received chunk) during a transfer
 * so the caller can redraw a progress screen. `progress` is the same
 * struct passed to downloader_run(). */
typedef void (*DownloaderProgressFn)(const DownloadProgress *progress,
                                      void *user_data);

/* Called at the same cadence as DownloaderProgressFn so the caller can
 * poll for a cancel button press without this module needing to know
 * anything about controller input. Returning true aborts the transfer
 * (progress ends in DL_STATE_CANCELLED); the partial file is left in
 * place for a future resume attempt, never silently deleted. */
typedef bool (*DownloaderShouldCancelFn)(void *user_data);

/* Runs one download of `game` to completion, failure, or cancellation.
 * `progress` must already be in DL_STATE_IDLE (call download_progress_init()
 * or transition it back to IDLE yourself before a retry) — this function
 * drives it through DOWNLOADING -> [EXTRACTING] -> VALIDATING -> COMPLETED,
 * or to FAILED/CANCELLED with a reason on the way.
 *
 * Returns true only if `progress->state` ended as DL_STATE_COMPLETED.
 * Blocks until done — there is no background thread; on PS5 this must be
 * called from the same loop that also redraws the screen (via
 * on_progress) and polls input (via should_cancel), since both are only
 * invoked from inside this call, not from a separate thread.
 */
bool downloader_run(const DownloaderConfig *config, const RommGame *game,
                     DownloadProgress *progress,
                     DownloaderShouldCancelFn should_cancel,
                     DownloaderProgressFn on_progress, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_DOWNLOADER_H */
