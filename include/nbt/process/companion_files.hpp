// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_COMPANION_FILES_HPP
#define NBT_PROCESS_COMPANION_FILES_HPP

/* The two files of a phone companion in the bridge directory
 * (docs/companion-bridge.md): the page its process writes (the host creates
 * it, the child's fd 11) and the phone's events (the worker and the door
 * replace it whole). The host removes both when the process starts and ends. */

#include <stddef.h>

namespace nbt {
namespace process {

/* <dir>/nbt-bridge1-<app>.events: 0 or -1. */
int companionEventsPath(const char *dir, const char *app, char *out, size_t size);
/* The page, checked (nbt_companion_page_check): its length, 0 when there is
 * none or it is malformed, -1 on an error. */
int companionPageRead(const char *dir, const char *app, char *out, size_t size);
/* Adds the phone's event, numbered after the last one, keeping the last
 * NBT_COMPANION_EVENTS_MAX: 0 or -1. */
int companionEventAdd(const char *dir, const char *app, const char *id, const char *value);
void companionRemove(const char *dir, const char *app);

} // namespace process
} // namespace nbt

#endif
