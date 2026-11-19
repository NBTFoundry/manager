#ifndef NBT_TESTS_SUPPORT_CHECK_HPP
#define NBT_TESTS_SUPPORT_CHECK_HPP

#include <stdio.h>
#include <stdlib.h>

#define TEST_CHECK(condition)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                   \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

#define TEST_RUN(function)                                                                         \
    do {                                                                                           \
        if (!(function)())                                                                         \
            return 1;                                                                              \
    } while (0)

#endif
