#ifndef NBT_LAUNCHER_BOOTSTRAP_HPP
#define NBT_LAUNCHER_BOOTSTRAP_HPP

#include "nbt/core/status.hpp"

#include <stdint.h>

namespace nbt {
namespace launcher {

class Bootstrap {
public:
    core::Status run(const char *root, int input, uint64_t keySize, uint64_t releaseSize,
                     const char *compat, const unsigned char digest[32], char *error,
                     unsigned errorSize) const;
};

} // namespace launcher
} // namespace nbt

#endif
