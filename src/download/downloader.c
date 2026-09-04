#include "download/downloader.h"
#include "download/zip_extract.h"
#include "log/log.h"
#include "net/url_encode.h"
#include "pathval/path_validate.h"
#include "storage/storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DOWNLOAD_SPACE_MARGIN_MIN_BYTES (16ULL * 1024 * 1024)
#define DOWNLOAD_PATH_MAX 700

typedef struct {
    FILE *file;
    DownloadProgress *progress;
    uint64_t base_offset;         /* bytes already on disk before this attempt */
    uint64_t written_this_attempt;
    time_t last_update_time;
    uint64_t bytes_since_last_update;
    bool sent_range_request;
    bool first_chunk;
    const HttpResponseInfo *response_info; /* same memory as get()'s response_out;
                                             * populated before the first sink call
                                             * per the HttpClient interface contract */
    DownloaderShouldCancelFn should_cancel;
    DownloaderProgressFn on_progress;
    void *user_data;
} SinkContext;

static const char *format_extension(RommGameFormat format) {
    switch (format) {
    case ROMM_FORMAT_FFPKG:
        return "ffpkg";
    case ROMM_FORMAT_EXFAT:
        return "exfat";
    case ROMM_FORMAT_FFPFS:
        return "ffpfs";
    case ROMM_FORMAT_FFPFSC:
        return "ffpfsc";
    default:
        return NULL; /* folder or unknown format: no forced extension */
    }
}

/* Builds a filesystem-safe base name from a RomM-provided string:
 * rejected outright via pathval if it looks like a traversal/absolute
 * path attempt (defense in depth against a compromised or malicious
 * server), and any literal slash that survives that check is replaced
 * rather than allowed to create unexpected subdirectories. */
static bool safe_base_name(const char *raw, char *out, size_t out_capacity) {
    if (raw == NULL || raw[0] == '\0') {
        return false;
    }
    if (!path_validate_entry_is_safe(raw)) {
        log_error("Rejected unsafe game name from server: '%s'", raw);
        return false;
    }
    size_t len = strlen(raw);
    if (len >= out_capacity) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = raw[i];
        out[i] = (c == '/' || c == '\\') ? '_' : c;
    }
    out[len] = '\0';
    return true;
}

static bool build_paths(const DownloaderConfig *config, const RommGame *game,
                         char *final_path, char *temp_path, bool *is_folder) {
    char base[400];
    bool have_fs_name = game->fs_name[0] != '\0';
    const char *name_source = have_fs_name ? game->fs_name : game->title;
    if (!safe_base_name(name_source, base, sizeof(base))) {
        return false;
    }

    *is_folder = (game->format == ROMM_FORMAT_FOLDER);
    int n;

    if (*is_folder) {
        n = snprintf(final_path, DOWNLOAD_PATH_MAX, "%s/%s", config->dest_root,
                     base);
        if (n < 0 || n >= DOWNLOAD_PATH_MAX) {
            return false;
        }
        n = snprintf(temp_path, DOWNLOAD_PATH_MAX,
                     "%s/%s.download-partial.zip", config->dest_root, base);
    } else if (have_fs_name) {
        /* fs_name is RomM's real on-disk filename (see romm_api.h) and
         * already carries whatever extension applies — appending
         * format_extension() on top of it here would turn e.g.
         * "Game.ffpkg" into "Game.ffpkg.ffpkg". Only the title fallback
         * below (no fs_name available) needs an extension synthesized. */
        n = snprintf(final_path, DOWNLOAD_PATH_MAX, "%s/%s", config->dest_root,
                     base);
        if (n < 0 || n >= DOWNLOAD_PATH_MAX) {
            return false;
        }
        n = snprintf(temp_path, DOWNLOAD_PATH_MAX, "%s/%s.part",
                     config->dest_root, base);
    } else {
        const char *ext = format_extension(game->format);
        if (ext != NULL) {
            n = snprintf(final_path, DOWNLOAD_PATH_MAX, "%s/%s.%s",
                         config->dest_root, base, ext);
            if (n < 0 || n >= DOWNLOAD_PATH_MAX) {
                return false;
            }
            n = snprintf(temp_path, DOWNLOAD_PATH_MAX, "%s/%s.%s.part",
                         config->dest_root, base, ext);
        } else {
            n = snprintf(final_path, DOWNLOAD_PATH_MAX, "%s/%s",
                         config->dest_root, base);
            if (n < 0 || n >= DOWNLOAD_PATH_MAX) {
                return false;
            }
            n = snprintf(temp_path, DOWNLOAD_PATH_MAX, "%s/%s.part",
                         config->dest_root, base);
        }
    }
    return n >= 0 && n < DOWNLOAD_PATH_MAX;
}

static bool build_content_url(const DownloaderConfig *config,
                               const RommGame *game, char *url,
                               size_t url_capacity) {
    const char *name_source = game->fs_name[0] != '\0' ? game->fs_name : game->title;
    char encoded[512];
    url_encode(name_source, encoded, sizeof(encoded));

    int n = snprintf(url, url_capacity, "%s/api/roms/%d/content/%s",
                      config->server_url, game->id, encoded);
    return n >= 0 && (size_t)n < url_capacity;
}

