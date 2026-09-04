/* RomM-PS5 — minimal PS5-native entry point (PS5 cross-compilation
 * milestone). Not the SDL UI (src/main.c) — that's host-only, see
 * docs/building.md "Keep host-only SDL ... separated from PS5-specific
 * code". This file exists to prove the pinned ps5-payload-dev/sdk
 * toolchain can actually build and run something meaningful on real
 * hardware, not to implement the real application UI.
 *
 * What this does, and why each call was chosen:
 *   1. Initializes UserService (required before most other SCE calls that
 *      touch a logged-in user — pattern copied from
 *      ps5-payload-dev/sdk samples/browser/main.c). CONFIRMED on real
 *      hardware (see docs/testing.md "First hardware test").
 *   2. Opens the persistent log file and logs via this project's own log
 *      module (src/log/log.c), which now fans every log_message() call
 *      out to both stderr (the elfldr TCP connection) and the file — see
 *      log.h. This replaced a hand-rolled, module-specific
 *      duplicate-logging pattern that silently dropped several lines from
 *      the file on the first hardware test; see docs/testing.md.
 *   3. Logs the console's hardware model name via sceKernelGetHwModelName
 *      (prototype copied from samples/hwinfo/main.c). CONFIRMED on real
 *      hardware.
 *   4. Runs this project's real storage_discover() against the actual
 *      fixed candidate destination list (src/storage/storage.c).
 *      CONFIRMED on real hardware.
 *   5. Sends an on-screen toast notification via sceNotificationSend
 *      (prototype + JSON payload shape copied from
 *      samples/notify/main.c). CONFIRMED on real hardware.
 *   6. Attempts to obtain a real user id, then open (but not read from) a
 *      ScePad controller handle. See the SceUserService/ScePad section
 *      below — this is a documented blocker, not a working feature yet.
 *   7. Terminates UserService (only if it was actually initialized) and
 *      exits cleanly, flushing and closing the persistent log.
 *
 * --- SceUserService / ScePad: investigation result and blocker ---
 *
 * The first hardware test (see docs/testing.md) showed
 * `sceUserServiceGetInitialUser` failing. Investigating the pinned SDK
 * (ps5-payload-dev/sdk v0.43) directly — every sample's source, every
 * stub file under sce_stubs, and the full include/ tree — found:
 *   - `sceUserServiceInitialize(0)` / `sceUserServiceTerminate()` are the
 *     ONLY UserService calls demonstrated anywhere in this SDK
 *     (samples/browser/main.c), and this pair is confirmed working on
 *     real hardware.
 *   - `sceUserServiceGetInitialUser` is a real, exported symbol in
 *     sce_stubs/libSceUserService.so (confirmed by inspecting the stub
 *     library directly — one of ~300 exported SceUserService symbols),
 *     but its signature, its argument meaning, and what its return value
 *     means are NOT documented anywhere in this SDK: no sample calls it,
 *     no header declares it, no comment describes it. The
 *     `int32_t *userId` out-param signature used in the previous version
 *     of this file was sourced from general PS4/PS5 homebrew convention,
 *     not from this SDK — i.e. it was a guess, and the hardware test's
 *     failure is real evidence that guess should not be trusted further.
 *   - No SCE_USER_SERVICE_ERROR_* (or similar) constants exist anywhere
 *     in this SDK, so even the numeric failure code from that call cannot
 *     be authoritatively interpreted (e.g. distinguished from a
 *     documented "already initialized"-style condition) — there is
 *     nothing documented to compare it against.
 *   - No sample in this SDK obtains a real user id at all, by any
 *     function — install_app and browser (the other two samples linking
 *     -lSceUserService) don't either.
 *
 * Per this project's policy against guessing undocumented ABI: this file
 * no longer calls `sceUserServiceGetInitialUser`. There is currently no
 * SDK-verified way, anywhere in the pinned SDK, to obtain a real user id
 * from a raw elfldr-launched payload. That blocks `scePadOpen` (which
 * needs one), so ScePad open is skipped and logged as a documented
 * blocker rather than an attempted-and-failed call. `scePadInit()` is
 * still called — it takes no arguments, so there is no struct/constant
 * layout to guess, unlike scePadOpen/scePadReadState.
 *
 * Resolving this blocker for real needs one of: an authoritative SCE/
 * homebrew reference for SceUserService's real ABI (not present in this
 * SDK), or evidence from someone who has gotten a real user id in this
 * SDK's ecosystem. Neither is available to this project right now.
 */
#include "log/log.h"
#include "storage/storage.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>

/* --- Prototypes copied verbatim (signatures) from
 * ps5-payload-dev/sdk v0.43 sample sources; see the file-level comment
 * above for exactly which sample each came from. --- */

