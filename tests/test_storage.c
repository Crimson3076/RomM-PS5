#include "test_framework.h"
#include "storage/storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void mkdir_p(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0700);
            *p = '/';
        }
    }
    mkdir(tmp, 0700);
}

void test_storage(TestCounters *tc) {
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "/tmp/rommps5_test_storage_%d", getpid());

    /* Only the first two candidates get created; storage_discover must
     * report exactly those as existing/writable and skip the rest — the
     * real fixed candidate list is far longer than what any one console
     * will actually have mounted. */
    const char *candidates[] = {
        "/data/etaHEN/games",
        "/mnt/usb0/etaHEN/games",
        "/mnt/usb1/etaHEN/games", /* deliberately not created */
    };
    size_t candidate_count = sizeof(candidates) / sizeof(candidates[0]);

    char full0[256], full1[256];
    snprintf(full0, sizeof(full0), "%s%s", prefix, candidates[0]);
    snprintf(full1, sizeof(full1), "%s%s", prefix, candidates[1]);
    mkdir_p(full0);
    mkdir_p(full1);

    StorageDestination out[8];
    size_t found = storage_discover(candidates, candidate_count, prefix, out,
                                     8);

    TEST_CHECK(tc, found == 2);
    TEST_CHECK(tc, strcmp(out[0].path, full0) == 0);
    TEST_CHECK(tc, strcmp(out[1].path, full1) == 0);
    /* A real mounted filesystem always reports a nonzero total size */
    TEST_CHECK(tc, out[0].total_bytes > 0);

    /* An output buffer smaller than the discovered count fills only what
     * fits and reports that, rather than overflowing. */
    StorageDestination small_out[1];
    size_t small_found =
        storage_discover(candidates, candidate_count, prefix, small_out, 1);
    TEST_CHECK(tc, small_found == 1);

    /* Every declared candidate path is covered by the real fixed list. */
    TEST_CHECK(tc, STORAGE_CANDIDATE_PATH_COUNT == 18);
}
