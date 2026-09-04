#include "ps5/pad.h"
#include "log/log.h"

#include <string.h>

/* --- Verified against ps5-payload-dev/SDL's SDL_ps5joystick.h/.c (see
 * pad.h) --- */
int scePadInit(void);
int scePadOpen(int32_t user_id, int32_t type, int32_t index, void *param);
int scePadGetHandle(int32_t user_id, int32_t type, int32_t index);
int scePadReadState(int32_t handle, PadData *data);
int scePadClose(int32_t handle);

int sceUserServiceGetLoginUserIdList(int32_t userId[4]);

bool pad_init(PadState *state) {
    memset(state, 0, sizeof(*state));
    state->handle = -1;
    state->user_id = -1;

    int32_t result = scePadInit();
    log_info("scePadInit returned 0x%x (%d)", (unsigned int)result,
             (int)result);
    state->available = (result == 0);
    return state->available;
}

static int32_t resolve_user_id(void) {
    int32_t user_ids[4] = {-1, -1, -1, -1};
    int32_t list_result = sceUserServiceGetLoginUserIdList(user_ids);
    log_info("sceUserServiceGetLoginUserIdList returned 0x%x (%d)",
             (unsigned int)list_result, (int)list_result);

    if (list_result == 0) {
        for (int i = 0; i < 4; i++) {
            if (user_ids[i] >= 0) {
                log_info("Using logged-in user id %d for ScePad", user_ids[i]);
                return user_ids[i];
            }
        }
    }

    log_warn("No logged-in user id found; falling back to "
             "PAD_USER_ID_SYSTEM (0x%x)",
             PAD_USER_ID_SYSTEM);
    return PAD_USER_ID_SYSTEM;
}

bool pad_open(PadState *state) {
    if (!state->available) {
        log_warn("pad_open called before pad_init succeeded");
        return false;
    }

    int32_t user_id = resolve_user_id();
    int32_t handle = scePadOpen(user_id, PAD_PORT_TYPE_STANDARD, 0, NULL);
    log_info("scePadOpen returned 0x%x (%d) for user_id=%d",
             (unsigned int)handle, (int)handle, (int)user_id);

    if (handle < 0 && (uint32_t)handle == PAD_ERROR_ALREADY_OPENED) {
        /* Documented condition (see pad.h): something else already holds
         * this pad open. scePadGetHandle retrieves the existing handle
         * rather than creating a new one — same verified reference as
         * scePadOpen itself. */
        handle = scePadGetHandle(user_id, PAD_PORT_TYPE_STANDARD, 0);
        log_info("Pad already opened; scePadGetHandle returned 0x%x (%d)",
                 (unsigned int)handle, (int)handle);
    }

    if (handle < 0) {
        log_warn("Could not obtain a usable ScePad handle (nonfatal); "
                 "controller input unavailable this run");
        return false;
    }

    state->handle = handle;
    state->user_id = user_id;
    return true;
}

bool pad_read(PadState *state, PadData *out) {
    if (state->handle < 0) {
        return false;
    }
    return scePadReadState(state->handle, out) == 0;
}

void pad_close(PadState *state) {
    if (state->handle >= 0) {
        scePadClose(state->handle);
        state->handle = -1;
    }
}
