/* HTTP transport interface. UI/API/download code is written against this
 * abstraction, never against a concrete network stack directly.
 *
 * Two real implementations exist:
 *   - http_client_ps5_init() (src/net/http_client_ps5.c): uses the PS5's
 *     own SceNet/SceSsl/SceHttp2 system libraries. COMPILED for PS5, NOT
 *     yet run on hardware — see docs/testing.md.
 *   - http_client_null_init(): every call returns HTTP_ERR_UNIMPLEMENTED.
 *     Used where a client is needed structurally but no request should
 *     ever actually happen (e.g. as a safe default before configuration
 *     is loaded).
 * Host-side tests use a test-only in-process mock (tests/mock_http_client.c)
 * instead of a real network stack, so RomM API client logic is verified
 * without needing real sockets.
 */
#ifndef ROMM_PS5_HTTP_CLIENT_H
#define ROMM_PS5_HTTP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HTTP_OK = 0,
    HTTP_ERR_UNIMPLEMENTED,
    HTTP_ERR_CONNECT,
    HTTP_ERR_TLS,
    HTTP_ERR_TIMEOUT,
    HTTP_ERR_HTTP_STATUS, /* request completed, but status was an error */
    HTTP_ERR_CANCELLED,   /* the body sink returned false */
} HttpResult;

typedef struct {
    int status_code;
    int64_t content_length; /* -1 = not present in the response */
    bool accept_ranges;
    bool is_partial_content; /* true for a 206 response */
} HttpResponseInfo;

/* Called for each chunk of the response body as it arrives, so callers can
 * stream straight to a destination file instead of buffering the whole
 * response. Return false to abort the transfer (e.g. user cancelled). */
typedef bool (*HttpBodySink)(const uint8_t *chunk, size_t chunk_len,
                              void *user_data);

typedef struct {
    const char *url;
    const char *authorization_header; /* full "Bearer rmm_..." value, or NULL */
    int64_t range_start;              /* -1 = no Range header */
    int64_t range_end;                /* -1 = open-ended range */
} HttpRequest;

typedef struct HttpClient {
    void *ctx;

    /* Contract every implementation must follow: `*response_out` is
     * populated (status_code, content_length, is_partial_content,
     * accept_ranges) BEFORE the first `sink` call, not just before `get`
     * returns. This lets a caller's sink inspect e.g. whether a Range
     * request actually got a 206 (see src/download/downloader.c, which
     * needs to know that before deciding whether to append to or
     * truncate its output file) without having to buffer anything or
     * wait for the whole transfer to finish. */
    HttpResult (*get)(void *ctx, const HttpRequest *request,
                       HttpResponseInfo *response_out, HttpBodySink sink,
                       void *sink_user_data);

    HttpResult (*head)(void *ctx, const HttpRequest *request,
                        HttpResponseInfo *response_out);
} HttpClient;

/* Fills `out` with a client whose every call returns
 * HTTP_ERR_UNIMPLEMENTED. Exists so callers and tests can be written
 * against the HttpClient interface today. */
void http_client_null_init(HttpClient *out);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_HTTP_CLIENT_H */
