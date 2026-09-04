#include "test_framework.h"
#include "synthetic_zip.h"
#include "download/zip_extract.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *write_temp_zip(const uint8_t *data, size_t len) {
    static char path[128];
    snprintf(path, sizeof(path), "/tmp/rommps5_test_zip_%d.zip", getpid());
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return NULL;
    }
    fwrite(data, 1, len, f);
    fclose(f);
    return path;
}

static bool read_file(const char *path, char *out, size_t out_capacity) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    size_t n = fread(out, 1, out_capacity - 1, f);
    out[n] = '\0';
    fclose(f);
    return true;
}

void test_zip_extract(TestCounters *tc) {
    /* Use a unique directory instead of getpid(). Sandboxed test runners
     * can reuse a PID while preserving /tmp between invocations, which
     * made a second run falsely report the prior output as duplicate ZIP
     * entries. */
    char dest_root[] = "/tmp/rommps5_test_zip_dest_XXXXXX";
    if (mkdtemp(dest_root) == NULL) {
        TEST_CHECK(tc, false);
        return;
    }

    /* --- Happy path: two files, one directory entry --- */
    {
        const char *content_a = "hello world";
        const char *content_b = "second file contents here";
        SyntheticZipEntry entries[] = {
            {.name = "sce_sys/",
             .content = NULL,
             .content_len = 0,
             .method = 0,
             .gp_flag = 0},
            {.name = "sce_sys/param.json",
             .content = (const uint8_t *)content_a,
             .content_len = strlen(content_a),
             .method = 0,
             .gp_flag = 0},
            {.name = "eboot.bin",
             .content = (const uint8_t *)content_b,
             .content_len = strlen(content_b),
             .method = 0,
             .gp_flag = 0},
        };

        uint8_t buf[4096];
        size_t len = build_synthetic_zip(buf, sizeof(buf), entries, 3);
        TEST_CHECK(tc, len > 0);

        char *zip_path = write_temp_zip(buf, len);
        TEST_CHECK(tc, zip_path != NULL);

        uint32_t extracted = 0;
        ZipExtractResult result =
            zip_extract_stored(zip_path, dest_root, &extracted);
        TEST_CHECK(tc, result == ZIP_EXTRACT_OK);
        TEST_CHECK(tc, extracted == 3);

        char param_path[128];
        snprintf(param_path, sizeof(param_path), "%s/sce_sys/param.json",
                  dest_root);
        char read_buf[64];
        TEST_CHECK(tc, read_file(param_path, read_buf, sizeof(read_buf)));
        TEST_CHECK(tc, strcmp(read_buf, content_a) == 0);

        char eboot_path[128];
        snprintf(eboot_path, sizeof(eboot_path), "%s/eboot.bin", dest_root);
        TEST_CHECK(tc, read_file(eboot_path, read_buf, sizeof(read_buf)));
        TEST_CHECK(tc, strcmp(read_buf, content_b) == 0);

        struct stat st;
        char dir_path[128];
        snprintf(dir_path, sizeof(dir_path), "%s/sce_sys", dest_root);
        TEST_CHECK(tc, stat(dir_path, &st) == 0 && S_ISDIR(st.st_mode));

        remove(zip_path);
    }

    /* --- Rejects path traversal --- */
    {
        const uint8_t content[] = "x";
        SyntheticZipEntry entries[] = {
            {.name = "../../etc/passwd",
             .content = content,
             .content_len = 1,
             .method = 0,
             .gp_flag = 0},
        };
        uint8_t buf[512];
        size_t len = build_synthetic_zip(buf, sizeof(buf), entries, 1);
        char *zip_path = write_temp_zip(buf, len);

        char dest2[80];
        snprintf(dest2, sizeof(dest2), "%s_traversal", dest_root);
        ZipExtractResult result = zip_extract_stored(zip_path, dest2, NULL);
        TEST_CHECK(tc, result == ZIP_EXTRACT_ERR_UNSAFE_PATH);

        remove(zip_path);
    }

    /* --- Rejects a duplicate archive path --- */
    {
        const uint8_t content[] = "dup";
        SyntheticZipEntry entries[] = {
            {.name = "same.bin",
             .content = content,
             .content_len = 3,
             .method = 0,
             .gp_flag = 0},
            {.name = "same.bin",
             .content = content,
             .content_len = 3,
             .method = 0,
             .gp_flag = 0},
        };
        uint8_t buf[512];
        size_t len = build_synthetic_zip(buf, sizeof(buf), entries, 2);
        char *zip_path = write_temp_zip(buf, len);

        char dest3[80];
        snprintf(dest3, sizeof(dest3), "%s_dup", dest_root);
        ZipExtractResult result = zip_extract_stored(zip_path, dest3, NULL);
        TEST_CHECK(tc, result == ZIP_EXTRACT_ERR_DUPLICATE_PATH);

        remove(zip_path);
    }

    /* --- Rejects a compressed (non-stored) entry --- */
    {
        const uint8_t content[] = "compressed-looking";
        SyntheticZipEntry entries[] = {
            {.name = "deflated.bin",
             .content = content,
             .content_len = sizeof(content) - 1,
             .method = 8, /* DEFLATE */
             .gp_flag = 0},
        };
        uint8_t buf[512];
        size_t len = build_synthetic_zip(buf, sizeof(buf), entries, 1);
        char *zip_path = write_temp_zip(buf, len);

        char dest4[80];
        snprintf(dest4, sizeof(dest4), "%s_method", dest_root);
        ZipExtractResult result = zip_extract_stored(zip_path, dest4, NULL);
        TEST_CHECK(tc, result == ZIP_EXTRACT_ERR_UNSUPPORTED_ENTRY);

        remove(zip_path);
    }

    /* --- Missing file --- */
    {
        ZipExtractResult result =
            zip_extract_stored("/tmp/rommps5_does_not_exist.zip",
                                "/tmp/rommps5_test_zip_missing", NULL);
        TEST_CHECK(tc, result == ZIP_EXTRACT_ERR_IO);
    }
}
