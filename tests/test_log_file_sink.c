#include "test_framework.h"
#include "log/log.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Counts non-overlapping occurrences of `needle` in `haystack`. Used to
 * assert a message reached the file sink exactly once, not zero or twice. */
static int count_occurrences(const char *haystack, const char *needle) {
    int count = 0;
    const char *p = haystack;
    size_t needle_len = strlen(needle);
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += needle_len;
    }
    return count;
}

static char *read_whole_file(const char *path, char *buf, size_t buf_size) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        buf[0] = '\0';
        return buf;
    }
    size_t n = fread(buf, 1, buf_size - 1, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

void test_log_file_sink(TestCounters *tc) {
    char path[64];
    snprintf(path, sizeof(path), "/tmp/rommps5_test_log_sink_%d.log", getpid());
    remove(path);

    /* --- INFO/WARN/ERROR all reach the persistent file --- */
    TEST_CHECK(tc, log_init_file_sink(path) == true);

    log_info("test info message %d", 1);
    log_warn("test warn message");
    log_error("test error message");

    /* Read the file while the sink is still open (not yet closed), to
     * verify log_message() flushes per-line rather than buffering until
     * log_close_file_sink() — a payload that crashes mid-run must not
     * lose earlier lines. */
    char contents[4096];
    read_whole_file(path, contents, sizeof(contents));

    TEST_CHECK(tc, strstr(contents, "INFO") != NULL);
    TEST_CHECK(tc, strstr(contents, "test info message 1") != NULL);
    TEST_CHECK(tc, strstr(contents, "WARN") != NULL);
    TEST_CHECK(tc, strstr(contents, "test warn message") != NULL);
    TEST_CHECK(tc, strstr(contents, "ERROR") != NULL);
    TEST_CHECK(tc, strstr(contents, "test error message") != NULL);

    /* Timestamps are preserved in the file the same way as stderr: every
     * line starts with a bracketed ISO-8601 UTC timestamp, e.g.
     * "[2026-09-04T18:31:48Z] INFO  ...". */
    TEST_CHECK(tc, contents[0] == '[');
    TEST_CHECK(tc, strstr(contents, "Z] INFO") != NULL);

    /* --- No duplication: each message appears exactly once --- */
    TEST_CHECK(tc, count_occurrences(contents, "test info message 1") == 1);
    TEST_CHECK(tc, count_occurrences(contents, "test warn message") == 1);
    TEST_CHECK(tc, count_occurrences(contents, "test error message") == 1);

    /* --- Messages from a storage-discovery-shaped log line reach the file ---
     */
    log_info("storage_discover: 1 of 18 candidate destinations exist and "
             "are writable");
    log_info("  destination: /data/etaHEN/games (515746824192 bytes free)");
    read_whole_file(path, contents, sizeof(contents));
    TEST_CHECK(tc, strstr(contents, "storage_discover: 1 of 18") != NULL);
    TEST_CHECK(tc, strstr(contents, "/data/etaHEN/games") != NULL);

    /* --- Warnings from a failing-service-shaped log line reach the file --- */
    log_warn("scePadOpen skipped: no SDK-verified way to obtain a real "
             "user id was found");
    read_whole_file(path, contents, sizeof(contents));
    TEST_CHECK(tc, strstr(contents, "scePadOpen skipped") != NULL);

    log_close_file_sink();

    /* Content survives after close too (flush + close both happened). */
    read_whole_file(path, contents, sizeof(contents));
    TEST_CHECK(tc, strstr(contents, "scePadOpen skipped") != NULL);

    remove(path);

    /* --- A file sink that can't be opened is nonfatal --- */
    TEST_CHECK(tc, log_init_file_sink("/nonexistent_dir_xyz/should_fail.log") ==
                       false);
    /* Must not crash, and normal logging must keep working (stderr-only). */
    log_info("still alive after a failed sink open");
    log_error("still alive after a failed sink open (error level)");

    /* --- Re-opening a sink after a failed attempt works normally --- */
    char path2[64];
    snprintf(path2, sizeof(path2), "/tmp/rommps5_test_log_sink2_%d.log",
             getpid());
    remove(path2);
    TEST_CHECK(tc, log_init_file_sink(path2) == true);
    log_info("second sink message");
    log_close_file_sink();
    read_whole_file(path2, contents, sizeof(contents));
    TEST_CHECK(tc, strstr(contents, "second sink message") != NULL);
    /* The failed sink's messages must not have leaked into this file. */
    TEST_CHECK(tc, strstr(contents, "still alive") == NULL);
    remove(path2);

    /* log_close_file_sink() with nothing open must be a safe no-op. */
    log_close_file_sink();
    log_close_file_sink();
}
