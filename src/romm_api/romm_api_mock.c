#include "romm_api/romm_api_mock.h"
#include "mockdata/mock_library.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp (POSIX/BSD; available on both host and PS5 libc) */

/* Case-insensitive substring search, ASCII only — matches the fixed ASCII
 * mock title set. A real backend will let the RomM server do this instead. */
static bool title_contains_ci(const char *title, const char *needle) {
    size_t title_len = strlen(title);
    size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        return true;
    }
    if (needle_len > title_len) {
        return false;
    }
    for (size_t i = 0; i + needle_len <= title_len; i++) {
        size_t j = 0;
        for (; j < needle_len; j++) {
            if (tolower((unsigned char)title[i + j]) !=
                tolower((unsigned char)needle[j])) {
                break;
            }
        }
        if (j == needle_len) {
            return true;
        }
    }
    return false;
}

static int compare_title_asc(const void *a, const void *b) {
    const RommGame *ga = *(const RommGame *const *)a;
    const RommGame *gb = *(const RommGame *const *)b;
    return strcasecmp(ga->title, gb->title);
}

static int compare_title_desc(const void *a, const void *b) {
    return -compare_title_asc(a, b);
}

static RommResult mock_test_connection(void *ctx) {
    (void)ctx;
    return ROMM_OK;
}

static RommResult mock_list_ps5_games(void *ctx, const char *search_term,
                                       RommSortOrder sort, size_t offset,
                                       size_t limit, RommGamePage *page_out) {
    (void)ctx;
    if (page_out == NULL || limit == 0) {
        return ROMM_ERR_UNSUPPORTED;
    }

    const RommGame *pool = MOCK_PS5_GAMES;
    size_t pool_count = MOCK_PS5_GAMES_COUNT;

    /* Filter into a scratch array of pointers first so sort/paginate never
     * touches the read-only fixture data. */
    const RommGame **matches = malloc(sizeof(RommGame *) * pool_count);
    if (matches == NULL) {
        return ROMM_ERR_UNSUPPORTED;
    }
    size_t match_count = 0;
    for (size_t i = 0; i < pool_count; i++) {
        if (search_term == NULL || search_term[0] == '\0' ||
            title_contains_ci(pool[i].title, search_term)) {
            matches[match_count++] = &pool[i];
        }
    }

    qsort(matches, match_count, sizeof(RommGame *),
          sort == ROMM_SORT_TITLE_DESC ? compare_title_desc
                                        : compare_title_asc);

    size_t start = offset < match_count ? offset : match_count;
    size_t end = start + limit < match_count ? start + limit : match_count;
    size_t page_count = end - start;

    RommGame *page_items = NULL;
    if (page_count > 0) {
        page_items = malloc(sizeof(RommGame) * page_count);
        if (page_items == NULL) {
            free(matches);
            return ROMM_ERR_UNSUPPORTED;
        }
        for (size_t i = 0; i < page_count; i++) {
            page_items[i] = *matches[start + i];
        }
    }

    free(matches);

    page_out->items = page_items;
    page_out->count = page_count;
    page_out->total = match_count;
    page_out->offset = offset;
    return ROMM_OK;
}

static void mock_list_ps5_games_free(RommGamePage *page) {
    if (page == NULL) {
        return;
    }
    free(page->items);
    page->items = NULL;
    page->count = 0;
    page->total = 0;
    page->offset = 0;
}

void romm_api_mock_init(RommApi *out) {
    out->ctx = NULL;
    out->test_connection = mock_test_connection;
    out->list_ps5_games = mock_list_ps5_games;
    out->list_ps5_games_free = mock_list_ps5_games_free;
}
