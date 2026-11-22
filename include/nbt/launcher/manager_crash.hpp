// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LAUNCHER_MANAGER_CRASH_HPP
#define NBT_LAUNCHER_MANAGER_CRASH_HPP

#include "nbt/manager_state.h"
#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace launcher {

/* After the GUI or the worker ended other than by the user closing the
 * manager: prints the GUI's crash record (manager/crash.gui) to the launcher's
 * output, and writes NBT Manager's own crash report
 * (nbt/log/crash_report.hpp): `reason`, that record, and the debug log's
 * newest 16 KiB while it is on. */
void reportManagerCrash(storage::ManagedRoot &root, const nbt_manager_state &state,
                        const char *reason);

} // namespace launcher
} // namespace nbt

#endif
