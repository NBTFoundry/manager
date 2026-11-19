// SPDX-License-Identifier: MPL-2.0
#include "nbt/lifecycle/app_settings.hpp"

#include "nbt/app_settings.h"
#include "nbt/core/unique_fd.hpp"
#include "nbt/lifecycle/transactions.hpp"
#include "nbt/process/bridge_environment.hpp"
#include "nbt/process/hmi_environment.hpp"
#include "nbt/transaction.h"
#include "nbt/util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <utility>

namespace nbt {
namespace lifecycle {
namespace {

const size_t kFileMax = 16 + NBT_SETTINGS_MAX * (NBT_SETTING_TEXT_MAX + 10);

enum ReadStatus { ReadError = -1, ReadPresent = 0, ReadAbsent = 1 };

ReadStatus readFile(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, char *out, size_t size,
                    size_t *length) {
    size_t used = 0;
    core::UniqueFd fd(nbt_fs_open(fs, kind, app, NULL, NBT_FS_READ));
    if (!fd.valid()) {
        return errno == ENOENT ? ReadAbsent : ReadError;
    }
    for (;;) {
        if (used == size) {
            errno = EFBIG;
            return ReadError;
        }
        const ssize_t n = ::read(fd.get(), out + used, size - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            return ReadError;
        }
        if (n == 0) {
            break;
        }
        used += static_cast<size_t>(n);
    }
    if (!fd.close().ok()) {
        return ReadError;
    }
    *length = used;
    return ReadPresent;
}

int publishSnapshot(nbt_fs *fs, const char *app, const nbt_settings *settings) {
    char text[NBT_SETTINGS_VALUES_MAX];
    const int n = nbt_settings_values_format(settings, text, sizeof(text));
    if (n < 0) {
        errno = EINVAL;
        return -1;
    }
    core::UniqueFd fd(nbt_fs_open(fs, NBT_FS_DATA_SETTINGS, app, NULL, NBT_FS_REWRITE));
    if (!fd.valid()) {
        return -1;
    }
    int result = nbt_fs_settings_write(fs, app, fd.get(), text, static_cast<size_t>(n));
    if (!fd.close().ok()) {
        result = -1;
    }
    return result;
}

void capitalizeSettingKey(char *line) {
    for (size_t k = 12; line[k] && line[k] != '='; ++k) {
        if (line[k] >= 'a' && line[k] <= 'z') {
            line[k] = static_cast<char>(line[k] - 'a' + 'A');
        }
    }
}

} // namespace

core::Result<sdk::SettingsList> AppSettingsStore::load(const storage::ManagedRoot &root,
                                                       const char *app) {
    sdk::SettingsList settings;
    if (!root.valid() ||
        nbt_app_settings_load(const_cast<nbt_fs *>(root.legacy()), app, &settings.legacy())) {
        return core::Result<sdk::SettingsList>::failure(
            core::Status::fromErrno("application settings load failed"));
    }
    return core::Result<sdk::SettingsList>::success(std::move(settings));
}

core::Status AppSettingsStore::saveSchema(const storage::ManagedRoot &root, const char *app,
                                          const sdk::SettingsList &schema) {
    if (!root.valid()) {
        return core::Status::error(core::kInvalidArgument, "managed root is not initialized");
    }
    if (nbt_app_settings_save_schema(const_cast<nbt_fs *>(root.legacy()), app, &schema.legacy())) {
        return core::Status::fromErrno("application schema save failed");
    }
    return core::Status();
}

core::Status AppSettingsStore::set(const storage::ManagedRoot &root, const char *app,
                                   const char *key, const char *value) {
    if (!root.valid()) {
        return core::Status::error(core::kInvalidArgument, "managed root is not initialized");
    }
    if (nbt_app_settings_set(const_cast<nbt_fs *>(root.legacy()), app, key, value)) {
        return core::Status::fromErrno("application setting update failed");
    }
    return core::Status();
}

AppEnvironment::AppEnvironment() {
    memset(&environment_, 0, sizeof(environment_));
}

core::Status AppEnvironment::build(const storage::ManagedRoot &root, const char *app,
                                   const char *launch, const nbt_bridge_requirement *bridge,
                                   const char *available) {
    if (!root.valid() || nbt_app_environment_build(const_cast<nbt_fs *>(root.legacy()), app, launch,
                                                   bridge, available, &environment_)) {
        return core::Status::fromErrno("application environment build failed");
    }
    return core::Status();
}

} // namespace lifecycle
} // namespace nbt

int nbt_app_settings_load(nbt_fs *fs, const char *app, nbt_settings *settings) {
    char text[nbt::lifecycle::kFileMax];
    size_t length = 0;
    if (!fs || !app || !settings || !strcmp(app, "manager")) {
        errno = EINVAL;
        return -1;
    }
    memset(settings, 0, sizeof(*settings));
    const nbt::lifecycle::ReadStatus found =
        nbt::lifecycle::readFile(fs, NBT_FS_SETTINGS_SCHEMA, app, text, sizeof(text), &length);
    if (found == nbt::lifecycle::ReadError) {
        return -1;
    }
    if (found == nbt::lifecycle::ReadAbsent) {
        return 0;
    }
    if (nbt_settings_schema_parse(text, length, settings)) {
        memset(settings, 0, sizeof(*settings));
        errno = EINVAL;
        return -1;
    }
    if (nbt::lifecycle::readFile(fs, NBT_FS_SETTINGS, app, text, sizeof(text), &length) ==
        nbt::lifecycle::ReadPresent) {
        (void)nbt_settings_values_apply(settings, text, length);
    }
    return 0;
}

