/* RomM-PS5 application entry point — Milestone 1 foundation.
 *
 * Wires the mock RomM API, the SDL UI, and DualSense/keyboard input
 * together into a single-screen game list with focus navigation. No real
 * network, download, or extraction happens yet; see docs/architecture.md
 * and the Milestone 1 session report for what is and isn't implemented.
 */
#include "config/config.h"
#include "download/download_manager.h"
#include "log/log.h"
#include "romm_api/romm_api_mock.h"
#include "ui/input.h"
#include "ui/ui.h"

#include <stdlib.h>

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720
#define PAGE_LIMIT 50

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    log_info("RomM-PS5 starting (Milestone 1 foundation build)");

    AppConfig config;
    config_set_defaults(&config);
    log_info("Config: selected_destination_index=%d fullscreen=%d",
             config.selected_destination_index, config.fullscreen);

    RommApi api;
    romm_api_mock_init(&api);

    RommGamePage page;
    RommResult list_result =
        api.list_ps5_games(api.ctx, NULL, ROMM_SORT_TITLE_ASC, 0, PAGE_LIMIT,
                            &page);
    if (list_result != ROMM_OK) {
        log_error("Failed to load mock PS5 library (result=%d)", list_result);
        return EXIT_FAILURE;
    }
    log_info("Loaded %zu of %zu mock PS5 games", page.count, page.total);

    UiContext ui;
    if (!ui_init(&ui, "RomM-PS5", WINDOW_WIDTH, WINDOW_HEIGHT)) {
        api.list_ps5_games_free(&page);
        return EXIT_FAILURE;
    }

    UiInputState input;
    ui_input_init(&input);

    DownloadProgress download;
    download_progress_init(&download);

    size_t focused_index = 0;
    bool running = true;

    /* CI/smoke-test hook: with ROMM_PS5_SMOKE_TEST_FRAMES set, run that many
     * frames unattended (paired with SDL_VIDEODRIVER=dummy) and exit 0
     * instead of waiting for a window-close event nobody will send. */
    const char *smoke_frames_env = getenv("ROMM_PS5_SMOKE_TEST_FRAMES");
    long smoke_frames_remaining = smoke_frames_env ? atol(smoke_frames_env) : -1;

    while (running) {
        UiInputEvent event = ui_input_poll(&input);

        switch (event) {
        case UI_INPUT_QUIT:
            running = false;
            break;
        case UI_INPUT_UP:
            if (page.count > 0) {
                focused_index =
                    (focused_index == 0) ? page.count - 1 : focused_index - 1;
            }
            break;
        case UI_INPUT_DOWN:
            if (page.count > 0) {
                focused_index = (focused_index + 1) % page.count;
            }
            break;
        case UI_INPUT_CONFIRM:
            if (focused_index < page.count) {
                log_info("Selected: %s (download not implemented yet)",
                         page.items[focused_index].title);
            }
            break;
        default:
            break;
        }

        ui_render_game_list(&ui, page.items, page.count, focused_index);

        if (smoke_frames_remaining >= 0) {
            smoke_frames_remaining--;
            if (smoke_frames_remaining <= 0) {
                log_info("Smoke test frame budget reached; exiting");
                running = false;
            }
        }
    }

    ui_input_shutdown(&input);
    ui_shutdown(&ui);
    api.list_ps5_games_free(&page);

    log_info("RomM-PS5 exiting");
    return EXIT_SUCCESS;
}
