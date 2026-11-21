// SPDX-License-Identifier: MPL-2.0
/* iDrive's App shortcut entries 1-3 (raActions 3-5, NbtManager.xml): a press
 * starts `nbt-manager-launcher --shortcut<n>`, whose worker brings the
 * assigned application back or asks, in the shortcut record, for the manager
 * window (nbt/gui/shortcut_record.hpp). The plugin then opens it as for its
 * own entry. STL-free, no heap. */
#ifndef NBT_PLUGIN_SHORTCUT_HPP
#define NBT_PLUGIN_SHORTCUT_HPP

#include "framework.hpp"

#include <stdint.h>

namespace nbt {
namespace plugin {

/* A handler of `type` for `id` (a state's focus handler). */
void addHandler(bmw::PluginApp *app, unsigned id, unsigned type,
                bool (*stub)(void *, const bmw::Parcel &));
/* The three action handlers. */
void addShortcutHandlers(bmw::PluginApp *app);
/* While a press waits for its answer: poll every 250 ms, not every second. */
bool shortcutPending();
/* True once when the manager window is to open for the last press. */
bool shortcutOpenDue(uint64_t now);

} // namespace plugin
} // namespace nbt

#endif