static bool download_sink(const uint8_t *chunk, size_t chunk_len,
                           void *user_data) {
    SinkContext *sc = (SinkContext *)user_data;

    if (sc->first_chunk) {
        sc->first_chunk = false;
        if (sc->sent_range_request && sc->response_info->status_code != 206) {
            /* Server ignored/refused our Range request (200, not 206) —
             * must restart from zero rather than append this full body
             * after old partial bytes. See docs/architecture.md's
             * Range/resume findings: this is expected, not a bug. */
            log_warn("Server did not honor Range resume (status %d); "
                     "restarting this download from zero",
                     sc->response_info->status_code);
            if (ftruncate(fileno(sc->file), 0) != 0) {
                log_error("Could not truncate temp file for restart");
                return false;
            }
            rewind(sc->file);
            sc->base_offset = 0;
        }
    }

    if (fwrite(chunk, 1, chunk_len, sc->file) != chunk_len) {
        log_error("Write to temp file failed");
        return false;
    }

    sc->written_this_attempt += chunk_len;
    sc->bytes_since_last_update += chunk_len;

    /* On a 206 resume, content_length is the length of the remaining
     * range, not the whole resource — adding base_offset recovers the
     * true total in both the resume and fresh-download cases. */
    uint64_t total = 0;
    if (sc->response_info->content_length >= 0) {
        total = sc->base_offset + (uint64_t)sc->response_info->content_length;
    }
    uint64_t transferred = sc->base_offset + sc->written_this_attempt;
    download_progress_update_bytes(sc->progress, transferred, total);

    time_t now = time(NULL);
    if (now != sc->last_update_time) {
        double elapsed = difftime(now, sc->last_update_time);
        if (elapsed > 0 && sc->last_update_time != 0) {
            sc->progress->speed_bytes_per_sec =
                (uint64_t)((double)sc->bytes_since_last_update / elapsed);
        }
        sc->bytes_since_last_update = 0;
        sc->last_update_time = now;
    }

    if (sc->on_progress != NULL) {
        sc->on_progress(sc->progress, sc->user_data);
    }

    if (sc->should_cancel != NULL && sc->should_cancel(sc->user_data)) {
        log_info("Download cancelled by user");
        return false;
    }

    return true;
}

static bool check_storage(const DownloaderConfig *config, uint64_t required,
                           uint64_t already_on_disk,
                           DownloadProgress *progress) {
    if (required == 0) {
        return true; /* unknown size reported by server; can't refuse in advance */
    }

    uint64_t free_bytes = 0;
    if (!storage_get_free_bytes(config->dest_root, &free_bytes)) {
        download_progress_fail(progress,
                                "Could not determine free space for destination");
        return false;
    }

    uint64_t still_needed = required > already_on_disk
                                 ? required - already_on_disk
                                 : 0;
    uint64_t margin = required / 20; /* +5%, covers folder-zip framing overhead */
    if (margin < DOWNLOAD_SPACE_MARGIN_MIN_BYTES) {
        margin = DOWNLOAD_SPACE_MARGIN_MIN_BYTES;
    }

    if (free_bytes < still_needed + margin) {
        char reason[192];
        snprintf(reason, sizeof(reason),
                 "Insufficient storage: need ~%llu MB more, have %llu MB free",
                 (unsigned long long)((still_needed + margin) / (1024 * 1024)),
                 (unsigned long long)(free_bytes / (1024 * 1024)));
        download_progress_fail(progress, reason);
        return false;
    }

    return true;
}

static bool finish_folder_game(const char *temp_path, const char *final_path,
                                DownloadProgress *progress) {
    if (!download_progress_transition(progress, DL_STATE_EXTRACTING)) {
        download_progress_fail(progress, "Internal error entering EXTRACTING state");
        return false;
    }

    uint32_t extracted_count = 0;
    ZipExtractResult zr = zip_extract_stored(temp_path, final_path, &extracted_count);
    if (zr != ZIP_EXTRACT_OK) {
        log_error("ZIP extraction of %s into %s failed (code %d) after %u "
                  "entries — left in place, not registered as installed",
                  temp_path, final_path, zr, extracted_count);
        char reason[160];
        snprintf(reason, sizeof(reason),
                 "ZIP extraction failed (code %d) after %u entries", zr,
                 extracted_count);
        download_progress_fail(progress, reason);
        return false;
    }

    char param_path[DOWNLOAD_PATH_MAX + 32];
    snprintf(param_path, sizeof(param_path), "%s/sce_sys/param.json", final_path);
    struct stat param_stat;
    if (stat(param_path, &param_stat) != 0) {
        download_progress_fail(progress,
                                "Extracted folder is missing sce_sys/param.json");
        return false;
    }

    if (!download_progress_transition(progress, DL_STATE_VALIDATING)) {
        download_progress_fail(progress, "Internal error entering VALIDATING state");
        return false;
    }

    /* Only remove the temp zip once extraction AND validation succeeded. */
    if (remove(temp_path) != 0) {
        log_warn("Could not remove temp zip %s after successful extraction "
                 "(nonfatal — the game itself is fully installed)",
                 temp_path);
    }

    return download_progress_transition(progress, DL_STATE_COMPLETED);
}

