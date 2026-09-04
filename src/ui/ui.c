#include "ui/ui.h"
#include "log/log.h"

#include <SDL.h>

/* No glyph/text rendering yet: pulling in SDL_ttf (or any font) was
 * deliberately deferred rather than added on an unverified PS5 port (see
 * docs/architecture.md's PNG/JPEG/SDL_image risk note, which applies
 * equally to SDL_ttf) — and pixel-accuracy of a hand-rolled bitmap font
 * couldn't be verified in this environment either, since there is no
 * display attached to look at the output. Titles print to the log instead;
 * each row renders as a placeholder bar sized from the title length so the
 * list's shape and focus movement are visible and testable now. Real text
 * rendering is planned for Milestone 2 UI work, once a font approach can
 * actually be checked against a real screen (or, on hardware, a
 * screenshot).
 */

#define ROW_HEIGHT 56
#define ROW_PADDING 12
#define ROW_MARGIN_X 32
#define ROW_MARGIN_TOP 32
#define ROW_MIN_WIDTH 160
#define ROW_WIDTH_PER_CHAR 10
#define BORDER_THICKNESS 3

bool ui_init(UiContext *ui, const char *title, int width, int height) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        log_error("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    SDL_Window *window =
        SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                          width, height, SDL_WINDOW_SHOWN);
    if (window == NULL) {
        log_error("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
        return false;
    }

    SDL_Renderer *renderer =
        SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) {
        log_warn("Accelerated renderer unavailable (%s); falling back to "
                 "software",
                 SDL_GetError());
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (renderer == NULL) {
        log_error("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
        return false;
    }

    ui->window = window;
    ui->renderer = renderer;
    ui->width = width;
    ui->height = height;
    return true;
}

void ui_shutdown(UiContext *ui) {
    if (ui->renderer != NULL) {
        SDL_DestroyRenderer((SDL_Renderer *)ui->renderer);
        ui->renderer = NULL;
    }
    if (ui->window != NULL) {
        SDL_DestroyWindow((SDL_Window *)ui->window);
        ui->window = NULL;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
}

static int row_width_for_title(const char *title, int max_width) {
    int len = (int)SDL_strlen(title);
    int width = ROW_MIN_WIDTH + len * ROW_WIDTH_PER_CHAR;
    return width > max_width ? max_width : width;
}

void ui_render_game_list(UiContext *ui, const RommGame *games, size_t count,
                          size_t focused_index) {
    SDL_Renderer *renderer = (SDL_Renderer *)ui->renderer;
    int max_row_width = ui->width - 2 * ROW_MARGIN_X;

    SDL_SetRenderDrawColor(renderer, 18, 18, 24, 255);
    SDL_RenderClear(renderer);

    for (size_t i = 0; i < count; i++) {
        SDL_Rect row = {
            .x = ROW_MARGIN_X,
            .y = ROW_MARGIN_TOP + (int)i * (ROW_HEIGHT + ROW_PADDING),
            .w = row_width_for_title(games[i].title, max_row_width),
            .h = ROW_HEIGHT,
        };

        bool focused = (i == focused_index);

        if (focused) {
            SDL_SetRenderDrawColor(renderer, 60, 110, 220, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 48, 48, 60, 255);
        }
        SDL_RenderFillRect(renderer, &row);

        if (focused) {
            SDL_SetRenderDrawColor(renderer, 230, 240, 255, 255);
            SDL_Rect border = row;
            for (int t = 0; t < BORDER_THICKNESS; t++) {
                SDL_Rect b = {border.x - t, border.y - t, border.w + 2 * t,
                              border.h + 2 * t};
                SDL_RenderDrawRect(renderer, &b);
            }
        }
    }

    SDL_RenderPresent(renderer);
}
