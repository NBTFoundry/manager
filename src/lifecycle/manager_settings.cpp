// SPDX-License-Identifier: MPL-2.0
#include "nbt/lifecycle/manager_settings.hpp"

#include "nbt/controller_config.h"
#include "nbt/manager_settings.h"
#include "nbt/model/language.hpp"
#include "nbt/storage/phone_store.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <utility>

namespace nbt {
namespace lifecycle {
namespace {

/* "Open from phone" is not shown from dev48: the plugin opening the manager froze
 * iDrive until its watchdog restarted it (dev47). SET still takes phone_open for
 * the door's tests; the plugin refuses to open either way (plugin/phone_runtime). */
const char *const kDeclarations[] = {
    "debug_log|Debug log|off|off,on",
    "invert|Invert knob|off|off,on",
    "release|Hand back controller|off|off,on",
    "native_audio|Native audio|interrupt|interrupt,media",
};

int barrier(nbt_fs *fs) {
    int fd = nbt_fs_open(fs, NBT_FS_SYNC, "manager", NULL, NBT_FS_OPEN_LOCK);
    if (fd < 0) {
        return -1;
    }
    if (fsync(fd)) {
        const int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    if (close(fd)) {
        return -1;
    }
#ifndef __QNX__
    {
        char path[NBT_PATH_MAX];
        if (nbt_fs_path(fs, NBT_FS_SETUP_MANAGER_DIR, "manager", NULL, path, sizeof(path))) {
            return -1;
        }
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            return -1;
        }
        if (fsync(fd)) {
            const int saved = errno;
            close(fd);
            errno = saved;
            return -1;
        }
        if (close(fd)) {
            return -1;
        }
    }
#endif
    return 0;
}

int declare(nbt_settings *settings) {
    memset(settings, 0, sizeof(*settings));
    for (size_t i = 0; i < sizeof(kDeclarations) / sizeof(kDeclarations[0]); ++i) {
        if (nbt_settings_add(settings, kDeclarations[i])) {
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}

void setValue(nbt_settings *settings, const char *key, int on) {
    const int index = nbt_settings_find(settings, key);
    if (index >= 0) {
        strcpy(settings->items[index].value, on ? "on" : "off");
    }
}

int debugLog(nbt_fs *fs, int on) {
    if (on) {
        const int fd = nbt_fs_open(fs, NBT_FS_MANAGER_LOG, "manager", NULL, NBT_FS_CREATE);
        if (fd < 0) {
            if (errno != EEXIST || nbt_fs_exists(fs, NBT_FS_MANAGER_LOG, "manager", NULL) != 1) {
                return -1;
            }
        } else if (fsync(fd)) {
            const int saved = errno;
            close(fd);
            errno = saved;
            return -1;
        } else if (close(fd)) {
            return -1;
        }
    } else if (nbt_fs_rename(fs, NBT_FS_MANAGER_LOG, NBT_FS_MANAGER_LOG_OLD, "manager", NULL) &&
               errno != ENOENT) {
        return -1;
    }
    return barrier(fs);
}

/* Writes `text` to `next`, synced, and renames it over `kind`. */
int replace(nbt_fs *fs, nbt_fs_path_kind next, nbt_fs_path_kind kind, const char *text, size_t n) {
    size_t done = 0;
    int result = 0;
    if (nbt_fs_unlink(fs, next, "manager", NULL) && errno != ENOENT) {
        return -1;
    }
    const int fd = nbt_fs_open(fs, next, "manager", NULL, NBT_FS_CREATE);
    if (fd < 0) {
        return -1;
    }
    while (done < n && !result) {
        const ssize_t w = write(fd, text + done, n - done);
        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w <= 0) {
            result = -1;
        } else {
            done += (size_t)w;
        }
    }
    if (!result && fsync(fd)) {
        result = -1;
    }
    if (close(fd)) {
        result = -1;
    }
    if (result || nbt_fs_rename(fs, next, kind, "manager", NULL) || barrier(fs)) {
        const int saved = errno;
        (void)nbt_fs_unlink(fs, next, "manager", NULL);
        errno = saved;
        return -1;
    }
    return 0;
}

int controller(nbt_fs *fs, const char *key, int on) {
    char text[NBT_CONTROLLER_CONFIG_MAX + 1];
    nbt_ccs_config config;
    (void)nbt_controller_config_load(fs, &config);
    if (!strcmp(key, "invert")) {
        config.invert = on;
    } else {
        config.release = (uint8_t)on;
    }
    const int n = nbt_controller_config_format(&config, text, sizeof(text));
    if (n < 0) {
        errno = EINVAL;
        return -1;
    }
    return replace(fs, NBT_FS_MANAGER_CONTROLLER_CONFIG_NEXT, NBT_FS_MANAGER_CONTROLLER_CONFIG,
                   text, (size_t)n);
}

/* The screen's language (nbt/model/language.hpp): not a declared setting,
 * since it has more choices than a declaration allows. */
int language(nbt_fs *fs, const char *value) {
    int choice = model::kLanguageAuto;
    char text[model::kLanguageFileMax + 1];
    if (!model::languageSettingParse(value, choice)) {
        errno = EINVAL;
        return -1;
    }
    const size_t n = strlen(model::languageSettingCode(choice));
    memcpy(text, model::languageSettingCode(choice), n);
    text[n] = '\n';
    return replace(fs, NBT_FS_MANAGER_LANGUAGE_NEXT, NBT_FS_MANAGER_LANGUAGE, text, n + 1);
}

/* "shortcut<n>" to <app> or off (nbt/manager_settings.h). */
int shortcut(nbt_fs *fs, const char *key, const char *value) {
    char apps[NBT_SHORTCUTS][NBT_ID_MAX + 1];
    const unsigned slot = static_cast<unsigned>(key[8] - '1');
    const bool off = !strcmp(value, "off");
    if (strlen(key) != 9 || slot >= NBT_SHORTCUTS ||
        (!off && (!nbt_token_valid(value, NBT_ID_MAX, 0) || !strcmp(value, "manager")))) {
        errno = EINVAL;
        return -1;
    }
    if (nbt_manager_shortcuts_load(fs, apps)) {
        return -1;
    }
    char text[NBT_SHORTCUTS * (NBT_ID_MAX + 1) + 1];
    size_t n = 0;
    for (unsigned i = 0; i < NBT_SHORTCUTS; ++i) {
        const char *app =
            i == slot ? (off ? "" : value) : (!off && !strcmp(apps[i], value) ? "" : apps[i]);
        const size_t length = strlen(app);
        memcpy(text + n, app, length);
        n += length;
        text[n++] = '\n';
    }
    return replace(fs, NBT_FS_MANAGER_SHORTCUTS_NEXT, NBT_FS_MANAGER_SHORTCUTS, text, n);
}

int loadLegacy(nbt_fs *fs, nbt_settings *settings) {
    nbt_ccs_config config;
    if (!fs || !settings) {
        errno = EINVAL;
        return -1;
    }
    if (declare(settings)) {
        return -1;
    }
    const int log = nbt_fs_exists(fs, NBT_FS_MANAGER_LOG, "manager", NULL);
    if (log < 0) {
        return -1;
    }
    (void)nbt_controller_config_load(fs, &config);
    setValue(settings, "debug_log", log);
    setValue(settings, "invert", config.invert);
    setValue(settings, "release", config.release);
    const int audio = nbt_settings_find(settings, "native_audio");
    if (audio >= 0 && nbt_manager_native_audio(fs)) {
        strcpy(settings->items[audio].value, "media");
    }
    return 0;
}

int setLegacy(nbt_fs *fs, const char *key, const char *value) {
    nbt_settings settings;
    if (!fs || !key || !value) {
        errno = EINVAL;
        return -1;
    }
    if (!strcmp(key, "phone_open") && (!strcmp(value, "on") || !strcmp(value, "off"))) {
        storage::ManagedRoot root(*fs);
        storage::PhoneStore phones(root);
        return phones.initialize() && phones.setOpenEnabled(!strcmp(value, "on")) ? 0 : -1;
    }
    if (!strcmp(key, "language")) {
        return language(fs, value);
    }
    if (!strncmp(key, "shortcut", 8)) {
        return shortcut(fs, key, value);
    }
    if (declare(&settings)) {
        return -1;
    }
    const int index = nbt_settings_find(&settings, key);
    if (index < 0 || !nbt_setting_allows(&settings.items[index], value)) {
        errno = EINVAL;
        return -1;
    }
    if (!strcmp(key, "native_audio")) {
        const size_t n = strlen(value);
        char text[16];
        memcpy(text, value, n); // a declared choice: at most "interrupt"
        text[n] = '\n';
        return replace(fs, NBT_FS_MANAGER_NATIVE_AUDIO_NEXT, NBT_FS_MANAGER_NATIVE_AUDIO, text,
                       n + 1);
    }
    const int on = !strcmp(value, "on");
    if (!strcmp(key, "debug_log")) {
        return debugLog(fs, on);
    }
    return controller(fs, key, on);
}

} // namespace

core::Result<sdk::SettingsList> ManagerSettings::load(const storage::ManagedRoot &root) {
    sdk::SettingsList settings;
    if (!root.valid() || loadLegacy(const_cast<nbt_fs *>(root.legacy()), &settings.legacy())) {
        return core::Result<sdk::SettingsList>::failure(
            core::Status::error(errno ? errno : EIO, "manager settings load failed"));
    }
    return core::Result<sdk::SettingsList>::success(std::move(settings));
}

core::Status ManagerSettings::set(const storage::ManagedRoot &root, const char *key,
                                  const char *value) {
    if (!root.valid() || setLegacy(const_cast<nbt_fs *>(root.legacy()), key, value)) {
        return core::Status::error(errno ? errno : EIO, "manager setting save failed");
    }
    return core::Status();
}

} // namespace lifecycle
} // namespace nbt

extern "C" {

int nbt_manager_settings_load(nbt_fs *fs, nbt_settings *settings) {
    if (!fs || !settings) {
        errno = EINVAL;
        return -1;
    }
    const nbt::storage::ManagedRoot root(*fs);
    nbt::core::Result<nbt::sdk::SettingsList> loaded = nbt::lifecycle::ManagerSettings::load(root);
    if (!loaded.get()) {
        return -1;
    }
    *settings = loaded.value().legacy();
    return 0;
}

int nbt_manager_settings_set(nbt_fs *fs, const char *key, const char *value) {
    if (!fs) {
        errno = EINVAL;
        return -1;
    }
    const nbt::storage::ManagedRoot root(*fs);
    return nbt::lifecycle::ManagerSettings::set(root, key, value).ok() ? 0 : -1;
}

} // extern "C"
