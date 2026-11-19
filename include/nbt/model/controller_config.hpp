#ifndef NBT_MODEL_CONTROLLER_CONFIG_HPP
#define NBT_MODEL_CONTROLLER_CONFIG_HPP

#include "nbt/controller_config.h"
#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace model {

class ControllerConfig {
public:
    enum LoadStatus { LoadInvalid = -1, LoadApplied = 0, LoadAbsent = 1 };

    ControllerConfig();
    static core::Result<ControllerConfig> parse(core::Span<const char> text);
    core::Result<size_t> format(core::Span<char> output) const;
    static core::Result<ControllerConfig> load(const storage::ManagedRoot &root, bool *absent);
    static LoadStatus loadLegacy(nbt_fs *fs, nbt_ccs_config *config);
    const nbt_ccs_config &legacy() const {
        return config_;
    }
    nbt_ccs_config &legacy() {
        return config_;
    }

private:
    nbt_ccs_config config_;
};

} // namespace model
} // namespace nbt

#endif
