// SPDX-License-Identifier: MPL-2.0
/* Starting the manager launcher from iDrive, and the plugin's small helpers
 * around it (clock, error log, whether the manager window runs). */
#ifndef NBT_PLUGIN_LAUNCHER_SPAWN_HPP
#define NBT_PLUGIN_LAUNCHER_SPAWN_HPP

#include <stdint.h>

namespace nbt {
namespace plugin {

uint64_t nowMs();
/* To the system log and, when on, the debug log. */
void logError(const char *message, int error);
/* gui.lock write-locked and the window's liveness file fresh. */
bool guiRunning();
/* NBT-AAD's spawn environment: own process group, default signals, empty
 * mask, its PATH plus iDrive's environment. Output goes to the enabled debug
 * log, else nowhere. */
bool spawnLauncher(const char *mode = "--open", bool includePreload = true);

} // namespace plugin
} // namespace nbt

#endif
