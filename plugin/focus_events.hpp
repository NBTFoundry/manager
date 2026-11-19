// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PLUGIN_FOCUS_EVENTS_HPP
#define NBT_PLUGIN_FOCUS_EVENTS_HPP

namespace bmw {
class PluginApp;
}

namespace nbt {
namespace plugin {

// iDrive focus events (50 hold, 51 release, 52 open), sent from the plugin's
// own sender thread (nbt/gui/focus_sender.hpp) so that the plugin's iDrive
// thread never waits on iDrive. An event that has not returned after 3 s is
// logged with a snapshot of the HMI's threads, again at 10 s.
void startFocusEvents(bmw::PluginApp *app);
void stopFocusEvents();
void focusEvent(bmw::PluginApp *app, unsigned event);
// True while an event has been in iDrive for 3 s or more.
bool focusStuck();

} // namespace plugin
} // namespace nbt

#endif
