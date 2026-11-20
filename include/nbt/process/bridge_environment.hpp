// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_BRIDGE_ENVIRONMENT_HPP
#define NBT_PROCESS_BRIDGE_ENVIRONMENT_HPP
#include "nbt/app_settings.h"
namespace nbt {
namespace process {
/* NBT_BRIDGE and NBT_BRIDGE_CAPS, then for a user launch the descriptors
 * (NBT_STATUS_FD=5 ... NBT_HOLD_FD=9) and, by grant, NBT_HOLD_STATE (`hold`),
 * NBT_CDS_DIR (`car-data`) and NBT_NET (`net`, the signed rules). */
size_t bridgeEnvironment(nbt_app_environment *out, size_t count, const nbt_bridge_version &grant,
                         const char *launch, const nbt_fs *fs, const char *app,
                         const nbt_bridge_requirement *bridge);
/* The application's files folder (capability `files`), created when absent;
 * false when the files root cannot be used now. */
bool filesDir(const char *app, char *out, size_t size);
} // namespace process
} // namespace nbt
#endif
