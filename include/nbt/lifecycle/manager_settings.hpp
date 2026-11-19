// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LIFECYCLE_MANAGER_SETTINGS_HPP
#define NBT_LIFECYCLE_MANAGER_SETTINGS_HPP

#include "nbt/core/status.hpp"
#include "nbt/manager_settings.h"
#include "nbt/sdk/settings.hpp"
#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace lifecycle {

class ManagerSettings {
public:
    static core::Result<sdk::SettingsList> load(const storage::ManagedRoot &root);
    static core::Status set(const storage::ManagedRoot &root, const char *key, const char *value);
};

} // namespace lifecycle
} // namespace nbt

#endif
