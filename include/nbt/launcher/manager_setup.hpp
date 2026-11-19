// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LAUNCHER_MANAGER_SETUP_HPP
#define NBT_LAUNCHER_MANAGER_SETUP_HPP

#include "nbt/core/status.hpp"
#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace launcher {

class ManagerSetup {
public:
    explicit ManagerSetup(const storage::ManagedRoot &root) : root_(root) {
    }

    core::Status setup(int releaseFd, int keyFd, const char *compat,
                       const unsigned char expectedHash[32], char *error, unsigned errorSize) const;

private:
    const storage::ManagedRoot &root_;
};

} // namespace launcher
} // namespace nbt

#endif
