#include "test_framework.h"
#include "romm_api/romm_api_mock.h"
#include "mockdata/mock_library.h"

#include <string.h>
#include <strings.h> /* strcasecmp, strcasestr */

void test_romm_api_mock(TestCounters *tc) {
    RommApi api;
    romm_api_mock_init(&api);

    TEST_CHECK(tc, api.test_connection(api.ctx) == ROMM_OK);

    /* No search term: full fixture set, sorted ascending by title */
    RommGamePage page;
    TEST_CHECK(tc, api.list_ps5_games(api.ctx, NULL, ROMM_SORT_TITLE_ASC, 0,
                                       100, &page) == ROMM_OK);
    TEST_CHECK(tc, page.total == MOCK_PS5_GAMES_COUNT);
    TEST_CHECK(tc, page.count == MOCK_PS5_GAMES_COUNT);
    for (size_t i = 1; i < page.count; i++) {
        TEST_CHECK(tc, strcasecmp(page.items[i - 1].title,
                                   page.items[i].title) <= 0);
    }
    api.list_ps5_games_free(&page);
    TEST_CHECK(tc, page.items == NULL && page.count == 0);

    /* Descending sort is the exact reverse of ascending */
    RommGamePage asc, desc;
    api.list_ps5_games(api.ctx, NULL, ROMM_SORT_TITLE_ASC, 0, 100, &asc);
    api.list_ps5_games(api.ctx, NULL, ROMM_SORT_TITLE_DESC, 0, 100, &desc);
    TEST_CHECK(tc, asc.count == desc.count);
    bool reversed_matches = true;
    for (size_t i = 0; i < asc.count; i++) {
        if (strcmp(asc.items[i].title,
                    desc.items[desc.count - 1 - i].title) != 0) {
            reversed_matches = false;
            break;
        }
    }
    TEST_CHECK(tc, reversed_matches);
    api.list_ps5_games_free(&asc);
    api.list_ps5_games_free(&desc);

    /* Case-insensitive substring search */
    RommGamePage search_page;
    api.list_ps5_games(api.ctx, "quest", ROMM_SORT_TITLE_ASC, 0, 100,
                        &search_page);
    TEST_CHECK(tc, search_page.total >= 1);
    for (size_t i = 0; i < search_page.count; i++) {
        TEST_CHECK(tc, strcasestr(search_page.items[i].title, "quest") != NULL);
    }
    api.list_ps5_games_free(&search_page);

    /* Search with no matches returns an empty (not error) result */
    RommGamePage no_match;
    TEST_CHECK(tc, api.list_ps5_games(api.ctx, "zzz-does-not-exist",
                                       ROMM_SORT_TITLE_ASC, 0, 100,
                                       &no_match) == ROMM_OK);
    TEST_CHECK(tc, no_match.total == 0 && no_match.count == 0);
    api.list_ps5_games_free(&no_match);

    /* Pagination: two pages of 3 together cover the same ground as one
     * page of 6, in the same order */
    RommGamePage page1, page2, combined;
    api.list_ps5_games(api.ctx, NULL, ROMM_SORT_TITLE_ASC, 0, 3, &page1);
    api.list_ps5_games(api.ctx, NULL, ROMM_SORT_TITLE_ASC, 3, 3, &page2);
    api.list_ps5_games(api.ctx, NULL, ROMM_SORT_TITLE_ASC, 0, 6, &combined);
    TEST_CHECK(tc, page1.count == 3 && page2.count >= 1);
    TEST_CHECK(tc, page1.total == combined.total);
    for (size_t i = 0; i < page1.count; i++) {
        TEST_CHECK(tc, page1.items[i].id == combined.items[i].id);
    }
    for (size_t i = 0; i < page2.count && 3 + i < combined.count; i++) {
        TEST_CHECK(tc, page2.items[i].id == combined.items[3 + i].id);
    }
    api.list_ps5_games_free(&page1);
    api.list_ps5_games_free(&page2);
    api.list_ps5_games_free(&combined);

    /* Offset past the end yields an empty page, not an error or crash */
    RommGamePage past_end;
    TEST_CHECK(tc, api.list_ps5_games(api.ctx, NULL, ROMM_SORT_TITLE_ASC,
                                       MOCK_PS5_GAMES_COUNT + 50, 10,
                                       &past_end) == ROMM_OK);
    TEST_CHECK(tc, past_end.count == 0);
    api.list_ps5_games_free(&past_end);
}
