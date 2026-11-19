// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LAUNCHER_STAGER_HPP
#define NBT_LAUNCHER_STAGER_HPP

#include "nbt/core/status.hpp"
#include "nbt/manager_stage.h"
#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace launcher {

class Stager {
public:
    explicit Stager(const storage::ManagedRoot &root) : root_(root) {
    }

    core::Status stage(bool initial, bool recovery, const unsigned char expectedHash[32],
                       char *error, unsigned errorSize) const;
    core::Status activeBridge(nbt_bridge_provider &provider) const;
    core::Status verifySlot(const nbt_manager_slot &slot) const;
    core::Status verifyRelease(int fd, const unsigned char expectedHash[32]) const;

private:
    const storage::ManagedRoot &root_;
};

} // namespace launcher
} // namespace nbt

#endif
