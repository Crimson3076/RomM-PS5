/* PS5-native HttpClient backend, using the console's own SceNet/SceSsl/
 * SceHttp2 system libraries. See http_client_ps5.c for exactly which
 * calls are verified against a working ps5-payload-dev/sdk sample versus
 * inferred from the broader SceHttp API convention. */
#ifndef ROMM_PS5_HTTP_CLIENT_PS5_H
#define ROMM_PS5_HTTP_CLIENT_PS5_H

#include "net/http_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One-time global setup: sceNetInit + sceSslInit + sceHttp2Init + a shared
 * request template. Must succeed before http_client_ps5_init() clients are
 * used. Safe to call once at application startup; calling it again while
 * already started is a no-op that returns true. */
bool http_client_ps5_startup(void);

/* Tears down everything http_client_ps5_startup() set up. Safe to call
 * even if startup was never called or already failed. */
void http_client_ps5_shutdown(void);

/* Fills `out` with a client backed by the global state from
 * http_client_ps5_startup(). Every call fails with HTTP_ERR_CONNECT if
 * startup hasn't succeeded. */
void http_client_ps5_init(HttpClient *out);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_HTTP_CLIENT_PS5_H */
