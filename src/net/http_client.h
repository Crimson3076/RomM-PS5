/* HTTP transport interface. This is a placeholder for Milestone 1: the
 * shape of the abstraction the download manager and RomM API client will
 * eventually use, so UI/API/download code can be written and tested
 * against it now without depending on a concrete network stack.
 *
 * No implementation in this milestone performs a real network request —
 * per docs/architecture.md, whether libcurl (or any HTTP+TLS stack) even
 * builds against the ps5-payload-dev/sdk sysroot is UNVERIFIED and is the
 * top risk for the whole project. That must be resolved with a real
 * on-device spike before this interface gets a real backend. Only
 * `http_client_null_init()` (always returns HTTP_ERR_UNIMPLEMENTED) exists
 * today.
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
