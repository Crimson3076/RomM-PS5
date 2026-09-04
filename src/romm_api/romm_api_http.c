#include "romm_api/romm_api_http.h"
#include "log/log.h"
#include "net/url_encode.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp */

/* Response bodies are buffered whole before parsing (cJSON is a DOM
 * parser) — bounded so a misbehaving server can't grow this without
 * limit. A page of RomM roms JSON (with the sidecar indexes disabled,
 * see the query string built below) is expected to be well under this. */
#define ROMM_MAX_RESPONSE_BYTES (4 * 1024 * 1024)

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} DynBuf;

static bool dynbuf_sink(const uint8_t *chunk, size_t chunk_len,
                         void *user_data) {
    DynBuf *buf = (DynBuf *)user_data;

    if (buf->len + chunk_len + 1 > ROMM_MAX_RESPONSE_BYTES) {
        log_error("RomM response exceeded %d bytes; aborting",
                  ROMM_MAX_RESPONSE_BYTES);
        return false;
    }

    if (buf->len + chunk_len + 1 > buf->cap) {
        size_t new_cap = buf->cap == 0 ? 8192 : buf->cap * 2;
        while (new_cap < buf->len + chunk_len + 1) {
            new_cap *= 2;
        }
        char *grown = realloc(buf->data, new_cap);
        if (grown == NULL) {
            log_error("RomM response buffer allocation failed");
            return false;
        }
        buf->data = grown;
        buf->cap = new_cap;
    }

    memcpy(buf->data + buf->len, chunk, chunk_len);
    buf->len += chunk_len;
    buf->data[buf->len] = '\0';
    return true;
}

static void dynbuf_free(DynBuf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static RommResult result_from_http(HttpResult http_result, int status_code) {
    if (http_result == HTTP_ERR_CANCELLED) {
        return ROMM_ERR_NETWORK;
    }
    if (http_result != HTTP_OK && http_result != HTTP_ERR_HTTP_STATUS) {
        return ROMM_ERR_NETWORK;
    }
    if (status_code == 401 || status_code == 403) {
        return ROMM_ERR_AUTH;
    }
    if (status_code == 404) {
        return ROMM_ERR_NOT_FOUND;
    }
    if (status_code < 200 || status_code >= 300) {
        return ROMM_ERR_NETWORK;
    }
    return ROMM_OK;
}

/* GETs `path` (appended to ctx->server_url) with the auth header, buffering
 * the whole body. Caller must dynbuf_free() `out` on ROMM_OK. */
static RommResult romm_get_json(RommApiHttpContext *ctx, const char *path,
                                 DynBuf *out) {
    char url[ROMM_SERVER_URL_MAX + 512];
    int written = snprintf(url, sizeof(url), "%s%s", ctx->server_url, path);
    if (written < 0 || (size_t)written >= sizeof(url)) {
        log_error("RomM request URL too long for path %s", path);
        return ROMM_ERR_UNSUPPORTED;
    }

    HttpRequest request = {
        .url = url,
        .authorization_header = ctx->auth_header,
        .range_start = -1,
        .range_end = -1,
    };

    memset(out, 0, sizeof(*out));
    HttpResponseInfo response_info;
    HttpResult http_result = ctx->http->get(ctx->http->ctx, &request,
                                             &response_info, dynbuf_sink, out);

    RommResult result = result_from_http(http_result, response_info.status_code);
    if (result != ROMM_OK) {
        log_warn("RomM GET %s failed: http_result=%d status=%d", path,
                 http_result, response_info.status_code);
        dynbuf_free(out);
        return result;
    }

    return ROMM_OK;
}

static RommResult ensure_ps5_platform_resolved(RommApiHttpContext *ctx) {
    if (ctx->platform_resolved) {
        return ROMM_OK;
    }

    DynBuf body;
    RommResult result = romm_get_json(ctx, "/api/platforms", &body);
    if (result != ROMM_OK) {
        return result;
    }

    cJSON *root = cJSON_Parse(body.data);
    dynbuf_free(&body);
    if (root == NULL || !cJSON_IsArray(root)) {
        log_error("RomM /api/platforms did not return a JSON array");
        cJSON_Delete(root);
        return ROMM_ERR_NETWORK;
    }

    int32_t found_id = -1;
    cJSON *platform;
    cJSON_ArrayForEach(platform, root) {
        cJSON *slug = cJSON_GetObjectItemCaseSensitive(platform, "fs_slug");
        if (!cJSON_IsString(slug)) {
            slug = cJSON_GetObjectItemCaseSensitive(platform, "slug");
        }
        if (cJSON_IsString(slug) && slug->valuestring != NULL &&
            strcmp(slug->valuestring, "ps5") == 0) {
            cJSON *id = cJSON_GetObjectItemCaseSensitive(platform, "id");
            if (cJSON_IsNumber(id)) {
                found_id = (int32_t)id->valuedouble;
                break;
            }
        }
    }
    cJSON_Delete(root);

    if (found_id < 0) {
        log_error("RomM server has no 'ps5' platform in /api/platforms");
        return ROMM_ERR_NOT_FOUND;
    }

    ctx->ps5_platform_id = found_id;
    ctx->platform_resolved = true;
    log_info("Resolved RomM PS5 platform id: %d", found_id);
    return ROMM_OK;
}

static RommGameFormat format_from_extension(const char *ext) {
    if (ext == NULL || ext[0] == '\0') {
        return ROMM_FORMAT_FOLDER;
    }
    if (strcasecmp(ext, "ffpkg") == 0) {
        return ROMM_FORMAT_FFPKG;
    }
    if (strcasecmp(ext, "exfat") == 0) {
        return ROMM_FORMAT_EXFAT;
    }
    if (strcasecmp(ext, "ffpfsc") == 0) {
        return ROMM_FORMAT_FFPFSC;
    }
    if (strcasecmp(ext, "ffpfs") == 0) {
        return ROMM_FORMAT_FFPFS;
    }
    return ROMM_FORMAT_UNKNOWN;
}

static void copy_json_string(cJSON *obj, const char *key, char *out,
                              size_t out_capacity) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(value) && value->valuestring != NULL) {
        snprintf(out, out_capacity, "%s", value->valuestring);
    } else {
        out[0] = '\0';
    }
}

