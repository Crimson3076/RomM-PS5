/* Minimal logging with mandatory secret redaction. Never call
 * fprintf/printf directly with request headers, tokens, or server
 * credentials elsewhere in the codebase — route them through
 * log_redact_bearer_token() (or a similar helper) first, so an accidental
 * `log_info("Authorization: %s", header)` cannot leak a full token.
 */
#ifndef ROMM_PS5_LOG_H
#define ROMM_PS5_LOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
} LogLevel;

void log_message(LogLevel level, const char *fmt, ...);

#define log_debug(...) log_message(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define log_info(...) log_message(LOG_LEVEL_INFO, __VA_ARGS__)
#define log_warn(...) log_message(LOG_LEVEL_WARN, __VA_ARGS__)
#define log_error(...) log_message(LOG_LEVEL_ERROR, __VA_ARGS__)

/* Redacts a RomM Client API Token (format "rmm_" + 64 hex chars, but this
 * treats anything shaped like "rmm_<opaque>" the same way rather than
 * hard-coding the length, in case the format changes) down to a form safe
 * to log: the "rmm_" prefix, the first 4 and last 4 characters of the
 * opaque part, and the true length, e.g. "rmm_ab12...ff90 (68 chars)".
 * A token shorter than 8 opaque characters is redacted to "rmm_***"
 * instead of leaking it piecemeal through the head/tail preview.
 *
 * If `token` does not start with "rmm_", or is NULL/empty, `out` receives
 * "(no token)".
 *
 * Returns `out` for chaining into a format call.
 */
char *log_redact_bearer_token(const char *token, char *out, size_t out_capacity);

/* Redacts a full "Authorization: Bearer rmm_..." (or similar) header value
 * for logging: keeps the scheme, redacts the credential via
 * log_redact_bearer_token() when it looks like an rmm_ token, and replaces
 * anything else after the scheme with "***" (never assume an unrecognized
 * auth scheme's credential is safe to print).
 */
char *log_redact_auth_header(const char *header_value, char *out,
                              size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_LOG_H */
