#include "test_framework.h"
#include "download/downloader.h"
#include "mock_http_client.h"
#include "synthetic_zip.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void mkdir_p(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0700);
            *p = '/';
        }
    }
    mkdir(tmp, 0700);
}

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool read_whole_file(const char *path, char *out, size_t out_capacity) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    size_t n = fread(out, 1, out_capacity - 1, f);
    out[n] = '\0';
    fclose(f);
    return true;
}

static bool cancel_after_first_chunk(void *user_data) {
    int *call_count = (int *)user_data;
    (*call_count)++;
    return *call_count >= 2;
}

void test_downloader(TestCounters *tc) {
    char dest_root[80];
    snprintf(dest_root, sizeof(dest_root), "/tmp/rommps5_test_dl_%d",
              getpid());
    mkdir_p(dest_root);

    /* --- Fresh single-file download, success --- */
    {
        MockHttpClient mock;
        HttpClient http;
        mock_http_client_init(&mock, &http);
        static const char body[] = "0123456789";
        mock.status_code = 200;
        mock.body = (const uint8_t *)body;
        mock.body_len = strlen(body);

        DownloaderConfig config = {
            .http = &http,
            .server_url = "http://romm.example.com",
            .auth_header = "Bearer rmm_test",
            .dest_root = dest_root,
        };
        RommGame game;
        memset(&game, 0, sizeof(game));
        game.id = 1;
        snprintf(game.title, sizeof(game.title), "Test Game");
        snprintf(game.fs_name, sizeof(game.fs_name), "Test Game.ffpkg");
        game.format = ROMM_FORMAT_FFPKG;
        game.fs_size_bytes = strlen(body);

        DownloadProgress progress;
        download_progress_init(&progress);

        bool ok = downloader_run(&config, &game, &progress, NULL, NULL, NULL);
        TEST_CHECK(tc, ok == true);
        TEST_CHECK(tc, progress.state == DL_STATE_COMPLETED);

        char final_path[160];
        snprintf(final_path, sizeof(final_path), "%s/Test Game.ffpkg",
                  dest_root);
        char content[64];
        TEST_CHECK(tc, read_whole_file(final_path, content, sizeof(content)));
        TEST_CHECK(tc, strcmp(content, body) == 0);

        char temp_path[160];
        snprintf(temp_path, sizeof(temp_path), "%s/Test Game.ffpkg.part",
                  dest_root);
        TEST_CHECK(tc, !file_exists(temp_path));
    }

    /* --- Resume: server honors Range (206), appends correctly --- */
    {
        char temp_path[160];
        snprintf(temp_path, sizeof(temp_path), "%s/Resume Game.ffpkg.part",
                  dest_root);
        FILE *f = fopen(temp_path, "wb");
        fwrite("0123", 1, 4, f);
        fclose(f);

        MockHttpClient mock;
        HttpClient http;
        mock_http_client_init(&mock, &http);
        static const char rest[] = "456789";
        mock.status_code = 206;
        mock.body = (const uint8_t *)rest;
        mock.body_len = strlen(rest);

        DownloaderConfig config = {
            .http = &http,
            .server_url = "http://romm.example.com",
            .auth_header = "Bearer rmm_test",
            .dest_root = dest_root,
        };
        RommGame game;
        memset(&game, 0, sizeof(game));
        game.id = 2;
        snprintf(game.title, sizeof(game.title), "Resume Game");
        snprintf(game.fs_name, sizeof(game.fs_name), "Resume Game.ffpkg");
        game.format = ROMM_FORMAT_FFPKG;
        game.fs_size_bytes = 10;

        DownloadProgress progress;
        download_progress_init(&progress);

        bool ok = downloader_run(&config, &game, &progress, NULL, NULL, NULL);
        TEST_CHECK(tc, ok == true);
        TEST_CHECK(tc, progress.state == DL_STATE_COMPLETED);
        TEST_CHECK(tc, mock.last_range_start == 4);

        char final_path[160];
        snprintf(final_path, sizeof(final_path), "%s/Resume Game.ffpkg",
                  dest_root);
        char content[64];
        TEST_CHECK(tc, read_whole_file(final_path, content, sizeof(content)));
        TEST_CHECK(tc, strcmp(content, "0123456789") == 0);
    }

    /* --- Resume attempted but server ignores Range (200): must restart
     * from zero, not corrupt by appending the fresh body after the stale
     * partial bytes --- */
    {
        char temp_path[160];
        snprintf(temp_path, sizeof(temp_path), "%s/Restart Game.ffpkg.part",
                  dest_root);
        FILE *f = fopen(temp_path, "wb");
        fwrite("STALE", 1, 5, f);
        fclose(f);

        MockHttpClient mock;
        HttpClient http;
        mock_http_client_init(&mock, &http);
        static const char fresh_body[] = "9876543210";
        mock.status_code = 200; /* not 206: Range was not honored */
        mock.body = (const uint8_t *)fresh_body;
        mock.body_len = strlen(fresh_body);

        DownloaderConfig config = {
            .http = &http,
            .server_url = "http://romm.example.com",
            .auth_header = "Bearer rmm_test",
            .dest_root = dest_root,
        };
        RommGame game;
        memset(&game, 0, sizeof(game));
        game.id = 3;
        snprintf(game.title, sizeof(game.title), "Restart Game");
        snprintf(game.fs_name, sizeof(game.fs_name), "Restart Game.ffpkg");
        game.format = ROMM_FORMAT_FFPKG;
        game.fs_size_bytes = strlen(fresh_body);

        DownloadProgress progress;
        download_progress_init(&progress);

        bool ok = downloader_run(&config, &game, &progress, NULL, NULL, NULL);
        TEST_CHECK(tc, ok == true);

        char final_path[160];
        snprintf(final_path, sizeof(final_path), "%s/Restart Game.ffpkg",
                  dest_root);
        char content[64];
        TEST_CHECK(tc, read_whole_file(final_path, content, sizeof(content)));
        TEST_CHECK(tc, strcmp(content, fresh_body) == 0);
    }

    /* --- Refuses to start when the reported size can't possibly fit --- */
    {
        MockHttpClient mock;
        HttpClient http;
        mock_http_client_init(&mock, &http);

        DownloaderConfig config = {
            .http = &http,
            .server_url = "http://romm.example.com",
            .auth_header = "Bearer rmm_test",
            .dest_root = dest_root,
        };
        RommGame game;
        memset(&game, 0, sizeof(game));
        game.id = 4;
        snprintf(game.title, sizeof(game.title), "Huge Game");
        snprintf(game.fs_name, sizeof(game.fs_name), "Huge Game.ffpkg");
        game.format = ROMM_FORMAT_FFPKG;
        game.fs_size_bytes = 1ULL << 60; /* far more than any real free space */

        DownloadProgress progress;
        download_progress_init(&progress);

        bool ok = downloader_run(&config, &game, &progress, NULL, NULL, NULL);
        TEST_CHECK(tc, ok == false);
        TEST_CHECK(tc, progress.state == DL_STATE_FAILED);
        TEST_CHECK(tc, strstr(progress.failure_reason, "Insufficient") != NULL);
        TEST_CHECK(tc, mock.request_count == 0); /* never even made the request */
    }

    /* --- Cancellation mid-transfer leaves the partial file in place --- */
    {
        MockHttpClient mock;
        HttpClient http;
        mock_http_client_init(&mock, &http);
        static const char body[] = "abcdefghijklmnop";
        mock.status_code = 200;
        mock.body = (const uint8_t *)body;
        mock.body_len = strlen(body);
        mock.chunk_size = 4; /* force multiple sink calls */

        DownloaderConfig config = {
            .http = &http,
            .server_url = "http://romm.example.com",
            .auth_header = "Bearer rmm_test",
            .dest_root = dest_root,
        };
        RommGame game;
        memset(&game, 0, sizeof(game));
        game.id = 5;
        snprintf(game.title, sizeof(game.title), "Cancel Game");
        snprintf(game.fs_name, sizeof(game.fs_name), "Cancel Game.ffpkg");
        game.format = ROMM_FORMAT_FFPKG;
        game.fs_size_bytes = strlen(body);

        DownloadProgress progress;
        download_progress_init(&progress);

        int cancel_calls = 0;
        bool ok = downloader_run(&config, &game, &progress,
                                   cancel_after_first_chunk, NULL,
                                   &cancel_calls);
        TEST_CHECK(tc, ok == false);
        TEST_CHECK(tc, progress.state == DL_STATE_CANCELLED);

        char temp_path[160];
        snprintf(temp_path, sizeof(temp_path), "%s/Cancel Game.ffpkg.part",
                  dest_root);
        TEST_CHECK(tc, file_exists(temp_path));

        char content[64];
        TEST_CHECK(tc, read_whole_file(temp_path, content, sizeof(content)));
        TEST_CHECK(tc, strlen(content) < strlen(body));
    }

    /* --- Folder-format game: downloads a ZIP and extracts it --- */
    {
        const char *param_json = "{\"fake\":\"param\"}";
        SyntheticZipEntry entries[] = {
            {.name = "sce_sys/param.json",
             .content = (const uint8_t *)param_json,
             .content_len = strlen(param_json),
             .method = 0,
             .gp_flag = 0},
            {.name = "eboot.bin",
             .content = (const uint8_t *)"EBOOT",
             .content_len = 5,
             .method = 0,
             .gp_flag = 0},
        };
        uint8_t zip_buf[4096];
        size_t zip_len = build_synthetic_zip(zip_buf, sizeof(zip_buf), entries, 2);
        TEST_CHECK(tc, zip_len > 0);

        MockHttpClient mock;
        HttpClient http;
        mock_http_client_init(&mock, &http);
        mock.status_code = 200;
        mock.body = zip_buf;
        mock.body_len = zip_len;

        DownloaderConfig config = {
            .http = &http,
            .server_url = "http://romm.example.com",
            .auth_header = "Bearer rmm_test",
            .dest_root = dest_root,
        };
        RommGame game;
        memset(&game, 0, sizeof(game));
        game.id = 6;
        snprintf(game.title, sizeof(game.title), "Folder Game");
        snprintf(game.fs_name, sizeof(game.fs_name), "Folder Game");
        game.format = ROMM_FORMAT_FOLDER;
        game.fs_size_bytes = zip_len;

        DownloadProgress progress;
        download_progress_init(&progress);

        bool ok = downloader_run(&config, &game, &progress, NULL, NULL, NULL);
        TEST_CHECK(tc, ok == true);
        TEST_CHECK(tc, progress.state == DL_STATE_COMPLETED);

        char param_path[192];
        snprintf(param_path, sizeof(param_path),
                  "%s/Folder Game/sce_sys/param.json", dest_root);
        char content[64];
        TEST_CHECK(tc, read_whole_file(param_path, content, sizeof(content)));
        TEST_CHECK(tc, strcmp(content, param_json) == 0);

        char temp_zip[192];
        snprintf(temp_zip, sizeof(temp_zip),
                  "%s/Folder Game.download-partial.zip", dest_root);
        TEST_CHECK(tc, !file_exists(temp_zip));
    }
}