/* samples/browser/main.c — the only SDK-demonstrated UserService calls. */
int sceUserServiceInitialize(void *);
int sceUserServiceTerminate(void);

/* samples/notify/main.c */
#define SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM 0xFE
int sceNotificationSend(int userId, bool isLogged, const char *payload);

/* samples/hwinfo/main.c */
int sceKernelGetHwModelName(char *);

/* ScePad: real, confirmed-exported symbols (sce_stubs/libScePad.so), but
 * NOT demonstrated by any sample in this SDK. scePadInit takes no
 * arguments, so there is nothing about its call shape to get wrong.
 * scePadOpen/scePadClose follow the widely-published PS4/PS5 homebrew
 * convention (not SDK-verified) and are only called once a real user id
 * is available — which, per the investigation above, doesn't currently
 * happen. scePadReadState is deliberately never declared or called here:
 * its output struct's field layout is published nowhere in this SDK. */
int32_t scePadInit(void);
int32_t scePadOpen(int32_t userId, int32_t type, int32_t index,
                   const void *pParam);
int32_t scePadClose(int32_t handle);

#define LOG_DIR "/data/romm-ps5"
#define LOG_PATH LOG_DIR "/ps5-hello.log"

int main(void) {
    int32_t user_service_result = sceUserServiceInitialize(0);
    bool user_service_ready = (user_service_result == 0);

    /* Directory creation is PS5-specific (the path itself is a PS5 detail)
     * and stays here rather than in the platform-agnostic log module —
     * see log.h. Ignored on failure/EEXIST; log_init_file_sink()'s own
     * fopen() below is the real, observable check. */
    mkdir(LOG_DIR, 0755);
    log_init_file_sink(LOG_PATH);

    log_info("RomM-PS5 PS5-target starting (cross-compilation milestone)");

    log_info("sceUserServiceInitialize returned 0x%x (%d)",
             (unsigned int)user_service_result, (int)user_service_result);
    if (!user_service_ready) {
        log_warn("sceUserServiceInitialize failed (nonfatal); treating "
                 "UserService as unavailable for the rest of this run");
    }

    char model[256] = {0};
    if (sceKernelGetHwModelName(model) == 0 && model[0] != '\0') {
        log_info("Hardware model: %s", model);
    } else {
        log_warn("sceKernelGetHwModelName failed or returned an empty name");
    }

    StorageDestination destinations[STORAGE_CANDIDATE_PATH_COUNT];
    size_t found =
        storage_discover(STORAGE_CANDIDATE_PATHS, STORAGE_CANDIDATE_PATH_COUNT,
                         NULL, destinations, STORAGE_CANDIDATE_PATH_COUNT);
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

    int notify_result =
        sceNotificationSend(SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM, true, toast);
    log_info("sceNotificationSend returned 0x%x (%d)",
             (unsigned int)notify_result, notify_result);
    if (notify_result != 0) {
        log_warn("sceNotificationSend failed (nonfatal)");
    } else {
        log_info("Notification toast sent");
    }

    /* See the file-level comment: no SDK-verified way to obtain a real
     * user id exists in this pinned SDK, so scePadOpen (which needs one)
     * is never reached. scePadInit takes no arguments and is safe to call
     * regardless. */
    int32_t pad_init_result = scePadInit();
    log_info("scePadInit returned 0x%x (%d)", (unsigned int)pad_init_result,
             (int)pad_init_result);

    bool have_verified_user_id = false; /* see file-level comment */
    int32_t user_id = 0;
    int32_t pad_handle = -1;
    if (have_verified_user_id) {
        pad_handle = scePadOpen(user_id, 0, 0, NULL);
        log_info("scePadOpen returned 0x%x (%d) for user_id=%d",
                 (unsigned int)pad_handle, (int)pad_handle, (int)user_id);
        if (pad_handle >= 0) {
            log_info("ScePad handle %d opened (state read intentionally "
                     "not attempted — see docs/testing.md)",
                     (int)pad_handle);
        } else {
            log_warn("scePadOpen failed (nonfatal)");
        }
    } else {
        log_warn("scePadOpen skipped: no SDK-verified way to obtain a real "
                 "user id was found in this pinned SDK (documented "
                 "blocker, not a guessed workaround — see the file-level "
                 "comment in src/ps5/main_ps5.c and docs/testing.md)");
    }

    /* Clean up whatever was actually initialized/opened, in reverse order. */
    if (pad_handle >= 0) {
        scePadClose(pad_handle);
        log_info("scePadClose called for handle %d", (int)pad_handle);
    }

    log_info("RomM-PS5 PS5-target exiting cleanly");

    if (user_service_ready) {
        sceUserServiceTerminate();
    }

    log_close_file_sink();

    return EXIT_SUCCESS;
}