static bool finish_single_file_game(const char *temp_path,
                                     const char *final_path,
                                     uint64_t expected_size,
                                     DownloadProgress *progress) {
    if (!download_progress_transition(progress, DL_STATE_VALIDATING)) {
        download_progress_fail(progress, "Internal error entering VALIDATING state");
        return false;
    }

    struct stat temp_stat;
    if (expected_size > 0 && stat(temp_path, &temp_stat) == 0) {
        if ((uint64_t)temp_stat.st_size != expected_size) {
            char reason[192];
            snprintf(reason, sizeof(reason),
                     "Downloaded size %lld does not match expected %llu — "
                     "not renaming to final destination",
                     (long long)temp_stat.st_size,
                     (unsigned long long)expected_size);
            download_progress_fail(progress, reason);
            return false;
        }
    }

    if (rename(temp_path, final_path) != 0) {
        char reason[192];
        snprintf(reason, sizeof(reason),
                 "Could not rename temp file to final destination (errno %d)",
                 errno);
        download_progress_fail(progress, reason);
        return false;
    }

    return download_progress_transition(progress, DL_STATE_COMPLETED);
}

bool downloader_run(const DownloaderConfig *config, const RommGame *game,
                     DownloadProgress *progress,
                     DownloaderShouldCancelFn should_cancel,
                     DownloaderProgressFn on_progress, void *user_data) {
    char final_path[DOWNLOAD_PATH_MAX];
    char temp_path[DOWNLOAD_PATH_MAX];
    bool is_folder;

    if (!build_paths(config, game, final_path, temp_path, &is_folder)) {
        download_progress_fail(progress, "Could not build a safe destination path");
        return false;
    }

    char url[ROMM_TITLE_MAX + 256];
    if (!build_content_url(config, game, url, sizeof(url))) {
        download_progress_fail(progress, "Could not build a request URL");
        return false;
    }

    if (!download_progress_transition(progress, DL_STATE_DOWNLOADING)) {
        log_error("downloader_run: cannot start — progress is not in a "
                  "state that allows DOWNLOADING (state=%d)",
                  progress->state);
        return false;
    }

    struct stat temp_stat;
    uint64_t base_offset = 0;
    if (stat(temp_path, &temp_stat) == 0 && S_ISREG(temp_stat.st_mode) &&
        temp_stat.st_size > 0) {
        base_offset = (uint64_t)temp_stat.st_size;
        log_info("Resuming %s from byte %llu", game->title,
                 (unsigned long long)base_offset);
    }
    /* The transition above reset the counters to zero; correct that
     * immediately for a resumed attempt (legal: still DL_STATE_DOWNLOADING). */
    download_progress_update_bytes(progress, base_offset, 0);

    if (!check_storage(config, game->fs_size_bytes, base_offset, progress)) {
        return false;
    }

    FILE *file = fopen(temp_path, base_offset > 0 ? "ab" : "wb");
    if (file == NULL) {
        log_error("Could not open %s for writing (errno %d)", temp_path,
                  errno);
        char reason[64];
        snprintf(reason, sizeof(reason),
                 "Could not open temp file for writing (errno %d)", errno);
        download_progress_fail(progress, reason);
        return false;
    }

    HttpRequest request;
    memset(&request, 0, sizeof(request));
    request.url = url;
    request.authorization_header = config->auth_header;
    request.range_start = base_offset > 0 ? (int64_t)base_offset : -1;
    request.range_end = -1;

    HttpResponseInfo response_info;
    memset(&response_info, 0, sizeof(response_info));

    SinkContext sink_ctx;
    memset(&sink_ctx, 0, sizeof(sink_ctx));
    sink_ctx.file = file;
    sink_ctx.progress = progress;
    sink_ctx.base_offset = base_offset;
    sink_ctx.sent_range_request = (request.range_start >= 0);
    sink_ctx.first_chunk = true;
    sink_ctx.response_info = &response_info;
    sink_ctx.should_cancel = should_cancel;
    sink_ctx.on_progress = on_progress;
    sink_ctx.user_data = user_data;

    HttpResult http_result =
        config->http->get(config->http->ctx, &request, &response_info,
                           download_sink, &sink_ctx);

    fclose(file);

    if (http_result == HTTP_ERR_CANCELLED) {
        download_progress_transition(progress, DL_STATE_CANCELLED);
        return false;
    }
    if (http_result != HTTP_OK) {
        char reason[160];
        snprintf(reason, sizeof(reason),
                 "Download request failed (result=%d, http_status=%d)",
                 http_result, response_info.status_code);
        download_progress_fail(progress, reason);
        return false;
    }

    if (is_folder) {
        return finish_folder_game(temp_path, final_path, progress);
    }
    return finish_single_file_game(temp_path, final_path, game->fs_size_bytes,
                                    progress);
}
