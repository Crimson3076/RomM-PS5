#include "test_framework.h"

#include <stdio.h>

void test_path_validate(TestCounters *tc);
void test_download_state(TestCounters *tc);
void test_log_redact(TestCounters *tc);
void test_romm_api_mock(TestCounters *tc);
void test_storage(TestCounters *tc);
void test_config(TestCounters *tc);
void test_log_file_sink(TestCounters *tc);
void test_url_encode(TestCounters *tc);
void test_config_credentials(TestCounters *tc);
void test_zip_extract(TestCounters *tc);
void test_romm_api_http(TestCounters *tc);
void test_downloader(TestCounters *tc);
void test_tilemap(TestCounters *tc);

typedef struct {
    const char *name;
    void (*fn)(TestCounters *tc);
} NamedTest;

int main(void) {
    NamedTest tests[] = {
        {"path_validate", test_path_validate},
        {"download_state", test_download_state},
        {"log_redact", test_log_redact},
        {"romm_api_mock", test_romm_api_mock},
        {"storage", test_storage},
        {"config", test_config},
        {"log_file_sink", test_log_file_sink},
        {"url_encode", test_url_encode},
        {"config_credentials", test_config_credentials},
        {"zip_extract", test_zip_extract},
        {"romm_api_http", test_romm_api_http},
        {"downloader", test_downloader},
        {"tilemap", test_tilemap},
    };
    size_t test_count = sizeof(tests) / sizeof(tests[0]);

    int total_run = 0;
    int total_failed = 0;

    for (size_t i = 0; i < test_count; i++) {
        TestCounters tc = {0, 0};
        tests[i].fn(&tc);
        printf("%-16s %3d checks, %d failed\n", tests[i].name, tc.run,
               tc.failed);
        total_run += tc.run;
        total_failed += tc.failed;
    }

    printf("----\n%d checks run, %d failed\n", total_run, total_failed);
    return total_failed == 0 ? 0 : 1;
}
