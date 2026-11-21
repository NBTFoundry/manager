// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/shortcut_dispatch.hpp"

#include "nbt/app_host.h"
#include "nbt/gui/shortcut_record.hpp"
#include "nbt/log.h"
#include "nbt/manager_settings.h"
#include "nbt/storage/managed_root.hpp"
#include "nbt/util.h"

#include <string.h>

namespace nbt {
namespace worker {
unsigned shortcutSlot(const char *argument) {
    if (!argument || strncmp(argument, "--shortcut", 10) || argument[11]) {
        return 0;
    }
    const unsigned slot = static_cast<unsigned>(argument[10] - '0');
    return slot >= 1 && slot <= NBT_SHORTCUTS ? slot : 0;
}

int shortcutDispatch(nbt_fs *fs, unsigned slot) {
    char apps[NBT_SHORTCUTS][NBT_ID_MAX + 1];
    uint64_t now = 0;
    if (!slot || slot > NBT_SHORTCUTS || nbt_monotonic_ms(&now) ||
        nbt_manager_shortcuts_load(fs, apps)) {
        return 1;
    }
    const char *app = apps[slot - 1];
    gui::ShortcutRecord record;
    memset(&record, 0, sizeof(record));
    record.ms = now;
    record.word = gui::ShortcutOpen;
    if (app[0] && nbt_fs_exists(fs, NBT_FS_PROFILE, app, NULL) == 1) {
        const pid_t host = nbt_app_host_pid(fs, app);
        if (host > 0 && nbt_app_host_show(fs, app) == 0) {
            nbt_logf("shortcut %u: %s asked to come back", slot, app);
            return 0;
        }
        if (!host) { // the manager opens and starts it
            record.word = gui::ShortcutStart;
            memcpy(record.app, app, strlen(app) + 1);
        }
    }
    nbt_logf("shortcut %u: %s%s", slot,
             record.word == gui::ShortcutStart ? "the manager starts " : "the manager opens",
             record.word == gui::ShortcutStart ? app : "");
    return gui::shortcutRecordWrite(gui::shortcutRecordPath(), record) ? 0 : 1;
}

int shortcutMain(const char *root, const char *self, int argc, char **argv) {
    const unsigned slot = argc >= 2 ? shortcutSlot(argv[1]) : 0;
#ifdef NBT_TESTING
    if (argc == 4 && !strcmp(argv[2], "--root")) {
        root = argv[3];
    } else if (argc != 2) {
        return 2;
    }
#else
    if (argc != 2) {
        return 2;
    }
#endif
    storage::ManagedRoot rootObject;
    if (!slot || !rootObject.initialize(root).ok()) {
        return slot ? 1 : 2;
    }
#ifdef NBT_TESTING
    if (nbt_app_host_set_self(self, root)) {
        return 1;
    }
#else
    if (nbt_app_host_set_self(self, NULL)) {
        return 1;
    }
#endif
    (void)nbt_log_open(rootObject.legacy(), "shortcut");
    const int result = shortcutDispatch(rootObject.legacy(), slot);
    nbt_log_close();
    return result;
}

} // namespace worker
} // namespace nbt
