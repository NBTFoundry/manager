// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_CORE_CRASH_HPP
#define NBT_CORE_CRASH_HPP

#include "nbt/core/status.hpp"

namespace nbt {
namespace core {

struct CrashConfig {
    const char *component;
    const char *markerPath;
    const char *debugPath;
};

Status installCrashHandlers(const CrashConfig &config);

} // namespace core
} // namespace nbt

#endif