int nbt_app_settings_save_schema(nbt_fs *fs, const char *app, const nbt_settings *schema) {
    char text[nbt::lifecycle::kFileMax];
    if (!fs || !app || !schema || !strcmp(app, "manager")) {
        errno = EINVAL;
        return -1;
    }
    const int n = nbt_settings_schema_format(schema, text, sizeof(text));
    if (n < 0) {
        errno = EINVAL;
        return -1;
    }
    if (nbt_fs_replace_text(fs, NBT_FS_SETTINGS_SCHEMA_NEXT, NBT_FS_SETTINGS_SCHEMA, app, text,
                            static_cast<size_t>(n))) {
        return -1;
    }
    nbt_settings current;
    if (nbt_app_settings_load(fs, app, &current) ||
        nbt::lifecycle::publishSnapshot(fs, app, &current)) {
        return -1;
    }
    return 0;
}

int nbt_app_settings_set(nbt_fs *fs, const char *app, const char *key, const char *value) {
    char text[NBT_SETTINGS_VALUES_MAX];
    nbt_settings settings;
    if (!fs || !app || !key || !value || !strcmp(app, "manager")) {
        errno = EINVAL;
        return -1;
    }
    nbt::core::UniqueFd lock(nbt_tx_lock(fs, app));
    if (!lock.valid() || nbt_app_settings_load(fs, app, &settings)) {
        return -1;
    }
    const int index = nbt_settings_find(&settings, key);
    if (index < 0 || !nbt_setting_allows(&settings.items[index], value)) {
        errno = EINVAL;
        return -1;
    }
    strcpy(settings.items[index].value, value);
    const int n = nbt_settings_values_format(&settings, text, sizeof(text));
    if (n < 0) {
        errno = EINVAL;
        return -1;
    }
    nbt::core::UniqueFd snapshot(nbt_fs_open(fs, NBT_FS_DATA_SETTINGS, app, NULL, NBT_FS_REWRITE));
    if (!snapshot.valid() || nbt_fs_replace_text(fs, NBT_FS_SETTINGS_NEXT, NBT_FS_SETTINGS, app,
                                                 text, static_cast<size_t>(n))) {
        return -1;
    }
    int result = nbt_fs_settings_write(fs, app, snapshot.get(), text, static_cast<size_t>(n));
    if (!snapshot.close().ok() || !lock.close().ok()) {
        result = -1;
    }
    return result;
}

int nbt_app_environment_build(nbt_fs *fs, const char *app, const char *launch,
                              const nbt_bridge_requirement *bridge, const char *available,
                              nbt_app_environment *out) {
    nbt_settings settings;
    nbt_bridge_provider provider;
    nbt_bridge_version grant;
    char data[NBT_PATH_MAX];
    char path[NBT_PATH_MAX];
    size_t count = 0;
    if (!fs || !app || !out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    nbt_bridge_current(&provider);
    if (nbt_bridge_check(bridge, &provider, available ? available : provider.versions[0].caps,
                         &grant, NULL, 0) ||
        nbt_fs_app_data(fs, app, data)) {
        return -1;
    }
    if (!nbt::process::hmiRuntime(out->list, nbt::process::kHmiRuntimeMax, out->preload,
                                  sizeof(out->preload), &count)) {
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!strncmp(out->list[i], "LD_LIBRARY_PATH=", 16) &&
            nbt::process::withMediaLibraries(out->list[i], out->library_path,
                                             sizeof(out->library_path))) {
            out->list[i] = out->library_path;
        }
    }
    snprintf(out->data_dir, sizeof(out->data_dir), "NBT_DATA_DIR=%s", data);
    out->list[count++] = out->data_dir;
    const int loaded = nbt_app_settings_load(fs, app, &settings);
    if (nbt_bridge_cap_has(grant.caps, "live-settings")) {
        if (loaded || nbt::lifecycle::publishSnapshot(fs, app, &settings) ||
            nbt_fs_path(fs, NBT_FS_DATA_SETTINGS, app, NULL, path, sizeof(path))) {
            return -1;
        }
        snprintf(out->settings_file, sizeof(out->settings_file), "NBT_SETTINGS_FILE=%s", path);
        out->list[count++] = out->settings_file;
    }
    count = nbt::process::bridgeEnvironment(out, count, grant, launch);
    if (launch) {
        const int n = snprintf(out->text[count], sizeof(out->text[0]), "NBT_LAUNCH=%s", launch);
        if (n < 0 || n >= static_cast<int>(sizeof(out->text[0]))) {
            return -1;
        }
        out->list[count] = out->text[count];
        ++count;
    }
    if (!loaded) {
        for (size_t i = 0; i < settings.count; ++i) {
            if (count >= sizeof(out->text) / sizeof(out->text[0])) {
                return -1; // cannot happen with the sizes in app_settings.h
            }
            char *line = out->text[count];
            const int n = snprintf(line, sizeof(out->text[0]), "NBT_SETTING_%s=%s",
                                   settings.items[i].key, settings.items[i].value);
            if (n < 0 || n >= static_cast<int>(sizeof(out->text[0]))) {
                continue;
            }
            nbt::lifecycle::capitalizeSettingKey(line);
            out->list[count++] = line;
        }
    }
    out->list[count] = NULL;
    return 0;
}