static RommResult http_test_connection(void *ctx_ptr) {
    RommApiHttpContext *ctx = (RommApiHttpContext *)ctx_ptr;
    return ensure_ps5_platform_resolved(ctx);
}

static RommResult http_list_ps5_games(void *ctx_ptr, const char *search_term,
                                       RommSortOrder sort, size_t offset,
                                       size_t limit, RommGamePage *page_out) {
    RommApiHttpContext *ctx = (RommApiHttpContext *)ctx_ptr;

    RommResult result = ensure_ps5_platform_resolved(ctx);
    if (result != ROMM_OK) {
        return result;
    }

    char encoded_search[384] = "";
    if (search_term != NULL && search_term[0] != '\0') {
        url_encode(search_term, encoded_search, sizeof(encoded_search));
    }

    char path[640];
    snprintf(path, sizeof(path),
             "/api/roms?platform_ids=%d&limit=%zu&offset=%zu"
             "&order_by=name&order_dir=%s"
             "&with_char_index=false&with_filter_values=false"
             "&with_rom_id_index=false&with_total=true%s%s",
             ctx->ps5_platform_id, limit, offset,
             sort == ROMM_SORT_TITLE_DESC ? "desc" : "asc",
             encoded_search[0] != '\0' ? "&search_term=" : "", encoded_search);

    DynBuf body;
    result = romm_get_json(ctx, path, &body);
    if (result != ROMM_OK) {
        return result;
    }

    cJSON *root = cJSON_Parse(body.data);
    dynbuf_free(&body);
    if (root == NULL || !cJSON_IsObject(root)) {
        log_error("RomM /api/roms did not return a JSON object");
        cJSON_Delete(root);
        return ROMM_ERR_NETWORK;
    }

    cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "items");
    cJSON *total = cJSON_GetObjectItemCaseSensitive(root, "total");

    size_t item_count = cJSON_IsArray(items) ? (size_t)cJSON_GetArraySize(items) : 0;
    RommGame *games = NULL;
    if (item_count > 0) {
        games = calloc(item_count, sizeof(RommGame));
        if (games == NULL) {
            cJSON_Delete(root);
            log_error("Failed to allocate %zu RommGame entries", item_count);
            return ROMM_ERR_UNSUPPORTED;
        }
    }

    size_t i = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, items) {
        if (i >= item_count) {
            break;
        }
        RommGame *game = &games[i];

        cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        game->id = cJSON_IsNumber(id) ? (int32_t)id->valuedouble : 0;

        copy_json_string(item, "name", game->title, sizeof(game->title));
        if (game->title[0] == '\0') {
            copy_json_string(item, "fs_name_no_tags", game->title,
                              sizeof(game->title));
        }
        copy_json_string(item, "fs_name", game->fs_name, sizeof(game->fs_name));
        if (game->title[0] == '\0') {
            snprintf(game->title, sizeof(game->title), "%s", game->fs_name);
        }

        char extension[16];
        copy_json_string(item, "fs_extension", extension, sizeof(extension));
        game->format = format_from_extension(extension);

        cJSON *size = cJSON_GetObjectItemCaseSensitive(item, "fs_size_bytes");
        game->fs_size_bytes = cJSON_IsNumber(size) ? (uint64_t)size->valuedouble : 0;

        /* RomM's SimpleRomSchema does not expose a PS5 title id or version
         * field directly (see docs/architecture.md) — left empty rather
         * than guessing a field name that might not exist. */
        game->title_id[0] = '\0';
        game->version[0] = '\0';
        game->has_cover_art = false; /* cover art fetching not implemented this milestone */

        i++;
    }

    cJSON_Delete(root);

    page_out->items = games;
    page_out->count = i;
    page_out->total = cJSON_IsNumber(total) ? (size_t)total->valuedouble : i;
    page_out->offset = offset;
    return ROMM_OK;
}

static void http_list_ps5_games_free(RommGamePage *page) {
    if (page == NULL) {
        return;
    }
    free(page->items);
    page->items = NULL;
    page->count = 0;
    page->total = 0;
    page->offset = 0;
}

void romm_api_http_init(RommApi *out, RommApiHttpContext *storage,
                         const HttpClient *http, const char *server_url,
                         const char *api_token) {
    memset(storage, 0, sizeof(*storage));
    storage->http = http;
    snprintf(storage->server_url, sizeof(storage->server_url), "%s",
             server_url);
    snprintf(storage->auth_header, sizeof(storage->auth_header), "Bearer %s",
             api_token);
    storage->ps5_platform_id = -1;
    storage->platform_resolved = false;

    out->ctx = storage;
    out->test_connection = http_test_connection;
    out->list_ps5_games = http_list_ps5_games;
    out->list_ps5_games_free = http_list_ps5_games_free;
}
