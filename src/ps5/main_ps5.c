/* RomM-PS5 — minimal PS5-native entry point (PS5 cross-compilation
 * milestone). Not the SDL UI (src/main.c) — that's host-only, see
 * docs/building.md "Keep host-only SDL ... separated from PS5-specific
 * code". This file exists to prove the pinned ps5-payload-dev/sdk
 * toolchain can actually build and (pending real hardware) run something
 * meaningful, not to implement the real application UI.
 *
 * What this does, and why each call was chosen:
 *   1. Initializes UserService (required before most other SCE calls that
 *      touch a logged-in user — pattern copied from
 *      ps5-payload-dev/sdk samples/browser/main.c).
 *   2. Logs via this project's own log module (src/log/log.c), proving
 *      that module compiles and links unchanged against this toolchain.
 *   3. Logs the console's hardware model name via sceKernelGetHwModelName
 *      (prototype copied from samples/hwinfo/main.c).
 *   4. Runs this project's real storage_discover() against the actual
 *      fixed candidate destination list (src/storage/storage.c) — the
 *      first real, on-hardware exercise of that module once this runs on
 *      a console.
 *   5. Sends an on-screen toast notification via sceNotificationSend
 *      (prototype + JSON payload shape copied from
 *      samples/notify/main.c) — this project's chosen "displays a basic
 *      screen" proof; see docs/architecture.md for why a full custom
 *      graphics screen was not attempted this milestone.
 *   6. Opens (but does not read from) a controller handle via ScePad.
 *      scePadInit/scePadOpen/scePadClose are real, confirmed-exported
 *      symbols in this SDK's sce_stubs/libScePad.so (see
 *      docs/testing.md), but their exact call signatures and
 *      ScePadData's field layout are NOT published by this SDK. This
 *      file deliberately stops at opening a handle — it does not call
 *      scePadReadState with a hand-typed struct, since a wrong struct
 *      size passed to it is a real crash risk this project has no way to
 *      check without real hardware. See docs/testing.md.
 *   7. Terminates UserService and exits cleanly.
 */
#include "log/log.h"
#include "storage/storage.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* --- Prototypes copied verbatim (signatures) from
 * ps5-payload-dev/sdk v0.43 sample sources; see the file-level comment
 * above for exactly which sample each came from. --- */

/* samples/browser/main.c */
int sceUserServiceInitialize(void *);
int sceUserServiceTerminate(void);

/* Widely published PS4/PS5 homebrew convention (out-param form), not
 * shipped as a header by this SDK. Scalar in/out args only — the safer
 * category of unverified call compared to a struct-shaped one. */
int sceUserServiceGetInitialUser(int32_t *userId);

/* samples/notify/main.c */
#define SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM 0xFE
int sceNotificationSend(int userId, bool isLogged, const char *payload);

/* samples/hwinfo/main.c */
int sceKernelGetHwModelName(char *);

/* ScePad: only init/open/close, deliberately no state read — see the
 * file-level comment. Signatures follow the standard PS4/PS5 homebrew
 * convention (also not shipped as a header by this SDK). */
int32_t scePadInit(void);
int32_t scePadOpen(int32_t userId, int32_t type, int32_t index,
                    const void *pParam);
int32_t scePadClose(int32_t handle);

#define LOG_DIR "/data/romm-ps5"
#define LOG_PATH LOG_DIR "/ps5-hello.log"

static FILE *g_logfile = NULL;

/* Duplicates a message to both this project's stderr logger (relayed live
 * over the elfldr TCP connection when deployed — see docs/building.md)
 * and a durable log file on disk (this build's crash-log location — see
 * the deliverable report for this milestone). */
static void logfile_line(const char *msg) {
    if (g_logfile != NULL) {
        fprintf(g_logfile, "%s\n", msg);
        fflush(g_logfile);
    }
}

