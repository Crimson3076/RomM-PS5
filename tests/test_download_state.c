#include "test_framework.h"
#include "download/download_manager.h"

#include <string.h>

void test_download_state(TestCounters *tc) {
    DownloadProgress p;
    download_progress_init(&p);
    TEST_CHECK(tc, p.state == DL_STATE_IDLE);
    TEST_CHECK(tc, p.bytes_transferred == 0);
    TEST_CHECK(tc, p.bytes_total == 0);

    /* Happy path: IDLE -> DOWNLOADING -> EXTRACTING -> VALIDATING -> COMPLETED */
    TEST_CHECK(tc, download_progress_transition(&p, DL_STATE_DOWNLOADING));
    TEST_CHECK(tc, p.state == DL_STATE_DOWNLOADING);
    TEST_CHECK(tc, download_progress_update_bytes(&p, 1024, 4096));
    TEST_CHECK(tc, p.bytes_transferred == 1024 && p.bytes_total == 4096);
    TEST_CHECK(tc, download_progress_transition(&p, DL_STATE_EXTRACTING));
    TEST_CHECK(tc, download_progress_transition(&p, DL_STATE_VALIDATING));
    TEST_CHECK(tc, download_progress_transition(&p, DL_STATE_COMPLETED));
    TEST_CHECK(tc, p.state == DL_STATE_COMPLETED);

    /* COMPLETED -> IDLE is the only legal exit, and resets counters */
    TEST_CHECK(tc, !download_progress_transition(&p, DL_STATE_DOWNLOADING));
    TEST_CHECK(tc, download_progress_transition(&p, DL_STATE_IDLE));
    TEST_CHECK(tc, p.bytes_transferred == 0 && p.bytes_total == 0);

    /* Single-file download may skip EXTRACTING */
    TEST_CHECK(tc, download_progress_transition(&p, DL_STATE_DOWNLOADING));
    TEST_CHECK(tc, download_progress_transition(&p, DL_STATE_VALIDATING));
    TEST_CHECK(tc, download_progress_transition(&p, DL_STATE_COMPLETED));

    /* Illegal transitions are rejected and leave state untouched */
    download_progress_init(&p);
    TEST_CHECK(tc, !download_progress_transition(&p, DL_STATE_EXTRACTING));
    TEST_CHECK(tc, !download_progress_transition(&p, DL_STATE_COMPLETED));
    TEST_CHECK(tc, p.state == DL_STATE_IDLE);

    /* Progress updates are rejected outside DOWNLOADING (e.g. a stray
     * callback firing after cancel must not silently move the counters) */
    TEST_CHECK(tc, !download_progress_update_bytes(&p, 1, 2));

    /* Cancel from DOWNLOADING, then retry */
    TEST_CHECK(tc, download_progress_transition(&p, DL_STATE_DOWNLOADING));
    TEST_CHECK(tc, download_progress_update_bytes(&p, 500, 1000));
    TEST_CHECK(tc, download_progress_transition(&p, DL_STATE_CANCELLED));
    TEST_CHECK(tc, p.state == DL_STATE_CANCELLED);
    TEST_CHECK(tc, download_progress_transition(&p, DL_STATE_DOWNLOADING));
    /* Retrying must not carry over the cancelled attempt's byte counts */
    TEST_CHECK(tc, p.bytes_transferred == 0 && p.bytes_total == 0);

    /* Network interruption mid-download: fail with a reason, then retry */
    TEST_CHECK(tc, download_progress_update_bytes(&p, 200, 1000));
    TEST_CHECK(tc, download_progress_fail(&p, "connection reset"));
    TEST_CHECK(tc, p.state == DL_STATE_FAILED);
    TEST_CHECK(tc, strcmp(p.failure_reason, "connection reset") == 0);
    TEST_CHECK(tc, download_progress_transition(&p, DL_STATE_DOWNLOADING));
    TEST_CHECK(tc, p.failure_reason[0] == '\0');

    /* A too-long failure reason is truncated, not overflowed */
    download_progress_init(&p);
    download_progress_transition(&p, DL_STATE_DOWNLOADING);
    char long_reason[512];
    memset(long_reason, 'x', sizeof(long_reason) - 1);
    long_reason[sizeof(long_reason) - 1] = '\0';
    TEST_CHECK(tc, download_progress_fail(&p, long_reason));
    TEST_CHECK(tc, strlen(p.failure_reason) == sizeof(p.failure_reason) - 1);

    /* fail() from a state where FAILED isn't reachable (e.g. already
     * COMPLETED) is rejected, matching "a failed/cancelled download must
     * never appear as a complete game" — the reverse must hold too. */
    download_progress_init(&p);
    download_progress_transition(&p, DL_STATE_DOWNLOADING);
    download_progress_transition(&p, DL_STATE_VALIDATING);
    download_progress_transition(&p, DL_STATE_COMPLETED);
    TEST_CHECK(tc, !download_progress_fail(&p, "too late"));
    TEST_CHECK(tc, p.state == DL_STATE_COMPLETED);
}
