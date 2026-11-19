#ifndef NBT_CORE_CHECK_HPP
#define NBT_CORE_CHECK_HPP

namespace nbt {
namespace core {
#if defined(__GNUC__)
__attribute__((noreturn))
#endif
void invariantFailed(const char *file, int line);
} // namespace core
} // namespace nbt

#define NBT_CHECK(condition)                                                                       \
    do {                                                                                           \
        if (!(condition))                                                                          \
            nbt::core::invariantFailed(__FILE__, __LINE__);                                        \
    } while (0)

#endif
