/* Minimal SDL2 rendering for the PS5 library browser screen.
 *
 * Milestone 1 scope only: renders each game as a placeholder bar (no glyph
 * text yet — see the note in ui.c) with a focus highlight, driven by
 * UiInputEvent from input.h. Cover artwork, real typography, and the other
 * required screens (settings, download progress, etc.) are later
 * milestones; this exists to prove the render loop, window lifecycle, and
 * DualSense-driven focus movement work end-to-end before building on top of
 * them.
 */
#ifndef ROMM_PS5_UI_H
#define ROMM_PS5_UI_H

#include "romm_api/romm_api.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *window;   /* SDL_Window* */
    void *renderer; /* SDL_Renderer* */
    int width;
    int height;
} UiContext;

/* Initializes SDL video, creates a window+renderer. `title` is the window
 * title (ignored on PS5, useful when running the desktop build). Returns
 * false on failure (logs the SDL error via log_error). */
bool ui_init(UiContext *ui, const char *title, int width, int height);
void ui_shutdown(UiContext *ui);

/* Clears the frame, draws `games[0..count)` as a vertical list with
 * `focused_index` highlighted, and presents. `focused_index` outside
 * [0, count) draws no highlight. */
void ui_render_game_list(UiContext *ui, const RommGame *games, size_t count,
                          size_t focused_index);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_UI_H */
