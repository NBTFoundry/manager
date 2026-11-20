// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LAUNCHER_DOOR_HPP
#define NBT_LAUNCHER_DOOR_HPP
#include "nbt/fs.h"
namespace nbt {
namespace launcher {
/* Boot-time jobs the iDrive plugin starts, each with the known-good worker
 * (the active slot, or the previous one while an update is pending) under the
 * manager operation lock, without opening the GUI. */
/* --door: the phone's boot-time listener; returns once the worker is ready. */
int startDoor(nbt_fs *fs);
/* --autostart: starts the applications set to start with the car (the
 * worker's --autostart); returns when that dispatch has ended, at most 15 s. */
int runAutostart(nbt_fs *fs);
} // namespace launcher
} // namespace nbt
#endif
