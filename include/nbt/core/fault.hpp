#ifndef NBT_CORE_FAULT_HPP
#define NBT_CORE_FAULT_HPP

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace testing {
inline const char *env(const char *name) {
    return name ? getenv(name) : NULL;
}
inline void faultPoint(const char *name, const char *step) {
    const char *requested = env(name);
    if (requested && step && strcmp(requested, step) == 0) {
        _exit(91);
    }
}
} // namespace testing
} // namespace nbt

// Test hooks: compiled out of release builds, including the variable names.
#ifdef NBT_TESTING
#define NBT_FAULT_POINT(environment, step) nbt::testing::faultPoint(environment, step)
#define NBT_TEST_FLAG(environment) (nbt::testing::env(environment) != NULL)
#else
#define NBT_TEST_FLAG(environment) ((void)sizeof(environment), false)
#define NBT_FAULT_POINT(environment, step)                                                         \
    do {                                                                                           \
        (void)sizeof(environment);                                                                 \
        (void)sizeof(step);                                                                        \
    } while (0)
#endif

#endif
