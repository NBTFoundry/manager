// SPDX-License-Identifier: MPL-2.0
#include "shortcut.hpp"

#include "launcher_spawn.hpp"
#include "nbt/gui/shortcut_record.hpp"
#include "nbt/log.h"

#include <errno.h>

namespace nbt {
namespace plugin {
namespace {

const unsigned kFirstAction = 3; // App shortcut 1; 4 and 5 follow
uint64_t pressedMs = 0;          // 0: no press waiting for its answer

bool press(unsigned slot) {
    static const char *const kModes[] = {"--shortcut1", "--shortcut2", "--shortcut3"};
    (void)nbt_log_open_path(NBT_LOG_PATH, "plugin");
    nbt_logf("App shortcut %u pressed", slot);
    pressedMs = nowMs();
    // The worker only signals or writes a line: no preloads, as for the door.
    if (!spawnLauncher(kModes[slot - 1], false)) {
        pressedMs = 0;
    }
    return true;
}

bool onFirst(void *, const bmw::Parcel &) {
    return press(1);
}
bool onSecond(void *, const bmw::Parcel &) {
    return press(2);
}
bool onThird(void *, const bmw::Parcel &) {
    return press(3);
}

} // namespace

void addHandler(bmw::PluginApp *app, unsigned id, unsigned type,
                bool (*stub)(void *, const bmw::Parcel &)) {
    bmw::ActionDelegate delegate = {app, stub};
    app->addHandler(id, type, delegate);
}

void addShortcutHandlers(bmw::PluginApp *app) {
    bool (*const stubs[])(void *, const bmw::Parcel &) = {&onFirst, &onSecond, &onThird};
    for (unsigned i = 0; i < 3; ++i) {
        bmw::ActionDelegate delegate = {app, stubs[i]};
        app->addHandler(kFirstAction + i, delegate);
    }
}

bool shortcutPending() {
    return pressedMs != 0;
}

bool shortcutOpenDue(uint64_t now) {
    if (!pressedMs) {
        return false;
    }
    gui::ShortcutRecord record;
    if (gui::shortcutRecordRead(gui::kShortcutRecordPath, record) &&
        gui::shortcutRecordDue(record, pressedMs, now)) {
        pressedMs = 0;
        nbt_logf("App shortcut: opening the manager");
        return true;
    }
    if (now < pressedMs || now - pressedMs > gui::kShortcutFreshMs) {
        pressedMs = 0; // brought back (show), or the launcher failed: nothing to open
    }
    return false;
}

} // namespace plugin
} // namespace nbt
