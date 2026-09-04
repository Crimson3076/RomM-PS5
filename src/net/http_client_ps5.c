/* PS5-native HTTP(S) client, backing the RomM API client and the
 * download manager on the PS5 target. Uses the console's own system
 * networking libraries directly (SceNet + SceSsl + SceHttp2) rather than
 * porting curl/OpenSSL — see docs/architecture.md §1 (the original
 * project-blocking risk was "does *any* HTTP+TLS stack build against this
 * SDK", and the answer turned out to be "yes, Sony's own, and it's the
 * more idiomatic choice for a real PS5 app anyway").
 *
 * What's VERIFIED vs INFERRED:
 *
 * The full lifecycle sequence below (sceNetInit, sceNetPoolCreate,
 * sceSslInit, sceHttp2Init, sceHttp2CreateTemplate,
 * sceHttp2CreateRequestWithURL, sceHttp2SendRequest,
 * sceHttp2GetStatusCode, sceHttp2ReadData, and the matching
 * Term/Delete/Destroy calls) is copied directly from
 * ps5-payload-dev/sdk v0.43's own samples/http2_get/main.c, which builds
 * and links against the pinned SDK. That sample was not itself run on
 * hardware by this project, but its call sequence is the SDK maintainer's
 * own working reference, not a guess.
 *
 * `sceHttp2AddRequestHeader` and `sceHttp2GetResponseContentLength` are
 * real, confirmed-exported symbols in this SDK's sce_stubs/libSceHttp2.so,
 * but neither is called by any sample here. Their signatures below are
 * inferred from the surrounding, partially-verified sceHttp2 API's own
 * conventions (e.g. sceHttp2GetStatusCode's verified `(handle, int*)`
 * shape directly informs GetResponseContentLength's `(handle, uint64_t*)`
 * guess; sceHttp2CreateRequestWithURL's verified trailing-numeric-flag
 * shape informs AddRequestHeader's guessed trailing `mode` argument) and
 * from the classic (non-2) SceHttp API's long-public convention across
 * the PS4/PS5 homebrew ecosystem. This is this project's single highest-
 * risk unverified assumption in the whole networking path — isolated
 * here, in exactly these two functions, rather than spread around. If
 * hardware testing shows authentication or content-length reporting
 * behaving strangely, look here first. See docs/testing.md.
 *
 * Everything in this file is compiled for the PS5 target only; it has
 * NOT been run on real hardware as of this commit.
 */
#include "net/http_client_ps5.h"
#include "log/log.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --- Verified: ps5-payload-dev/sdk v0.43 samples/http2_get/main.c --- */
int sceNetInit(void);
int sceNetPoolCreate(const char *, int, int);
int sceNetPoolDestroy(int);

int sceSslInit(size_t);
int sceSslTerm(int);

int sceHttp2Init(int, int, size_t, int);
int sceHttp2Term(int);

int sceHttp2CreateTemplate(int, const char *, int, int);
int sceHttp2DeleteTemplate(int);

int sceHttp2CreateRequestWithURL(int, const char *, const char *, uint64_t);
int sceHttp2DeleteRequest(int);

int sceHttp2SendRequest(int, const void *, size_t);
int sceHttp2GetStatusCode(int, int *);
int sceHttp2ReadData(int, void *, size_t);

/* --- INFERRED, not sample-verified — see file-level comment --- */
int sceHttp2AddRequestHeader(int, const char *, const char *, int);
int sceHttp2GetResponseContentLength(int, uint64_t *);

#define HTTP2_HEADER_MODE_ADD 1

typedef struct {
    int net_pool;
    int ssl_ctx;
    int http_ctx;
    int template_id;
    bool ready;
} Ps5HttpState;

static Ps5HttpState g_state = {-1, -1, -1, -1, false};

bool http_client_ps5_startup(void) {
    if (g_state.ready) {
        return true;
    }

    if (sceNetInit()) {
        log_error("sceNetInit failed");
        return false;
    }

    g_state.net_pool = sceNetPoolCreate("rommps5", 32 * 1024, 0);
    if (g_state.net_pool < 0) {
        log_error("sceNetPoolCreate failed: %d", g_state.net_pool);
        return false;
    }

    g_state.ssl_ctx = sceSslInit(256 * 1024);
    if (g_state.ssl_ctx < 0) {
        log_error("sceSslInit failed: %d", g_state.ssl_ctx);
        return false;
    }

    g_state.http_ctx =
        sceHttp2Init(g_state.net_pool, g_state.ssl_ctx, 256 * 1024, 1);
    if (g_state.http_ctx < 0) {
        log_error("sceHttp2Init failed: %d", g_state.http_ctx);
        return false;
    }

    g_state.template_id =
        sceHttp2CreateTemplate(g_state.http_ctx, "RomM-PS5/0.1", 3, 1);
    if (g_state.template_id < 0) {
        log_error("sceHttp2CreateTemplate failed: %d", g_state.template_id);
        return false;
    }

    g_state.ready = true;
    log_info("PS5 HTTP client started (SceNet/SceSsl/SceHttp2)");
    return true;
}

