// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LAUNCHER_SUPERVISOR_HPP
#define NBT_LAUNCHER_SUPERVISOR_HPP

#include "nbt/core/status.hpp"
#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace launcher {

class Supervisor {
public:
    core::Status run(storage::ManagedRoot &root);
};

} // namespace launcher
} // namespace nbt

#endif
