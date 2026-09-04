#include "test_framework.h"
#include "pathval/path_validate.h"

#include <string.h>

void test_path_validate(TestCounters *tc) {
    char out[256];

    /* Happy path */
    TEST_CHECK(tc, path_validate_join_entry("/mnt/usb0/etaHEN/games",
                                             "MyGame/eboot.bin", out,
                                             sizeof(out)) == PATH_OK);
    TEST_CHECK(tc, strcmp(out, "/mnt/usb0/etaHEN/games/MyGame/eboot.bin") == 0);

    /* dest_root with a trailing slash should not produce a double slash */
    TEST_CHECK(tc, path_validate_join_entry("/mnt/usb0/games/", "eboot.bin",
                                             out, sizeof(out)) == PATH_OK);
    TEST_CHECK(tc, strcmp(out, "/mnt/usb0/games/eboot.bin") == 0);

    /* Directory entries (trailing slash) are allowed */
    TEST_CHECK(tc, path_validate_join_entry("/mnt/usb0/games", "sce_sys/",
                                             out, sizeof(out)) == PATH_OK);
    TEST_CHECK(tc, strcmp(out, "/mnt/usb0/games/sce_sys/") == 0);

    /* Absolute entry path rejected */
    TEST_CHECK(tc, path_validate_join_entry("/mnt/usb0/games", "/etc/passwd",
                                             out, sizeof(out)) ==
                        PATH_ERR_NOT_ABSOLUTE);

    /* Simple traversal */
    TEST_CHECK(tc, path_validate_join_entry("/mnt/usb0/games", "../escape",
                                             out, sizeof(out)) ==
                        PATH_ERR_TRAVERSAL);

    /* Traversal buried in the middle of an otherwise normal-looking path */
    TEST_CHECK(tc, path_validate_join_entry(
                       "/mnt/usb0/games", "sce_sys/../../../etc/passwd", out,
                       sizeof(out)) == PATH_ERR_TRAVERSAL);

    /* Trailing traversal segment */
    TEST_CHECK(tc, path_validate_join_entry("/mnt/usb0/games", "a/b/..", out,
                                             sizeof(out)) == PATH_ERR_TRAVERSAL);

    /* "." segments rejected too (non-canonical, not itself dangerous, but a
     * sign the entry wasn't normalized) */
    TEST_CHECK(tc, path_validate_join_entry("/mnt/usb0/games", "./eboot.bin",
                                             out, sizeof(out)) ==
                        PATH_ERR_TRAVERSAL);

    /* Empty interior segment */
    TEST_CHECK(tc, path_validate_join_entry("/mnt/usb0/games", "a//b", out,
                                             sizeof(out)) ==
                        PATH_ERR_EMPTY_SEGMENT);

    /* Backslash rejected defensively even though we target POSIX */
    TEST_CHECK(tc, path_validate_join_entry("/mnt/usb0/games", "a\\b", out,
                                             sizeof(out)) == PATH_ERR_BACKSLASH);

    /* Windows-style drive-letter absolute path rejected via the backslash it
     * would need to be dangerous on a POSIX target; on its own "C:foo" has
     * no '/' or '\\' so it is actually a valid *relative* entry named
     * "C:foo" here — confirm that's what we get, since silently accepting
     * or silently mis-rejecting either would both be a bug. */
    TEST_CHECK(tc, path_validate_join_entry("/mnt/usb0/games", "C:foo", out,
                                             sizeof(out)) == PATH_OK);

    /* Empty entry */
    TEST_CHECK(tc, path_validate_join_entry("/mnt/usb0/games", "", out,
                                             sizeof(out)) == PATH_ERR_EMPTY);

    /* Non-absolute destination root rejected */
    TEST_CHECK(tc, path_validate_join_entry("relative/root", "eboot.bin", out,
                                             sizeof(out)) ==
                        PATH_ERR_NOT_ABSOLUTE);

    /* Output buffer too small */
    char tiny[4];
    TEST_CHECK(tc, path_validate_join_entry("/mnt/usb0/games", "eboot.bin",
                                             tiny, sizeof(tiny)) ==
                        PATH_ERR_TOO_LONG);

    /* path_validate_entry_is_safe mirrors the same rules without joining */
    TEST_CHECK(tc, path_validate_entry_is_safe("sce_sys/param.json") == true);
    TEST_CHECK(tc, path_validate_entry_is_safe("../../etc/passwd") == false);
    TEST_CHECK(tc, path_validate_entry_is_safe("/etc/passwd") == false);
    TEST_CHECK(tc, path_validate_entry_is_safe("") == false);
    TEST_CHECK(tc, path_validate_entry_is_safe(NULL) == false);
}
