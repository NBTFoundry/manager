// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LAUNCHER_SLOT_SELECT_HPP
#define NBT_LAUNCHER_SLOT_SELECT_HPP

#include "nbt/core/status.hpp"
#include "nbt/launcher/slot_store.hpp"
#include "nbt/manager_state.h"
#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace launcher {

/* The slot to run, at an opening and after staging a manager update: a
 * pending candidate on trial (`trial`), a broken or interrupted one rolled
 * back, else the active slot; verified, with fresh restart attempts. */
core::Status selectSlot(storage::ManagedRoot &root, SlotStore &store, nbt_manager_state &state,
                        bool &trial);

} // namespace launcher
} // namespace nbt

#endif
