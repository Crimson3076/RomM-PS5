#include "config/credentials.h"
#include "log/log.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void strip_trailing_slash(char *s) {
    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == '/') {
        s[--len] = '\0';
    }
}

CredentialsResult credentials_load(const char *path, RommCredentials *out) {
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        log_error("Could not open config file %s", path);
        return CREDENTIALS_ERR_IO;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        log_error("Could not seek config file %s", path);
        return CREDENTIALS_ERR_IO;
    }
    long size = ftell(f);
    if (size < 0 || size > 1024 * 1024) {
        fclose(f);
        log_error("Config file %s has an unreasonable size", path);
        return CREDENTIALS_ERR_IO;
    }
    rewind(f);

    char *buf = malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(f);
        log_error("Failed to allocate %ld bytes for config file %s", size,
                  path);
        return CREDENTIALS_ERR_IO;
    }

    size_t read_bytes = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read_bytes] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);

    if (root == NULL || !cJSON_IsObject(root)) {
        log_error("Config file %s is not a valid JSON object", path);
        cJSON_Delete(root);
        return CREDENTIALS_ERR_PARSE;
    }

    cJSON *server_url = cJSON_GetObjectItemCaseSensitive(root, "server_url");
    cJSON *api_token = cJSON_GetObjectItemCaseSensitive(root, "api_token");

    if (!cJSON_IsString(server_url) || server_url->valuestring == NULL ||
        server_url->valuestring[0] == '\0') {
        log_error("Config file %s is missing a non-empty \"server_url\"",
                  path);
        cJSON_Delete(root);
        return CREDENTIALS_ERR_MISSING_FIELD;
    }

    if (!cJSON_IsString(api_token) || api_token->valuestring == NULL ||
        api_token->valuestring[0] == '\0') {
        /* Do not include the token field's presence/absence in a way that
         * could be confused with its value — just report the field name. */
        log_error("Config file %s is missing a non-empty \"api_token\"",
                  path);
        cJSON_Delete(root);
        return CREDENTIALS_ERR_MISSING_FIELD;
    }

    snprintf(out->server_url, sizeof(out->server_url), "%s",
             server_url->valuestring);
    strip_trailing_slash(out->server_url);
    snprintf(out->api_token, sizeof(out->api_token), "%s",
             api_token->valuestring);

    cJSON_Delete(root);

    char redacted[64];
    log_redact_bearer_token(out->api_token, redacted, sizeof(redacted));
    log_info("Loaded config from %s (server_url set, token %s)", path,
             redacted);

    return CREDENTIALS_OK;
}
