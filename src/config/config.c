#include "config/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_set_defaults(AppConfig *cfg) {
    cfg->selected_destination_index = -1;
    cfg->fullscreen = true;
}

ConfigResult config_load(const char *path, AppConfig *out) {
    config_set_defaults(out);

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return CONFIG_OK; /* no file yet is not an error */
    }

    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
        char *eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        char *key = line;
        char *value = eq + 1;

        char *newline = strchr(value, '\n');
        if (newline != NULL) {
            *newline = '\0';
        }

        if (strcmp(key, "selected_destination_index") == 0) {
            out->selected_destination_index = atoi(value);
        } else if (strcmp(key, "fullscreen") == 0) {
            out->fullscreen = strcmp(value, "1") == 0;
        }
        /* Unknown keys are ignored deliberately (forward compatibility). */
    }

    if (ferror(f)) {
        fclose(f);
        return CONFIG_ERR_IO;
    }
    fclose(f);
    return CONFIG_OK;
}

ConfigResult config_save(const char *path, const AppConfig *cfg) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return CONFIG_ERR_IO;
    }

    int written = fprintf(f, "selected_destination_index=%d\nfullscreen=%d\n",
                           cfg->selected_destination_index,
                           cfg->fullscreen ? 1 : 0);

    bool ok = written > 0 && ferror(f) == 0;
    fclose(f);
    return ok ? CONFIG_OK : CONFIG_ERR_IO;
}
