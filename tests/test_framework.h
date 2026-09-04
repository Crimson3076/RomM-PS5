#ifndef ROMM_PS5_TEST_FRAMEWORK_H
#define ROMM_PS5_TEST_FRAMEWORK_H

#include <stdio.h>

typedef struct {
    int run;
    int failed;
} TestCounters;

#define TEST_CHECK(counters, cond)                                           \
    do {                                                                     \
        (counters)->run++;                                                   \
        if (!(cond)) {                                                       \
            (counters)->failed++;                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

#endif /* ROMM_PS5_TEST_FRAMEWORK_H */
