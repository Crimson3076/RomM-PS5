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
 * Startup, request creation, Authorization header insertion, and send
 * have now run on real hardware. The first HTTPS send failed before an
 * HTTP status was available; the detailed result logging below was added
 * in response and still needs a hardware retest. See docs/testing.md.
 */
#include "net/http_client_ps5.h"
#include "log/log.h"

#include <errno.h>
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

static void log_sce_error(const char *operation, int result,
                          int saved_errno) {
    log_error("%s failed: rc=0x%08x (%d), errno=%d (%s)", operation,
              (unsigned int)result, result, saved_errno,
              strerror(saved_errno));
}

bool http_client_ps5_startup(void) {
    if (g_state.ready) {
        return true;
    }

    errno = 0;
    int result = sceNetInit();
    if (result != 0) {
        log_sce_error("sceNetInit", result, errno);
        return false;
    }
    log_info("sceNetInit succeeded");

    errno = 0;
    g_state.net_pool = sceNetPoolCreate("rommps5", 32 * 1024, 0);
    if (g_state.net_pool < 0) {
        log_sce_error("sceNetPoolCreate", g_state.net_pool, errno);
        return false;
    }
    log_info("sceNetPoolCreate succeeded: pool_id=%d", g_state.net_pool);

    errno = 0;
    g_state.ssl_ctx = sceSslInit(256 * 1024);
    if (g_state.ssl_ctx < 0) {
        log_sce_error("sceSslInit", g_state.ssl_ctx, errno);
        http_client_ps5_shutdown();
        return false;
    }
    log_info("sceSslInit succeeded: ssl_ctx=%d", g_state.ssl_ctx);

    errno = 0;
    g_state.http_ctx =
        sceHttp2Init(g_state.net_pool, g_state.ssl_ctx, 256 * 1024, 1);
    if (g_state.http_ctx < 0) {
        log_sce_error("sceHttp2Init", g_state.http_ctx, errno);
        http_client_ps5_shutdown();
        return false;
    }
    log_info("sceHttp2Init succeeded: http_ctx=%d", g_state.http_ctx);

    errno = 0;
    g_state.template_id =
        sceHttp2CreateTemplate(g_state.http_ctx, "RomM-PS5/0.1", 3, 1);
    if (g_state.template_id < 0) {
        log_sce_error("sceHttp2CreateTemplate", g_state.template_id, errno);
        http_client_ps5_shutdown();
        return false;
    }
    log_info("sceHttp2CreateTemplate succeeded: template_id=%d",
             g_state.template_id);

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

    log_info("HTTP request starting: method=%s url=%s", method, request->url);
    errno = 0;
    int req = sceHttp2CreateRequestWithURL(g_state.template_id, method,
                                            request->url, 0);
    if (req < 0) {
        int saved_errno = errno;
        log_sce_error("sceHttp2CreateRequestWithURL", req, saved_errno);
        log_error("Request creation failed for %s", request->url);
        return HTTP_ERR_CONNECT;
    }
    log_info("sceHttp2CreateRequestWithURL succeeded: request_id=%d", req);

    if (request->authorization_header != NULL) {
        errno = 0;
        int header_result = sceHttp2AddRequestHeader(
            req, "Authorization", request->authorization_header,
            HTTP2_HEADER_MODE_ADD);
        if (header_result != 0) {
            /* Nonfatal here on purpose: let the server's real response
             * (almost certainly 401/403) be the source of truth about
             * whether auth actually worked, rather than assuming this
             * unverified call's failure means the whole request is dead. */
            int saved_errno = errno;
            log_warn("sceHttp2AddRequestHeader(Authorization) failed: "
                     "rc=0x%08x (%d), errno=%d (%s), url=%s",
                     (unsigned int)header_result, header_result, saved_errno,
                     strerror(saved_errno), request->url);
        } else {
            /* Deliberately log only that the header was added. Never log
             * the header value or the API token it contains. */
            log_info("Authorization header added successfully");
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
        errno = 0;
        int header_result = sceHttp2AddRequestHeader(
            req, "Range", range_header, HTTP2_HEADER_MODE_ADD);
        if (header_result != 0) {
            int saved_errno = errno;
            log_warn("sceHttp2AddRequestHeader(Range) failed: rc=0x%08x "
                     "(%d), errno=%d (%s), url=%s",
                     (unsigned int)header_result, header_result, saved_errno,
                     strerror(saved_errno), request->url);
        } else {
            log_info("Range header added successfully: %s", range_header);
        }
    }

    errno = 0;
    int send_result = sceHttp2SendRequest(req, NULL, 0);
    if (send_result != 0) {
        int saved_errno = errno;
        log_sce_error("sceHttp2SendRequest", send_result, saved_errno);
        log_error("Request send failed for %s", request->url);
        sceHttp2DeleteRequest(req);
        return HTTP_ERR_CONNECT;
    }
    log_info("sceHttp2SendRequest succeeded for %s", request->url);

    int status = -1;
    errno = 0;
    int status_result = sceHttp2GetStatusCode(req, &status);
    if (status_result != 0) {
        int saved_errno = errno;
        log_sce_error("sceHttp2GetStatusCode", status_result, saved_errno);
        log_error("Status-code read failed for %s", request->url);
        sceHttp2DeleteRequest(req);
        return HTTP_ERR_CONNECT;
    }
    log_info("HTTP response: status=%d url=%s", status, request->url);

    uint64_t content_length = 0;
    errno = 0;
    int length_result =
        sceHttp2GetResponseContentLength(req, &content_length);
    bool have_content_length = (length_result == 0);
    if (!have_content_length) {
        int saved_errno = errno;
        log_warn("sceHttp2GetResponseContentLength failed: rc=0x%08x (%d), "
                 "errno=%d (%s), url=%s",
                 (unsigned int)length_result, length_result, saved_errno,
                 strerror(saved_errno), request->url);
    }

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
        while (true) {
            errno = 0;
            n = sceHttp2ReadData(req, buf, sizeof(buf));
            if (n <= 0) {
                break;
            }
            if (!sink(buf, (size_t)n, sink_user_data)) {
                result = HTTP_ERR_CANCELLED;
                break;
            }
        }
        if (n < 0 && result == HTTP_OK) {
            int saved_errno = errno;
            log_sce_error("sceHttp2ReadData", n, saved_errno);
            log_error("Response read failed for %s", request->url);
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
