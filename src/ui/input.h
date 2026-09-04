/* Controller/keyboard input abstraction. On real PS5 hardware this is
 * driven by a DualSense through SDL_GameController; on desktop it also
 * accepts arrow keys / Enter / Escape / a keyboard-mapped controller, so
 * the UI is fully exercisable without a physical pad attached (useful for
 * CI and quick local iteration). Real hardware testing of DualSense
 * mapping specifically is still an unverified, PS5-only item — see
 * docs/testing.md.
 */
#ifndef ROMM_PS5_UI_INPUT_H
#define ROMM_PS5_UI_INPUT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_INPUT_NONE = 0,
    UI_INPUT_UP,
    UI_INPUT_DOWN,
    UI_INPUT_LEFT,
    UI_INPUT_RIGHT,
    UI_INPUT_CONFIRM, /* DualSense Cross / Enter / Space */
    UI_INPUT_CANCEL,  /* DualSense Circle / Escape */
    UI_INPUT_QUIT,     /* window close */
} UiInputEvent;

typedef struct {
    void *game_controller; /* SDL_GameController*, opened lazily; NULL if none connected */
} UiInputState;

void ui_input_init(UiInputState *state);
void ui_input_shutdown(UiInputState *state);

/* Pumps the SDL event queue and returns the single most recent input event
 * of interest, or UI_INPUT_NONE if nothing relevant happened this call.
 * Opens/closes a game controller automatically as SDL reports
 * connect/disconnect events, so a DualSense plugged in mid-session starts
 * working without a restart. */
UiInputEvent ui_input_poll(UiInputState *state);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_UI_INPUT_H */
