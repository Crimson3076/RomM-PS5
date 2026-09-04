/* RomM server URL + Client API Token, loaded from a JSON config file.
 *
 * Deliberately a separate module from config.h (non-credential UI state):
 * this is the one place in the codebase that ever holds a token in memory,
 * which keeps the "never log/commit a token" rules easy to audit in one
 * place. Nothing in this module ever writes a token back to disk in a new
 * location or logs it unredacted — see log_redact_bearer_token() in
 * src/log/log.h for the only safe way to reference a token in a log line.
 */
#ifndef ROMM_PS5_CREDENTIALS_H
#define ROMM_PS5_CREDENTIALS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROMM_CRED_SERVER_URL_MAX 256
#define ROMM_CRED_API_TOKEN_MAX 128

typedef struct {
    char server_url[ROMM_CRED_SERVER_URL_MAX]; /* no trailing slash */
    char api_token[ROMM_CRED_API_TOKEN_MAX];   /* raw token, e.g. "rmm_..." */
} RommCredentials;

typedef enum {
    CREDENTIALS_OK = 0,
    CREDENTIALS_ERR_IO,            /* file missing or unreadable */
    CREDENTIALS_ERR_PARSE,         /* file exists but isn't valid JSON */
    CREDENTIALS_ERR_MISSING_FIELD, /* valid JSON, but server_url/api_token absent or empty */
} CredentialsResult;

/* Loads and validates {"server_url": "...", "api_token": "..."} from
 * `path`. A trailing slash on server_url is stripped automatically (RomM
 * API paths are built by this project as server_url + "/api/...").
 * Returns CREDENTIALS_OK only when both fields are present and non-empty;
 * `out` is zeroed on any other result, so a caller can never accidentally
 * use a half-populated struct. Never logs the token value. */
CredentialsResult credentials_load(const char *path, RommCredentials *out);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_CREDENTIALS_H */
