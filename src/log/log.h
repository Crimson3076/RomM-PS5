/* Minimal logging with mandatory secret redaction. Never call
 * fprintf/printf directly with request headers, tokens, or server
 * credentials elsewhere in the codebase — route them through
 * log_redact_bearer_token() (or a similar helper) first, so an accidental
 * `log_info("Authorization: %s", header)` cannot leak a full token.
 */
#ifndef ROMM_PS5_LOG_H
#define ROMM_PS5_LOG_H

#include <stdbool.h>
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

/* Every call formats its line exactly once, then fans it out to both
 * stderr (always) and the optional persistent file sink (if one is open
 * via log_init_file_sink()) — the same bytes reach both, so the two never
 * drift out of sync with each other. This is the ONLY logging entry point
 * this project's own code should use; do not call printf/fprintf directly
 * for application diagnostics anywhere else (config.c's file writes are a
 * different concern — serializing config data, not logging — and are
 * exempt). */
void log_message(LogLevel level, const char *fmt, ...);

#define log_debug(...) log_message(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define log_info(...) log_message(LOG_LEVEL_INFO, __VA_ARGS__)
#define log_warn(...) log_message(LOG_LEVEL_WARN, __VA_ARGS__)
#define log_error(...) log_message(LOG_LEVEL_ERROR, __VA_ARGS__)

/* Opens (append mode) `path` as the persistent log sink; every
 * log_message() call after this succeeds also writes its line there,
 * flushed immediately (no buffering left unflushed on the PS5 target,
 * where the process could be killed or the console could lose power
 * between log lines). The caller is responsible for making sure `path`'s
 * parent directory exists first — this function does not create
 * directories, since where the log should live is platform-specific
 * (e.g. PS5's src/ps5/main_ps5.c chooses /data/romm-ps5/, host code
 * doesn't need to) and doesn't belong in this platform-agnostic module.
 *
 * On failure, logs a warning (to stderr / whatever sink is already open)
 * and returns false; this is always nonfatal — the caller should keep
 * running with stdout/stderr-only logging.
 *
 * Calling this again while a sink is already open closes the previous one
 * first.
 */
bool log_init_file_sink(const char *path);

/* Flushes and closes the persistent file sink opened by
 * log_init_file_sink(), if any. Safe to call even if no sink is open.
 * Callers should call this after their last log_message() call and
 * before returning, so shutdown/cleanup messages are captured and
 * durably flushed. */
void log_close_file_sink(void);

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
char *log_redact_bearer_token(const char *token, char *out,
                              size_t out_capacity);

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
