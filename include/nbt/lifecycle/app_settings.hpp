#ifndef NBT_LIFECYCLE_APP_SETTINGS_HPP
#define NBT_LIFECYCLE_APP_SETTINGS_HPP

#include "nbt/app_settings.h"
#include "nbt/core/status.hpp"
#include "nbt/sdk/settings.hpp"
#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace lifecycle {

class AppSettingsStore {
public:
    static core::Result<sdk::SettingsList> load(const storage::ManagedRoot &root, const char *app);
    static core::Status saveSchema(const storage::ManagedRoot &root, const char *app,
                                   const sdk::SettingsList &schema);
    static core::Status set(const storage::ManagedRoot &root, const char *app, const char *key,
                            const char *value);
};

class AppEnvironment {
public:
    AppEnvironment();
    core::Status build(const storage::ManagedRoot &root, const char *app, const char *launch,
                       const nbt_bridge_requirement *bridge, const char *available);
    char *const *list() {
        return environment_.list;
    }
    AppEnvironment(const AppEnvironment &) = delete;
    AppEnvironment &operator=(const AppEnvironment &) = delete;

private:
    nbt_app_environment environment_;
};

} // namespace lifecycle
} // namespace nbt

#endif