void http_client_ps5_shutdown(void) {
    if (g_state.template_id >= 0) {
        sceHttp2DeleteTemplate(g_state.template_id);
        g_state.template_id = -1;
    }
    if (g_state.http_ctx >= 0) {
        sceHttp2Term(g_state.http_ctx);
        g_state.http_ctx = -1;
    }
    if (g_state.ssl_ctx >= 0) {
        sceSslTerm(g_state.ssl_ctx);
        g_state.ssl_ctx = -1;
    }
    if (g_state.net_pool >= 0) {
        sceNetPoolDestroy(g_state.net_pool);
        g_state.net_pool = -1;
    }
    g_state.ready = false;
}

static HttpResult do_request(const char *method, const HttpRequest *request,
                              HttpResponseInfo *response_out, HttpBodySink sink,
                              void *sink_user_data) {
    if (response_out != NULL) {
        response_out->status_code = 0;
        response_out->content_length = -1;
        response_out->accept_ranges = false;
        response_out->is_partial_content = false;
    }

    if (!g_state.ready) {
        log_error("HTTP request attempted before http_client_ps5_startup() "
                  "succeeded: %s",
                  request->url);
        return HTTP_ERR_CONNECT;
    }

    int req = sceHttp2CreateRequestWithURL(g_state.template_id, method,
                                            request->url, 0);
    if (req < 0) {
        log_error("sceHttp2CreateRequestWithURL failed (%d) for %s", req,
                  request->url);
        return HTTP_ERR_CONNECT;
    }

    if (request->authorization_header != NULL) {
        if (sceHttp2AddRequestHeader(req, "Authorization",
                                      request->authorization_header,
                                      HTTP2_HEADER_MODE_ADD)) {
            /* Nonfatal here on purpose: let the server's real response
             * (almost certainly 401/403) be the source of truth about
             * whether auth actually worked, rather than assuming this
             * unverified call's failure means the whole request is dead. */
            log_warn("sceHttp2AddRequestHeader(Authorization) failed for %s",
                     request->url);
        }
    }

    char range_header[80];
    if (request->range_start >= 0) {
        if (request->range_end >= 0) {
            snprintf(range_header, sizeof(range_header), "bytes=%lld-%lld",
                     (long long)request->range_start,
                     (long long)request->range_end);
        } else {
            snprintf(range_header, sizeof(range_header), "bytes=%lld-",
                     (long long)request->range_start);
        }
        if (sceHttp2AddRequestHeader(req, "Range", range_header,
                                      HTTP2_HEADER_MODE_ADD)) {
            log_warn("sceHttp2AddRequestHeader(Range) failed for %s",
                     request->url);
        }
    }

    if (sceHttp2SendRequest(req, NULL, 0)) {
        log_error("sceHttp2SendRequest failed for %s", request->url);
        sceHttp2DeleteRequest(req);
        return HTTP_ERR_CONNECT;
    }

    int status = -1;
    if (sceHttp2GetStatusCode(req, &status)) {
        log_error("sceHttp2GetStatusCode failed for %s", request->url);
        sceHttp2DeleteRequest(req);
        return HTTP_ERR_CONNECT;
    }

    uint64_t content_length = 0;
    bool have_content_length =
        (sceHttp2GetResponseContentLength(req, &content_length) == 0);

    if (response_out != NULL) {
        response_out->status_code = status;
        response_out->content_length =
            have_content_length ? (int64_t)content_length : -1;
        response_out->is_partial_content = (status == 206);
        /* No verified way to read the Accept-Ranges response header in
         * this SDK (sceHttp2GetAllResponseHeaders' output format is
         * undocumented) — a 206 response is itself proof the server
         * honored Range, which is the only case callers actually need to
         * distinguish (see docs/architecture.md's resume-support notes). */
        response_out->accept_ranges = response_out->is_partial_content;
    }

    HttpResult result = HTTP_OK;
    if (status < 200 || status >= 300) {
        result = HTTP_ERR_HTTP_STATUS;
    }

    if (sink != NULL && strcmp(method, "HEAD") != 0) {
        uint8_t buf[8192];
        int n;
        while ((n = sceHttp2ReadData(req, buf, sizeof(buf))) > 0) {
            if (!sink(buf, (size_t)n, sink_user_data)) {
                result = HTTP_ERR_CANCELLED;
                break;
            }
        }
        if (n < 0 && result == HTTP_OK) {
            log_error("sceHttp2ReadData failed for %s", request->url);
            result = HTTP_ERR_CONNECT;
        }
    }

    sceHttp2DeleteRequest(req);
    return result;
}

static HttpResult ps5_get(void *ctx, const HttpRequest *request,
                           HttpResponseInfo *response_out, HttpBodySink sink,
                           void *sink_user_data) {
    (void)ctx;
    return do_request("GET", request, response_out, sink, sink_user_data);
}

static HttpResult ps5_head(void *ctx, const HttpRequest *request,
                            HttpResponseInfo *response_out) {
    (void)ctx;
    return do_request("HEAD", request, response_out, NULL, NULL);
}

void http_client_ps5_init(HttpClient *out) {
    out->ctx = NULL;
    out->get = ps5_get;
    out->head = ps5_head;
}
