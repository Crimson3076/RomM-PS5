#include "test_framework.h"
#include "config/config.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

void test_config(TestCounters *tc) {
    char path[64];
    snprintf(path, sizeof(path), "/tmp/rommps5_test_config_%d.conf", getpid());
    remove(path); /* in case a previous failed run left one behind */

    /* A missing file yields defaults, not an error. */
    AppConfig loaded;
    TEST_CHECK(tc, config_load(path, &loaded) == CONFIG_OK);
    TEST_CHECK(tc, loaded.selected_destination_index == -1);
    TEST_CHECK(tc, loaded.fullscreen == true);

    /* Save, then load back an exact match. */
    AppConfig to_save;
    config_set_defaults(&to_save);
    to_save.selected_destination_index = 3;
    to_save.fullscreen = false;
    TEST_CHECK(tc, config_save(path, &to_save) == CONFIG_OK);

    AppConfig round_tripped;
    TEST_CHECK(tc, config_load(path, &round_tripped) == CONFIG_OK);
    TEST_CHECK(tc, round_tripped.selected_destination_index == 3);
    TEST_CHECK(tc, round_tripped.fullscreen == false);

    /* The saved file must never contain a RomM server URL or token field —
     * credential persistence is explicitly out of scope for this module
     * (see config.h); this asserts the current file format has no such
     * key, so a future change can't silently add one here without a test
     * failure calling it out. */
    FILE *f = fopen(path, "r");
    TEST_CHECK(tc, f != NULL);
    if (f != NULL) {
        char buf[512];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        TEST_CHECK(tc, strstr(buf, "token") == NULL);
        TEST_CHECK(tc, strstr(buf, "rmm_") == NULL);
        TEST_CHECK(tc, strstr(buf, "url") == NULL);
        fclose(f);
    }

    remove(path);
}
