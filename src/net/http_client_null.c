#include "net/http_client.h"

static HttpResult null_get(void *ctx, const HttpRequest *request,
                            HttpResponseInfo *response_out, HttpBodySink sink,
                            void *sink_user_data) {
    (void)ctx;
    (void)request;
    (void)sink;
    (void)sink_user_data;
    if (response_out != NULL) {
        response_out->status_code = 0;
        response_out->content_length = -1;
        response_out->accept_ranges = false;
        response_out->is_partial_content = false;
    }
    return HTTP_ERR_UNIMPLEMENTED;
}

static HttpResult null_head(void *ctx, const HttpRequest *request,
                             HttpResponseInfo *response_out) {
    (void)ctx;
    (void)request;
    if (response_out != NULL) {
        response_out->status_code = 0;
        response_out->content_length = -1;
        response_out->accept_ranges = false;
        response_out->is_partial_content = false;
    }
    return HTTP_ERR_UNIMPLEMENTED;
}

void http_client_null_init(HttpClient *out) {
    out->ctx = NULL;
    out->get = null_get;
    out->head = null_head;
}
