#include "test_framework.h"

#include "ps5/tilemap.h"

#include <stdint.h>

void test_tilemap(TestCounters *tc) {
    TEST_CHECK(tc, tilemap_buffer_size(1920, 1080) == 0x880000u);
    TEST_CHECK(tc, tilemap_buffer_size(1280, 720) == 0x3e0000u);
    TEST_CHECK(tc, tilemap_buffer_size(0, 1080) == 0);
    TEST_CHECK(tc, tilemap_buffer_size(1920, 0) == 0);
    TEST_CHECK(tc, tilemap_buffer_size(UINT32_MAX, UINT32_MAX) == 0);

    size_t stride = 0;
    size_t total = 0;
    TEST_CHECK(tc, tilemap_buffer_layout(1920, 1080, 0x20000, 2, &stride,
                                         &total));
    TEST_CHECK(tc, stride == 0x880000u);
    TEST_CHECK(tc, total == 0x1100000u);

    TEST_CHECK(tc, tilemap_buffer_layout(1280, 720, 0x20000, 2, &stride,
                                         &total));
    TEST_CHECK(tc, stride == 0x3e0000u);
    TEST_CHECK(tc, total == 0x7c0000u);

    TEST_CHECK(tc,
               !tilemap_buffer_layout(1920, 1080, 0, 2, &stride, &total));
    TEST_CHECK(tc, !tilemap_buffer_layout(1920, 1080, 0x20000, 0, &stride,
                                          &total));
    TEST_CHECK(tc,
               !tilemap_buffer_layout(1920, 1080, 0x20000, 2, NULL, &total));
    TEST_CHECK(tc,
               !tilemap_buffer_layout(1920, 1080, 0x20000, 2, &stride, NULL));
    TEST_CHECK(tc, !tilemap_buffer_layout(1920, 1080, SIZE_MAX, 2, &stride,
                                          &total));

    TEST_CHECK(tc, tilemap_buffer_layout(1, 1, 0x20000, 2, &stride, &total));
    TEST_CHECK(tc, stride == 0x40000u);
    TEST_CHECK(tc, total == 0x80000u);
}
