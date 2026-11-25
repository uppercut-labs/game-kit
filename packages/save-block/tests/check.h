/* A tiny assert for the save-block tests: prints the failing line and makes main return 1. */
#ifndef SB_TEST_CHECK_H
#define SB_TEST_CHECK_H

#include <stdio.h>

static int sb_test_failures;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
            sb_test_failures++; \
        } \
    } while (0)

#define TEST_RESULT(name) \
    (sb_test_failures ? (fprintf(stderr, "%s: %d failure(s)\n", (name), sb_test_failures), 1) \
                       : (printf("%s: passed\n", (name)), 0))

#endif
