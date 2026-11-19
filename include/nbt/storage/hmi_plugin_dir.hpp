// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_STORAGE_HMI_PLUGIN_DIR_HPP
#define NBT_STORAGE_HMI_PLUGIN_DIR_HPP

#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace storage {

class HmiPluginDir {
public:
    HmiPluginDir() : root_() {
    }
    core::Status initialize(const char *root);
    core::Status requireSpace(uint64_t bytes) const;
    core::Result<core::UniqueFd> open(bool temporary, unsigned index, bool create) const;
    core::Status prepare();
    core::Status discard(unsigned index);
    core::Status publish();
    core::Status sync(bool temporary);

private:
    nbt_hmi_fs root_;
};

} // namespace storage
} // namespace nbt

#endif
