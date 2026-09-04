#include "log/log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *level_name(LogLevel level) {
    switch (level) {
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_WARN:
        return "WARN";
    case LOG_LEVEL_ERROR:
        return "ERROR";
    default:
        return "?";
    }
}

void log_message(LogLevel level, const char *fmt, ...) {
    time_t now = time(NULL);
    struct tm tm_utc;
#if defined(_WIN32)
    gmtime_s(&tm_utc, &now);
#else
    gmtime_r(&now, &tm_utc);
#endif
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

    fprintf(stderr, "[%s] %-5s ", timestamp, level_name(level));
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
}

static const char RMM_PREFIX[] = "rmm_";
#define RMM_PREFIX_LEN (sizeof(RMM_PREFIX) - 1)

char *log_redact_bearer_token(const char *token, char *out,
                               size_t out_capacity) {
    if (token == NULL || strncmp(token, RMM_PREFIX, RMM_PREFIX_LEN) != 0) {
        snprintf(out, out_capacity, "(no token)");
        return out;
    }

    const char *opaque = token + RMM_PREFIX_LEN;
    size_t opaque_len = strlen(opaque);

    if (opaque_len < 8) {
        snprintf(out, out_capacity, "%s***", RMM_PREFIX);
        return out;
    }

    char head[5] = {0};
    char tail[5] = {0};
    memcpy(head, opaque, 4);
    memcpy(tail, opaque + opaque_len - 4, 4);

    size_t total_len = RMM_PREFIX_LEN + opaque_len;
    snprintf(out, out_capacity, "%s%s...%s (%zu chars)", RMM_PREFIX, head,
             tail, total_len);
    return out;
}

char *log_redact_auth_header(const char *header_value, char *out,
                              size_t out_capacity) {
    static const char BEARER_PREFIX[] = "Bearer ";
    const size_t bearer_prefix_len = sizeof(BEARER_PREFIX) - 1;

    if (header_value == NULL) {
        snprintf(out, out_capacity, "(no auth header)");
        return out;
    }

    if (strncmp(header_value, BEARER_PREFIX, bearer_prefix_len) == 0) {
        const char *credential = header_value + bearer_prefix_len;
        if (strncmp(credential, RMM_PREFIX, RMM_PREFIX_LEN) == 0) {
            char redacted_token[64];
            log_redact_bearer_token(credential, redacted_token,
                                     sizeof(redacted_token));
            snprintf(out, out_capacity, "Bearer %s", redacted_token);
            return out;
        }
        snprintf(out, out_capacity, "Bearer ***");
        return out;
    }

    /* Unrecognized scheme (Basic, session cookie, etc.) — never print the
     * credential portion for anything we don't explicitly know how to
     * redact. */
    snprintf(out, out_capacity, "***");
    return out;
}
