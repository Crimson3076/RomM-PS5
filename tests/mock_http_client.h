/* In-process HttpClient test double (see net/http_client.h's file
 * comment) — lets romm_api_http.c and downloader.c be exercised on the
 * host without real sockets.
 *
 * Two modes:
 *   - Single-response (default): every request gets the same canned
 *     status/body — enough for downloader.c tests, which only ever make
 *     one GET per downloader_run() call.
 *   - Routed: add_route() registers a canned response per URL substring
 *     (checked in registration order, first match wins) — needed for
 *     romm_api_http.c tests, where one call (e.g. list_ps5_games) issues
 *     two different GETs (/api/platforms, then /api/roms) through the same
 *     client and each needs its own fixture body.
 *
 * Records the last request so tests can assert on the URL/headers/Range
 * that were actually sent.
 */
#ifndef ROMM_PS5_TEST_MOCK_HTTP_CLIENT_H
#define ROMM_PS5_TEST_MOCK_HTTP_CLIENT_H

#include "net/http_client.h"

#include <stdbool.h>

#define MOCK_HTTP_MAX_ROUTES 4

typedef struct {
    const char *url_contains;
    int status_code;
    const uint8_t *body;
    size_t body_len;
} MockHttpRoute;

typedef struct {
    /* --- configured by the test before use --- */
    int status_code;         /* single-response mode default */
    const uint8_t *body;
    size_t body_len;
    int64_t content_length;  /* -1 = derive from the response body's length */
    bool force_result;       /* if true, use forced_result instead of
                               * deriving HTTP_OK/HTTP_ERR_HTTP_STATUS from
                               * status_code (for simulating connect/tls/
                               * timeout failures) */
    HttpResult forced_result;
    /* If non-zero, the sink is called this many bytes at a time instead of
     * delivering the whole body in one call — exercises chunk-boundary
     * logic (e.g. downloader.c's first-chunk resume detection) the same
     * way a real streaming transport would. */
    size_t chunk_size;

    MockHttpRoute routes[MOCK_HTTP_MAX_ROUTES];
    size_t route_count;

    /* --- filled in by the mock on the most recent get()/head() call --- */
    char last_url[512];
    char last_authorization[256];
    int64_t last_range_start;
    int64_t last_range_end;
    int request_count;
} MockHttpClient;

/* Resets `mock` to defaults (status 200, empty body, no routes, no forced
 * result) and wires `out` to call into it. */
void mock_http_client_init(MockHttpClient *mock, HttpClient *out);

/* Registers a routed response — see the file comment. */
void mock_http_client_add_route(MockHttpClient *mock,
                                 const char *url_contains, int status_code,
                                 const uint8_t *body, size_t body_len);

#endif /* ROMM_PS5_TEST_MOCK_HTTP_CLIENT_H */
