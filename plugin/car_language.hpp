// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PLUGIN_CAR_LANGUAGE_HPP
#define NBT_PLUGIN_CAR_LANGUAGE_HPP
#include "framework.hpp"
#include <stdint.h>
namespace nbt {
namespace plugin {
/* iDrive's language for the manager's screen: car data vehicle.language,
 * subscribed at start and asked for until it answers (then once a minute, in
 * case changes are not delivered), written to the shared record the GUI reads
 * (nbt/gui/car_language.hpp) when it changes. Callbacks run on the plugin's
 * own thread and only parse and write a few bytes. */
struct CarLanguage {
    bmw::PluginApp *app;
    uint64_t nextAsk;
    int written; // the value in the record; -1 none yet
    bool known;
    void start(bmw::PluginApp *instance, uint64_t now);
    void stop();
    void poll(uint64_t now);
    static bool onLanguage(void *, const bmw::BasicString<unsigned short> &);
};
} // namespace plugin
} // namespace nbt
#endif
