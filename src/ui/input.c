#include "ui/input.h"
#include "log/log.h"

#include <SDL.h>

void ui_input_init(UiInputState *state) {
    state->game_controller = NULL;

    int num_joysticks = SDL_NumJoysticks();
    for (int i = 0; i < num_joysticks; i++) {
        if (SDL_IsGameController(i)) {
            state->game_controller = SDL_GameControllerOpen(i);
            if (state->game_controller != NULL) {
                log_info("Opened game controller %d: %s", i,
                         SDL_GameControllerName(
                             (SDL_GameController *)state->game_controller));
                break;
            }
        }
    }
    if (state->game_controller == NULL) {
        log_info("No game controller connected at startup; keyboard input "
                 "is available for desktop testing");
    }
}

void ui_input_shutdown(UiInputState *state) {
    if (state->game_controller != NULL) {
        SDL_GameControllerClose((SDL_GameController *)state->game_controller);
        state->game_controller = NULL;
    }
}

static UiInputEvent map_keydown(SDL_Keycode key) {
    switch (key) {
    case SDLK_UP:
        return UI_INPUT_UP;
    case SDLK_DOWN:
        return UI_INPUT_DOWN;
    case SDLK_LEFT:
        return UI_INPUT_LEFT;
    case SDLK_RIGHT:
        return UI_INPUT_RIGHT;
    case SDLK_RETURN:
    case SDLK_SPACE:
        return UI_INPUT_CONFIRM;
    case SDLK_ESCAPE:
        return UI_INPUT_CANCEL;
    default:
        return UI_INPUT_NONE;
    }
}

static UiInputEvent map_controller_button(Uint8 button) {
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return UI_INPUT_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return UI_INPUT_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return UI_INPUT_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return UI_INPUT_RIGHT;
    case SDL_CONTROLLER_BUTTON_A: /* Cross on a PlayStation-layout pad */
        return UI_INPUT_CONFIRM;
    case SDL_CONTROLLER_BUTTON_B: /* Circle */
        return UI_INPUT_CANCEL;
    default:
        return UI_INPUT_NONE;
    }
}

UiInputEvent ui_input_poll(UiInputState *state) {
    SDL_Event event;
    UiInputEvent result = UI_INPUT_NONE;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            return UI_INPUT_QUIT;

        case SDL_KEYDOWN:
            if (!event.key.repeat) {
                UiInputEvent mapped = map_keydown(event.key.keysym.sym);
                if (mapped != UI_INPUT_NONE) {
                    result = mapped;
                }
            }
            break;

        case SDL_CONTROLLERBUTTONDOWN: {
            UiInputEvent mapped = map_controller_button(event.cbutton.button);
            if (mapped != UI_INPUT_NONE) {
                result = mapped;
            }
            break;
        }

        case SDL_CONTROLLERDEVICEADDED:
            if (state->game_controller == NULL &&
                SDL_IsGameController(event.cdevice.which)) {
                state->game_controller =
                    SDL_GameControllerOpen(event.cdevice.which);
                log_info("Game controller connected");
            }
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
            if (state->game_controller != NULL) {
                SDL_JoystickID id = SDL_JoystickInstanceID(
                    SDL_GameControllerGetJoystick(
                        (SDL_GameController *)state->game_controller));
                if (id == event.cdevice.which) {
                    SDL_GameControllerClose(
                        (SDL_GameController *)state->game_controller);
                    state->game_controller = NULL;
                    log_info("Game controller disconnected");
                }
            }
            break;

        default:
            break;
        }
    }

    return result;
}
