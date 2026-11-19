// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LIFECYCLE_APP_PROVISIONER_HPP
#define NBT_LIFECYCLE_APP_PROVISIONER_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"

#include <stdint.h>

namespace nbt {
namespace lifecycle {

class AppProvisioner {
public:
    static core::Status provision(const char *root, int input, const char *app, const char *compat,
                                  uint64_t keySize, uint64_t packageSize,
                                  const unsigned char digest[32],
                                  core::Span<char> errorOut = core::Span<char>());
};

} // namespace lifecycle
} // namespace nbt

#endif
