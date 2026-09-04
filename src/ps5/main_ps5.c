/* RomM-PS5 — real PS5-native application entry point.
 *
 * This is the first end-to-end vertical slice: load credentials, connect
 * to a real RomM server, list the PS5 library, let the user pick a game
 * with a DualSense, download it with live progress, and extract it — all
 * on-console, no mock data. It supersedes the earlier cross-compilation
 * milestone's minimal hello-world ELF (see git history for that file's
 * prior content); this file now owns the real application loop instead
 * of just proving the toolchain works.
 *
 * Every PS5-native building block this wires together documents its own
 * verified-vs-inferred status where it's implemented, not here:
 *   - src/net/http_client_ps5.c   (SceNet/SceSsl/SceHttp2)
 *   - src/ps5/pad.c                (ScePad + SceUserService)
 *   - src/ps5/video.c              (sceVideoOut direct framebuffer scanout)
 * This file's own job is orchestration and UI flow, not SCE ABI calls
 * directly (besides the small, already-verified SceUserService lifecycle
 * pair and sceKernelGetHwModelName kept from the earlier milestone).
 *
 * This path has run on a real CFI-1215A Z2X through VideoOut setup,
 * UserService/Pad startup, config loading, and a real HTTPS send attempt.
 * See docs/testing.md for the precise current verification boundary.
 */
