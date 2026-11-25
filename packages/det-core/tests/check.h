/* A tiny assert for the det-core tests: prints the failing line and makes main return 1. */
#ifndef DET_TEST_CHECK_H
#define DET_TEST_CHECK_H

#include <stdio.h>

static int det_test_failures;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
            det_test_failures++; \
        } \
    } while (0)

#define TEST_RESULT(name) \
    (det_test_failures ? (fprintf(stderr, "%s: %d failure(s)\n", (name), det_test_failures), 1) \
                       : (printf("%s: passed\n", (name)), 0))

#endif
