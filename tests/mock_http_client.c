#include "mock_http_client.h"

#include <stdio.h>
#include <string.h>

static const MockHttpRoute *find_route(MockHttpClient *mock,
                                        const char *url) {
    for (size_t i = 0; i < mock->route_count; i++) {
        if (strstr(url, mock->routes[i].url_contains) != NULL) {
            return &mock->routes[i];
        }
    }
    return NULL;
}

static HttpResult mock_get(void *ctx, const HttpRequest *request,
                            HttpResponseInfo *response_out, HttpBodySink sink,
                            void *sink_user_data) {
    MockHttpClient *mock = (MockHttpClient *)ctx;
    mock->request_count++;

    snprintf(mock->last_url, sizeof(mock->last_url), "%s", request->url);
    snprintf(mock->last_authorization, sizeof(mock->last_authorization), "%s",
              request->authorization_header != NULL
                  ? request->authorization_header
                  : "");
    mock->last_range_start = request->range_start;
    mock->last_range_end = request->range_end;

    const MockHttpRoute *route = find_route(mock, request->url);
    int status_code = route != NULL ? route->status_code : mock->status_code;
    const uint8_t *body = route != NULL ? route->body : mock->body;
    size_t body_len = route != NULL ? route->body_len : mock->body_len;

    int64_t content_length =
        mock->content_length >= 0 ? mock->content_length : (int64_t)body_len;

    if (response_out != NULL) {
        response_out->status_code = status_code;
        response_out->content_length = content_length;
        response_out->is_partial_content = (status_code == 206);
        response_out->accept_ranges = response_out->is_partial_content;
    }

    HttpResult result;
    if (mock->force_result) {
        result = mock->forced_result;
    } else if (status_code >= 200 && status_code < 300) {
        result = HTTP_OK;
    } else {
        result = HTTP_ERR_HTTP_STATUS;
    }

    if (sink != NULL && body_len > 0 &&
        (result == HTTP_OK || result == HTTP_ERR_HTTP_STATUS)) {
        size_t chunk_size = mock->chunk_size > 0 ? mock->chunk_size : body_len;
        size_t offset = 0;
        while (offset < body_len) {
            size_t this_chunk =
                body_len - offset < chunk_size ? body_len - offset : chunk_size;
            if (!sink(body + offset, this_chunk, sink_user_data)) {
                return HTTP_ERR_CANCELLED;
            }
            offset += this_chunk;
        }
    }

    return result;
}

static HttpResult mock_head(void *ctx, const HttpRequest *request,
                             HttpResponseInfo *response_out) {
    return mock_get(ctx, request, response_out, NULL, NULL);
}

void mock_http_client_init(MockHttpClient *mock, HttpClient *out) {
    memset(mock, 0, sizeof(*mock));
    mock->status_code = 200;
    mock->content_length = -1;

    out->ctx = mock;
    out->get = mock_get;
    out->head = mock_head;
}

void mock_http_client_add_route(MockHttpClient *mock,
                                 const char *url_contains, int status_code,
                                 const uint8_t *body, size_t body_len) {
    if (mock->route_count >= MOCK_HTTP_MAX_ROUTES) {
        return;
    }
    mock->routes[mock->route_count].url_contains = url_contains;
    mock->routes[mock->route_count].status_code = status_code;
    mock->routes[mock->route_count].body = body;
    mock->routes[mock->route_count].body_len = body_len;
    mock->route_count++;
}
