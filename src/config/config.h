/* Persistent, non-credential application settings.
 *
 * Deliberately does NOT store the RomM server URL or Client API Token yet —
 * this milestone excludes credential persistence entirely (see the task's
 * Milestone 1 scope) while the safest available storage mechanism on the
 * PS5 homebrew environment is still being evaluated. Once that's decided,
 * it belongs in its own clearly-labeled module, not bolted onto this one,
 * so the "never commit / never log a token" rules stay easy to audit.
 */
#ifndef ROMM_PS5_CONFIG_H
#define ROMM_PS5_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int selected_destination_index; /* -1 = none chosen yet */
    bool fullscreen;
} AppConfig;

typedef enum {
    CONFIG_OK = 0,
    CONFIG_ERR_IO,
    CONFIG_ERR_PARSE,
} ConfigResult;

void config_set_defaults(AppConfig *cfg);

/* Simple "key=value" text format, one setting per line — easy to review by
 * hand and to keep free of anything sensitive. Unknown keys are ignored
 * (forward compatible); a missing file is not an error and yields defaults.
 */
ConfigResult config_load(const char *path, AppConfig *out);
ConfigResult config_save(const char *path, const AppConfig *cfg);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_CONFIG_H */
