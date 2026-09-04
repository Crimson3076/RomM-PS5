/* Real, HTTP-backed RommApi implementation. Talks to an actual RomM
 * server over the HttpClient abstraction (net/http_client.h) — works
 * with either the PS5-native backend (net/http_client_ps5.h) or a
 * host-side test double (tests/mock_http_client.h), which is how this
 * module's logic is unit-tested without real sockets. See
 * docs/architecture.md §3 for the endpoint/response-shape findings this
 * implementation is built against (source-verified against RomM's own
 * backend, not just external docs — still not verified against a live
 * server; see docs/testing.md).
 */
#ifndef ROMM_PS5_ROMM_API_HTTP_H
#define ROMM_PS5_ROMM_API_HTTP_H

#include "net/http_client.h"
#include "romm_api/romm_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROMM_SERVER_URL_MAX 256
#define ROMM_AUTH_HEADER_MAX 128

/* Caller-owned backing state for the HTTP-backed RommApi — allocate this
 * (stack or static, no heap needed) and keep it alive as long as the
 * RommApi it backs is in use. Do not access its fields directly outside
 * romm_api_http.c; it exists here only so callers can own the storage. */
typedef struct {
    const HttpClient *http;
    char server_url[ROMM_SERVER_URL_MAX]; /* no trailing slash */
    char auth_header[ROMM_AUTH_HEADER_MAX]; /* "Bearer rmm_..." */
    int32_t ps5_platform_id;
    bool platform_resolved;
} RommApiHttpContext;

/* Wires `out` to a real RomM server reachable through `http`.
 * `server_url` must have no trailing slash (e.g. "http://192.168.1.50:3000"
 * or "https://romm.example.com"). `api_token` is the raw Client API Token
 * value (e.g. "rmm_<hex>", exactly as configured — this function adds the
 * "Bearer " prefix). Both strings are copied (truncated if they would
 * overflow ROMM_SERVER_URL_MAX/ROMM_AUTH_HEADER_MAX). */
void romm_api_http_init(RommApi *out, RommApiHttpContext *storage,
                         const HttpClient *http, const char *server_url,
                         const char *api_token);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_ROMM_API_HTTP_H */
