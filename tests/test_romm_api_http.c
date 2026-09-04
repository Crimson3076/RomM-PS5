#include "test_framework.h"
#include "mock_http_client.h"
#include "romm_api/romm_api_http.h"

#include <string.h>

static const char PLATFORMS_JSON[] =
    "[{\"id\": 1, \"fs_slug\": \"ps4\"},"
    " {\"id\": 3, \"fs_slug\": \"ps5\"}]";

static const char ROMS_JSON[] =
    "{\"items\": ["
    "  {\"id\": 101, \"name\": \"Test Game\", \"fs_name\": \"Test "
    "Game.ffpkg\", \"fs_extension\": \"ffpkg\", \"fs_size_bytes\": 123456},"
    "  {\"id\": 102, \"name\": \"\", \"fs_name\": \"Folder Game\", "
    "\"fs_extension\": \"\", \"fs_size_bytes\": 999}"
    "], \"total\": 2}";

void test_romm_api_http(TestCounters *tc) {
    /* --- test_connection resolves and caches the ps5 platform id --- */
    {
        MockHttpClient mock;
        HttpClient http;
        mock_http_client_init(&mock, &http);
        mock.status_code = 200;
        mock.body = (const uint8_t *)PLATFORMS_JSON;
        mock.body_len = strlen(PLATFORMS_JSON);

        RommApiHttpContext ctx;
        RommApi api;
        romm_api_http_init(&api, &ctx, &http, "http://romm.example.com",
                            "rmm_testtoken");

        TEST_CHECK(tc, api.test_connection(api.ctx) == ROMM_OK);
        TEST_CHECK(tc, mock.request_count == 1);
        TEST_CHECK(tc, strstr(mock.last_url, "/api/platforms") != NULL);
        TEST_CHECK(tc,
                    strcmp(mock.last_authorization, "Bearer rmm_testtoken") ==
                        0);

        /* Calling again must not re-issue the platforms request. */
        TEST_CHECK(tc, api.test_connection(api.ctx) == ROMM_OK);
        TEST_CHECK(tc, mock.request_count == 1);
    }

    /* --- list_ps5_games resolves the platform then lists roms,
     * parsing titles/format/size correctly, including the fs_name
     * fallback-to-title and empty-extension-means-folder rules --- */
    {
        MockHttpClient mock;
        HttpClient http;
        mock_http_client_init(&mock, &http);
        mock_http_client_add_route(&mock, "/api/platforms", 200,
                                     (const uint8_t *)PLATFORMS_JSON,
                                     strlen(PLATFORMS_JSON));
        mock_http_client_add_route(&mock, "/api/roms", 200,
                                     (const uint8_t *)ROMS_JSON,
                                     strlen(ROMS_JSON));

        RommApiHttpContext ctx;
        RommApi api;
        romm_api_http_init(&api, &ctx, &http, "http://romm.example.com",
                            "rmm_testtoken");

        RommGamePage page;
        memset(&page, 0, sizeof(page));
        RommResult result = api.list_ps5_games(api.ctx, NULL,
                                                 ROMM_SORT_TITLE_ASC, 0, 50,
                                                 &page);
        TEST_CHECK(tc, result == ROMM_OK);
        TEST_CHECK(tc, page.count == 2);
        TEST_CHECK(tc, page.total == 2);
        TEST_CHECK(tc, strstr(mock.last_url, "platform_ids=3") != NULL);

        TEST_CHECK(tc, page.items[0].id == 101);
        TEST_CHECK(tc, strcmp(page.items[0].title, "Test Game") == 0);
        TEST_CHECK(tc, strcmp(page.items[0].fs_name, "Test Game.ffpkg") == 0);
        TEST_CHECK(tc, page.items[0].format == ROMM_FORMAT_FFPKG);
        TEST_CHECK(tc, page.items[0].fs_size_bytes == 123456);

        /* Empty "name" falls back to fs_name; empty extension means a
         * folder-format game (per docs/architecture.md). */
        TEST_CHECK(tc, strcmp(page.items[1].title, "Folder Game") == 0);
        TEST_CHECK(tc, page.items[1].format == ROMM_FORMAT_FOLDER);

        api.list_ps5_games_free(&page);
        TEST_CHECK(tc, page.items == NULL);
    }

    /* --- Auth failure maps to ROMM_ERR_AUTH --- */
    {
        MockHttpClient mock;
        HttpClient http;
        mock_http_client_init(&mock, &http);
        mock.status_code = 401;

        RommApiHttpContext ctx;
        RommApi api;
        romm_api_http_init(&api, &ctx, &http, "http://romm.example.com",
                            "rmm_badtoken");

        TEST_CHECK(tc, api.test_connection(api.ctx) == ROMM_ERR_AUTH);
    }

    /* --- No 'ps5' platform on the server --- */
    {
        static const char no_ps5[] = "[{\"id\": 1, \"fs_slug\": \"ps4\"}]";
        MockHttpClient mock;
        HttpClient http;
        mock_http_client_init(&mock, &http);
        mock.status_code = 200;
        mock.body = (const uint8_t *)no_ps5;
        mock.body_len = strlen(no_ps5);

        RommApiHttpContext ctx;
        RommApi api;
        romm_api_http_init(&api, &ctx, &http, "http://romm.example.com",
                            "rmm_testtoken");

        TEST_CHECK(tc, api.test_connection(api.ctx) == ROMM_ERR_NOT_FOUND);
    }
}
