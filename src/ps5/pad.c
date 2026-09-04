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
int sceUserServiceGetUserName(int32_t user_id, char *name, size_t size);

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

static int32_t try_user_id(int32_t user_id) {
    int32_t handle = scePadOpen(user_id, PAD_PORT_TYPE_STANDARD, 0, NULL);
    log_info("scePadOpen returned 0x%08x (%d) for user_id=0x%08x (%d)",
             (unsigned int)handle, (int)handle, (unsigned int)user_id,
             (int)user_id);

    if (handle >= 0) {
        return handle;
    }

    if ((uint32_t)handle == PAD_ERROR_USER_NOT_LOGIN) {
        log_warn("ScePad rejected user_id=0x%08x as USER_NOT_LOGIN",
                 (unsigned int)user_id);
    }

    /* An LNC-managed process may already own the controller. The exact
     * failure code is not consistent across launch contexts, so check
     * for an existing handle after every open failure rather than only
     * PAD_ERROR_ALREADY_OPENED. This function and signature come from
     * the same PS5 SDL reference as scePadOpen. */
    int32_t existing =
        scePadGetHandle(user_id, PAD_PORT_TYPE_STANDARD, 0);
    log_info("scePadGetHandle returned 0x%08x (%d) for user_id=0x%08x (%d)%s",
             (unsigned int)existing, (int)existing, (unsigned int)user_id,
             (int)user_id,
             (uint32_t)handle == PAD_ERROR_ALREADY_OPENED
                 ? " after PAD_ERROR_ALREADY_OPENED"
                 : " after scePadOpen failure");
    return existing;
}

bool pad_open(PadState *state) {
    if (!state->available) {
        log_warn("pad_open called before pad_init succeeded");
        return false;
    }

    int32_t user_ids[4] = {-1, -1, -1, -1};
    int32_t list_result = sceUserServiceGetLoginUserIdList(user_ids);
    log_info("sceUserServiceGetLoginUserIdList returned 0x%08x (%d)",
             (unsigned int)list_result, (int)list_result);

    if (list_result == 0) {
        for (int i = 0; i < 4; i++) {
            log_info("SceUserService login slot %d: user_id=0x%08x (%d)", i,
                     (unsigned int)user_ids[i], (int)user_ids[i]);
            if (user_ids[i] >= 0) {
                char user_name[256] = {0};
                int32_t name_result = sceUserServiceGetUserName(
                    user_ids[i], user_name, sizeof(user_name));
                /* Log only whether UserService accepts the id. A local
                 * account name is unnecessary personally identifying
                 * information and must not be written to the log. */
                log_info("sceUserServiceGetUserName validation returned "
                         "0x%08x (%d) for login slot %d",
                         (unsigned int)name_result, (int)name_result, i);
            }
        }

        for (int i = 0; i < 4; i++) {
            if (user_ids[i] < 0) {
                continue;
            }
            int32_t handle = try_user_id(user_ids[i]);
            if (handle >= 0) {
                state->handle = handle;
                state->user_id = user_ids[i];
                log_info("ScePad ready: handle=%d, user_id=0x%08x (%d)",
                         (int)handle, (unsigned int)user_ids[i],
                         (int)user_ids[i]);
                return true;
            }
        }
    }

    bool system_was_reported = false;
    for (int i = 0; i < 4; i++) {
        if (user_ids[i] == PAD_USER_ID_SYSTEM) {
            system_was_reported = true;
        }
    }
    if (!system_was_reported) {
        log_warn("No reported login user produced a pad handle; trying "
                 "PAD_USER_ID_SYSTEM (0x%08x) once",
                 PAD_USER_ID_SYSTEM);
        int32_t handle = try_user_id(PAD_USER_ID_SYSTEM);
        if (handle >= 0) {
            state->handle = handle;
            state->user_id = PAD_USER_ID_SYSTEM;
            log_info("ScePad ready through PAD_USER_ID_SYSTEM: handle=%d",
                     (int)handle);
            return true;
        }
    }

    log_warn("Could not obtain a usable ScePad handle (nonfatal); "
             "controller input unavailable this run");
    return false;
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