#include "config/credentials.h"
#include "download/download_manager.h"
#include "download/downloader.h"
#include "log/log.h"
#include "net/http_client.h"
#include "net/http_client_ps5.h"
#include "ps5/pad.h"
#include "ps5/screens.h"
#include "ps5/video.h"
#include "romm_api/romm_api.h"
#include "romm_api/romm_api_http.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* samples/browser/main.c — the only SDK-demonstrated UserService calls;
 * confirmed working on real hardware (see docs/testing.md "First
 * hardware test"). Needed before sceUserServiceGetLoginUserIdList
 * (called from src/ps5/pad.c) can return anything meaningful. */
int sceUserServiceInitialize(void *);
int sceUserServiceTerminate(void);

/* samples/hwinfo/main.c — confirmed working; kept as a cheap diagnostic
 * line in the persistent log for hardware test reports. */
int sceKernelGetHwModelName(char *);

#define APP_DIR "/data/homebrew/RomM-PS5"
#define CONFIG_PATH APP_DIR "/config.json"
#define LOG_PATH APP_DIR "/romm-ps5.log"
#define GAMES_DIR "/data/etaHEN/games"

#define LIBRARY_PAGE_LIMIT 500
#define WAIT_FOR_INPUT_MAX_FRAMES 3600 /* bounded so a missing/dead pad
                                         * can never hang shutdown */

typedef struct {
    Video *video;
    PadState *pad;
    const RommGame *game;
    time_t last_draw_time;
} DownloadUiContext;

static void download_on_progress(const DownloadProgress *progress,
                                  void *user_data) {
    DownloadUiContext *ui = (DownloadUiContext *)user_data;
    time_t now = time(NULL);
    /* Redrawing (and thus flipping/vsync-waiting) on every received chunk
     * would cap transfer throughput at the display's flip rate instead of
     * the network's — throttle to roughly once per second, matching the
     * granularity src/download/downloader.c already uses for its own
     * speed_bytes_per_sec calculation. */
    if (ui->last_draw_time == 0 || now != ui->last_draw_time) {
        screen_draw_download(ui->video, ui->game, progress);
        ui->last_draw_time = now;
    }
}

static bool download_should_cancel(void *user_data) {
    DownloadUiContext *ui = (DownloadUiContext *)user_data;
    PadData pad_data;
    memset(&pad_data, 0, sizeof(pad_data));
    if (!pad_read(ui->pad, &pad_data)) {
        return false;
    }
    return (pad_data.buttons & PAD_BUTTON_CIRCLE) != 0;
}

/* Redraws the already-drawn message screen while polling for Cross/Circle,
 * so startup errors are visible and dismissable rather than the app just
 * hanging or vanishing. Bounded to WAIT_FOR_INPUT_MAX_FRAMES so this exits
 * cleanly even with no controller connected at all. */
static void wait_for_confirm(Video *video, PadState *pad) {
    uint32_t prev_buttons = 0;
    for (int frame = 0; frame < WAIT_FOR_INPUT_MAX_FRAMES; frame++) {
        PadData pad_data;
        memset(&pad_data, 0, sizeof(pad_data));
        bool have_pad = pad_read(pad, &pad_data);
        uint32_t buttons = have_pad ? pad_data.buttons : 0;
        uint32_t pressed = buttons & ~prev_buttons;
        prev_buttons = buttons;

        if (pressed & (PAD_BUTTON_CROSS | PAD_BUTTON_CIRCLE)) {
            return;
        }
        if (!video_present(video)) {
            return;
        }
    }
    log_warn("wait_for_confirm: timed out after %d frames with no input",
             WAIT_FOR_INPUT_MAX_FRAMES);
}

int main(void) {
    int32_t user_service_result = sceUserServiceInitialize(0);
    bool user_service_ready = (user_service_result == 0);

    mkdir(APP_DIR, 0755);
    log_init_file_sink(LOG_PATH);
    log_info("RomM-PS5 starting (vertical-slice MVP)");

    log_info("sceUserServiceInitialize returned 0x%x (%d)",
             (unsigned int)user_service_result, (int)user_service_result);
    if (!user_service_ready) {
        log_warn("sceUserServiceInitialize failed (nonfatal); DualSense "
                 "user-id resolution will fall back to PAD_USER_ID_SYSTEM");
    }

    char model[256] = {0};
    if (sceKernelGetHwModelName(model) == 0 && model[0] != '\0') {
        log_info("Hardware model: %s", model);
    }

    mkdir(GAMES_DIR, 0755);

    Video *video = video_init();
    if (video == NULL) {
        log_error("video_init failed; cannot show a UI, exiting");
        log_close_file_sink();
        if (user_service_ready) {
            sceUserServiceTerminate();
        }
        return EXIT_FAILURE;
    }

    PadState pad;
    pad_init(&pad);
    if (!pad_open(&pad)) {
        log_warn("No DualSense available at startup; controller input will "
                 "be unavailable until this app is restarted with one "
                 "connected");
    }

    screen_draw_message(video, "RomM-PS5", "Loading configuration...");

    RommCredentials creds;
    CredentialsResult cred_result = credentials_load(CONFIG_PATH, &creds);
    if (cred_result != CREDENTIALS_OK) {
        const char *body;
        switch (cred_result) {
        case CREDENTIALS_ERR_IO:
            body = "Could not read " CONFIG_PATH
                   " — create it with your RomM server_url and api_token.";
            break;
        case CREDENTIALS_ERR_PARSE:
            body = CONFIG_PATH " is not valid JSON.";
            break;
        default:
            body = CONFIG_PATH
                " is missing \"server_url\" or \"api_token\".";
            break;
        }
        log_error("credentials_load failed (%d): %s", cred_result, body);
        screen_draw_message(video, "Configuration error", body);
        wait_for_confirm(video, &pad);
        goto shutdown;
    }

    screen_draw_message(video, "RomM-PS5", "Starting network...");
    if (!http_client_ps5_startup()) {
        log_error("http_client_ps5_startup failed");
        screen_draw_message(video, "Network error",
                             "Could not initialize PS5 networking "
                             "(SceNet/SceSsl/SceHttp2). See the log.");
        wait_for_confirm(video, &pad);
        goto shutdown;
    }

    HttpClient http;
    http_client_ps5_init(&http);

    RommApiHttpContext api_ctx;
    RommApi api;
    romm_api_http_init(&api, &api_ctx, &http, creds.server_url,
                        creds.api_token);

    screen_draw_message(video, "RomM-PS5", "Connecting to RomM server...");

    RommGamePage page;
    memset(&page, 0, sizeof(page));
    RommResult list_result = api.list_ps5_games(
        api.ctx, NULL, ROMM_SORT_TITLE_ASC, 0, LIBRARY_PAGE_LIMIT, &page);
    if (list_result != ROMM_OK) {
        const char *body;
        switch (list_result) {
        case ROMM_ERR_AUTH:
            body = "Authentication failed — check api_token in "
                   CONFIG_PATH ".";
            break;
        case ROMM_ERR_NOT_FOUND:
            body = "Server has no 'ps5' platform configured.";
            break;
        default:
            body = "Could not reach the RomM server — check server_url and "
                   "network connectivity.";
            break;
        }
        log_error("list_ps5_games failed (%d): %s", list_result, body);
        screen_draw_message(video, "Connection error", body);
        wait_for_confirm(video, &pad);
        goto shutdown;
    }

    log_info("Loaded %zu of %zu PS5 games from RomM", page.count, page.total);

    /* Built locally rather than reading api_ctx.auth_header directly —
     * romm_api_http.h documents that struct's fields as private to
     * romm_api_http.c even though the header can't enforce that in C. */
    char auth_header[ROMM_AUTH_HEADER_MAX];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", creds.api_token);

    DownloaderConfig dl_config = {
        .http = &http,
        .server_url = creds.server_url,
        .auth_header = auth_header,
        .dest_root = GAMES_DIR,
    };

    size_t focused_index = 0;
    char status_line[192] = "";
    uint32_t prev_buttons = 0;
    bool running = true;

    while (running) {
        screen_draw_library(video, page.items, page.count, focused_index,
                             status_line);

        PadData pad_data;
        memset(&pad_data, 0, sizeof(pad_data));
        bool have_pad = pad_read(&pad, &pad_data);
        uint32_t buttons = have_pad ? pad_data.buttons : 0;
        uint32_t pressed = buttons & ~prev_buttons;
        prev_buttons = buttons;

        if (pressed & PAD_BUTTON_UP) {
            if (focused_index > 0) {
                focused_index--;
            }
        }
        if (pressed & PAD_BUTTON_DOWN) {
            if (page.count > 0 && focused_index + 1 < page.count) {
                focused_index++;
            }
        }
        if (pressed & PAD_BUTTON_CIRCLE) {
            running = false;
        }
        if ((pressed & PAD_BUTTON_CROSS) && page.count > 0) {
            const RommGame *game = &page.items[focused_index];

            DownloadProgress progress;
            download_progress_init(&progress);

            DownloadUiContext ui_ctx = {
                .video = video,
                .pad = &pad,
                .game = game,
                .last_draw_time = 0,
            };

            log_info("Starting download: %s (id=%d)", game->title, game->id);
            bool ok =
                downloader_run(&dl_config, game, &progress,
                                download_should_cancel, download_on_progress,
                                &ui_ctx);

            screen_draw_download(video, game, &progress);
            wait_for_confirm(video, &pad);

            if (ok) {
                snprintf(status_line, sizeof(status_line),
                         "Downloaded: %s", game->title);
            } else if (progress.state == DL_STATE_CANCELLED) {
                snprintf(status_line, sizeof(status_line), "Cancelled: %s",
                         game->title);
            } else {
                snprintf(status_line, sizeof(status_line), "Failed: %s (%s)",
                         game->title, progress.failure_reason);
            }
            prev_buttons = 0; /* avoid a stray Cross/Circle from the confirm
                                * wait immediately re-triggering a list action */
        }
    }

    api.list_ps5_games_free(&page);

shutdown:
    log_info("RomM-PS5 exiting cleanly");
    pad_close(&pad);
    video_shutdown(video);
    http_client_ps5_shutdown();

    if (user_service_ready) {
        sceUserServiceTerminate();
    }
    log_close_file_sink();

    return EXIT_SUCCESS;
}
