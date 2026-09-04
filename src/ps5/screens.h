/* Drawing for this MVP's three on-console screens: a startup/status
 * message, the controller-navigable game list, and the download-progress
 * screen. Deliberately text-only/basic (no cover art, no animation) per
 * this milestone's explicit UI scope — see the handoff report. Each
 * function does one full draw + present (src/ps5/video.h's video_present)
 * per call; main_ps5.c's loop calls one of these once per iteration.
 */
#ifndef ROMM_PS5_SCREENS_H
#define ROMM_PS5_SCREENS_H

#include "download/download_manager.h"
#include "ps5/video.h"
#include "romm_api/romm_api.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Full-screen single message, used for startup progress ("Connecting to
 * RomM server...") and fatal errors before the game list is available. */
void screen_draw_message(Video *video, const char *title, const char *body);

/* The game list, `games[0..count)`, with `focused_index` highlighted.
 * `status_line`, if non-NULL, is drawn at the bottom (e.g. an error from
 * the last failed action). */
void screen_draw_library(Video *video, const RommGame *games, size_t count,
                          size_t focused_index, const char *status_line);

/* Download progress for `game`, driven by a DownloadProgress this same
 * call also renders bytes transferred/total/speed and any failure_reason
 * from. */
void screen_draw_download(Video *video, const RommGame *game,
                           const DownloadProgress *progress);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_SCREENS_H */
