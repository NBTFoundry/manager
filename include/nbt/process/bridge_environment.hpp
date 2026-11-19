// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_BRIDGE_ENVIRONMENT_HPP
#define NBT_PROCESS_BRIDGE_ENVIRONMENT_HPP
#include "nbt/app_settings.h"
namespace nbt {
namespace process {
size_t bridgeEnvironment(nbt_app_environment *out, size_t count, const nbt_bridge_version &grant,
                         const char *launch);
}
} // namespace nbt
#endif
