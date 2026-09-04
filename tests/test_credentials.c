#include "test_framework.h"
#include "config/credentials.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char *write_temp_config(const char *contents) {
    static char path[128];
    snprintf(path, sizeof(path), "/tmp/rommps5_test_config_%d.json",
              getpid());
    FILE *f = fopen(path, "wb");
    fwrite(contents, 1, strlen(contents), f);
    fclose(f);
    return path;
}

void test_config_credentials(TestCounters *tc) {
    RommCredentials creds;

    /* Happy path, and the trailing slash on server_url is stripped. */
    char *path = write_temp_config(
        "{\"server_url\": \"http://192.168.1.50:3000/\", "
        "\"api_token\": \"rmm_abc123\"}");
    TEST_CHECK(tc, credentials_load(path, &creds) == CREDENTIALS_OK);
    TEST_CHECK(tc, strcmp(creds.server_url, "http://192.168.1.50:3000") == 0);
    TEST_CHECK(tc, strcmp(creds.api_token, "rmm_abc123") == 0);
    remove(path);

    /* Missing file. */
    memset(&creds, 0xAA, sizeof(creds));
    TEST_CHECK(tc, credentials_load("/tmp/rommps5_does_not_exist.json",
                                      &creds) == CREDENTIALS_ERR_IO);
    /* Zeroed on any non-OK result, never left half-populated. */
    TEST_CHECK(tc, creds.server_url[0] == '\0');
    TEST_CHECK(tc, creds.api_token[0] == '\0');

    /* Invalid JSON. */
    path = write_temp_config("{not valid json");
    TEST_CHECK(tc, credentials_load(path, &creds) == CREDENTIALS_ERR_PARSE);
    remove(path);

    /* Missing api_token. */
    path = write_temp_config("{\"server_url\": \"http://x\"}");
    TEST_CHECK(tc,
                credentials_load(path, &creds) == CREDENTIALS_ERR_MISSING_FIELD);
    remove(path);

    /* Empty api_token counts as missing. */
    path = write_temp_config(
        "{\"server_url\": \"http://x\", \"api_token\": \"\"}");
    TEST_CHECK(tc,
                credentials_load(path, &creds) == CREDENTIALS_ERR_MISSING_FIELD);
    remove(path);

    /* A JSON array instead of an object. */
    path = write_temp_config("[1, 2, 3]");
    TEST_CHECK(tc, credentials_load(path, &creds) == CREDENTIALS_ERR_PARSE);
    remove(path);
}
