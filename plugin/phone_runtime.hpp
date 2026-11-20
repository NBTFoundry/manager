// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PLUGIN_PHONE_RUNTIME_HPP
#define NBT_PLUGIN_PHONE_RUNTIME_HPP
#include "framework.hpp"
#include "nbt/gui/phone_wake.hpp"
#include "phone_display.hpp"
namespace nbt {
namespace plugin {
struct PhoneRuntime {
    bmw::PluginApp *app;
    uint64_t boot, nextSample, nextDoor, speedMs, contextMs, openingMs, doorHealthyMs;
    bool guardReset, shimChecked, autostartAsked;
    unsigned speed;
    bool speedKnown, contextSafe, opening;
    gui::PhoneWake request;
    PhoneDisplayProbe display;
    void start(bmw::PluginApp *instance, uint64_t now);
    void stop();
    bool doorDue(uint64_t now);
    /* Once, 60 s after iDrive started: the capture shim's check (--shim-check). */
    bool shimCheckDue(uint64_t now);
    /* Once per boot, 25 s after iDrive started (after the door's job): the
     * applications set to start with the car (--autostart). */
    bool autostartDue(uint64_t now);
    bool poll(uint64_t now, bool open);
    void openFailed();
    static bool onSpeed(void *, const bmw::BasicString<unsigned short> &);
    static bool onContext(void *, const bmw::BasicString<unsigned short> &);
};
} // namespace plugin
} // namespace nbt
#endif
