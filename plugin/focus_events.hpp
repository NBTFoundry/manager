// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PLUGIN_FOCUS_EVENTS_HPP
#define NBT_PLUGIN_FOCUS_EVENTS_HPP

#include <stddef.h>

namespace bmw {
class PluginApp;
}

namespace nbt {
namespace plugin {

const unsigned kHoldEvent = 50;    // to the blank state 14 (NbtManager.xml)
const unsigned kReleaseEvent = 51; // back to the list holding the entry
const unsigned kOpenEvent = 52;    // to the parent state 15

// iDrive focus events (50 hold, 51 release, 52 open), sent from the plugin's
// own sender thread (nbt/gui/focus_sender.hpp) so that the plugin's iDrive
// thread never waits on iDrive. An event that has not returned after 3 s is
// logged with a snapshot of the HMI's threads, again at 10 s.
void startFocusEvents(bmw::PluginApp *app);
void stopFocusEvents();
void focusEvent(bmw::PluginApp *app, unsigned event);
// True while an event has been in iDrive for 3 s or more.
bool focusStuck();
// The events a hold's actions ask for (nbt/gui/hmi_hold.hpp HoldAction):
// reopen = 51 then 52, open = 52, hold = 50, release = 51, in that order.
void holdFocusEvents(bmw::PluginApp *app, unsigned actions);
// " spawn reopen (list, then open) open hold release aside show" for the log.
void holdActionsText(unsigned actions, char *out, size_t size);

} // namespace plugin
} // namespace nbt

#endif