int main(void) {
    if (sceUserServiceInitialize(0)) {
        log_error("sceUserServiceInitialize failed");
    }

    mkdir(LOG_DIR, 0755); /* ignore EEXIST/any error; fopen below is the real check */
    g_logfile = fopen(LOG_PATH, "a");
    if (g_logfile == NULL) {
        log_warn("Could not open %s for logging (errno-based reason not "
                 "printed here to avoid depending on strerror behavior "
                 "that hasn't been verified on this libc yet)",
                 LOG_PATH);
    }

    log_info("RomM-PS5 PS5-target starting (cross-compilation milestone)");
    logfile_line("RomM-PS5 PS5-target starting (cross-compilation milestone)");

    char model[256] = {0};
    if (sceKernelGetHwModelName(model) == 0 && model[0] != '\0') {
        log_info("Hardware model: %s", model);
        char line[300];
        snprintf(line, sizeof(line), "Hardware model: %s", model);
        logfile_line(line);
    } else {
        log_warn("sceKernelGetHwModelName failed or returned an empty name");
    }

    StorageDestination destinations[STORAGE_CANDIDATE_PATH_COUNT];
    size_t found = storage_discover(STORAGE_CANDIDATE_PATHS,
                                     STORAGE_CANDIDATE_PATH_COUNT, NULL,
                                     destinations,
                                     STORAGE_CANDIDATE_PATH_COUNT);
    log_info("storage_discover: %zu of %zu candidate destinations exist "
             "and are writable",
             found, STORAGE_CANDIDATE_PATH_COUNT);
    for (size_t i = 0; i < found; i++) {
        log_info("  destination: %s (%llu bytes free)", destinations[i].path,
                  (unsigned long long)destinations[i].free_bytes);
    }

    static const char toast[] =
        "{\n"
        "  \"rawData\": {\n"
        "    \"viewTemplateType\": \"InteractiveToastTemplateB\",\n"
        "    \"channelType\": \"Downloads\",\n"
        "    \"useCaseId\": \"IDC\",\n"
        "    \"toastOverwriteType\": \"No\",\n"
        "    \"isImmediate\": true,\n"
        "    \"priority\": 100,\n"
        "    \"viewData\": {\n"
        "      \"icon\": {\n"
        "        \"type\": \"Predefined\",\n"
        "        \"parameters\": { \"icon\": \"download\" }\n"
        "      },\n"
        "      \"message\": { \"body\": \"RomM-PS5\" },\n"
        "      \"subMessage\": { \"body\": \"PS5 cross-compile milestone "
        "ELF ran successfully\" }\n"
        "    }\n"
        "  },\n"
        "  \"createdDateTime\": \"2026-01-01T00:00:00.000Z\",\n"
        "  \"localNotificationId\": \"1\"\n"
        "}";

    if (sceNotificationSend(SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM, true,
                             toast)) {
        log_warn("sceNotificationSend failed");
        logfile_line("sceNotificationSend failed");
    } else {
        log_info("Notification toast sent");
        logfile_line("Notification toast sent");
    }

    int32_t user_id = 0;
    if (sceUserServiceGetInitialUser(&user_id)) {
        log_warn("sceUserServiceGetInitialUser failed; skipping ScePad");
    } else {
        int32_t pad_init_result = scePadInit();
        log_info("scePadInit returned %d", pad_init_result);

        int32_t pad_handle = scePadOpen(user_id, 0, 0, NULL);
        if (pad_handle < 0) {
            log_warn("scePadOpen failed (returned %d) for user %d",
                     pad_handle, user_id);
        } else {
            log_info("scePadOpen succeeded, handle=%d for user %d "
                     "(state read intentionally not attempted — see "
                     "docs/testing.md)",
                     pad_handle, user_id);
            scePadClose(pad_handle);
        }
    }

    log_info("RomM-PS5 PS5-target exiting cleanly");
    logfile_line("RomM-PS5 PS5-target exiting cleanly");

    if (g_logfile != NULL) {
        fclose(g_logfile);
    }

    sceUserServiceTerminate();

    return EXIT_SUCCESS;
}
