#include "test_framework.h"
#include "net/url_encode.h"

#include <string.h>

void test_url_encode(TestCounters *tc) {
    char out[128];

    url_encode("Super Game: Deluxe Edition!", out, sizeof(out));
    TEST_CHECK(tc, strcmp(out, "Super%20Game%3A%20Deluxe%20Edition%21") == 0);

    url_encode("already-safe_chars.123~", out, sizeof(out));
    TEST_CHECK(tc, strcmp(out, "already-safe_chars.123~") == 0);

    url_encode("", out, sizeof(out));
    TEST_CHECK(tc, strcmp(out, "") == 0);

    /* Truncates rather than overflowing: a too-small buffer never gets a
     * partially-escaped %XX split across the boundary. */
    char tiny[4];
    url_encode("hello world", tiny, sizeof(tiny));
    TEST_CHECK(tc, strlen(tiny) < sizeof(tiny));

    /* Zero-capacity output is a documented no-op, not a crash. */
    char zero_cap[1] = {'X'};
    url_encode("anything", zero_cap, 0);
    TEST_CHECK(tc, zero_cap[0] == 'X');
}
