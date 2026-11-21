// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/phone_routes.hpp"
#include "nbt/app_settings.h"
#include "nbt/manager_refresh.h"
#include "nbt/manager_store.h"
#include "nbt/transaction.h"
#include "nbt/util.h"
#include "nbt/worker/autostart_request.hpp"
#include "nbt/worker/worker.hpp"
#include <stdio.h>
#include <string.h>
namespace nbt {
namespace worker {
int phoneApplications(Worker &worker, core::Span<char> out) {
    char names[NBT_APP_LIST_MAX][NBT_APP_NAME_MAX + 1];
    const int count = nbt_fs_list_apps(worker.root().legacy(), names);
    if (count < 0) {
        return 503;
    }
    int n = snprintf(out.data(), out.size(), "APPS\t%d\n", count);
    if (n < 0 || static_cast<size_t>(n) >= out.size()) {
        return 500;
    }
    size_t used = static_cast<size_t>(n);
    for (int i = 0; i < count; ++i) {
        nbt_installed installed;
        core::Result<bool> running = worker.running(names[i]);
        if (!running.ok() || nbt_tx_installed(worker.root().legacy(), names[i], &installed)) {
            return 503;
        }
        n = snprintf(out.data() + used, out.size() - used, "%s\t%s\t%llu\t%d\n", names[i],
                     installed.version, static_cast<unsigned long long>(installed.sequence),
                     running.value() ? 1 : 0);
        if (n < 0 || static_cast<size_t>(n) >= out.size() - used) {
            return 500;
        }
        used += static_cast<size_t>(n);
    }
    return 200;
}
int phoneAutostart(Worker &worker, const char *body, core::Span<char> out) {
    char text[64];
    const size_t length = body ? strlen(body) : 0;
    if (!length || length >= sizeof(text) || body[length - 1] != '\n') {
        return 400;
    }
    memcpy(text, body, length - 1);
    text[length - 1] = 0;
    char *mode = strchr(text, '\t');
    if (mode) {
        *mode++ = 0;
    }
    const AutostartResult result = autostartRequest(worker, text, mode, out);
    return result == AutostartDone ? 200 : result == AutostartInvalid ? 400 : 409;
}

int phoneSettings(Worker &worker, const char *body, bool setting, core::Span<char> out) {
    char text[160];
    if (!body || strlen(body) >= sizeof(text)) {
        return 400;
    }
    strcpy(text, body);
    const size_t length = strlen(text);
    if (!length || text[length - 1] != '\n') {
        return 400;
    }
    text[length - 1] = 0;
    char *app = text, *key = NULL, *value = NULL;
    if (setting) {
        key = strchr(app, '\t');
        if (!key) {
            return 400;
        }
        *key++ = 0;
        value = strchr(key, '\t');
        if (!value) {
            return 400;
        }
        *value++ = 0;
    }
    if (!nbt_token_valid(app, 48, 0) || !strcmp(app, "manager") ||
        nbt_fs_exists(worker.root().legacy(), NBT_FS_PROFILE, app, NULL) != 1) {
        return 400;
    }
    if (setting) {
        if ((worker.state().updateLock.valid() && !strcmp(app, worker.state().app)) ||
            nbt_app_settings_set(worker.root().legacy(), app, key, value)) {
            return 409;
        }
        snprintf(out.data(), out.size(), "OK\n");
        return 200;
    }
    nbt_settings settings;
    if (nbt_app_settings_load(worker.root().legacy(), app, &settings)) {
        return 503;
    }
    int n =
        snprintf(out.data(), out.size(), "SETTINGS\t%u\n", static_cast<unsigned>(settings.count));
    if (n < 0 || static_cast<size_t>(n) >= out.size()) {
        return 500;
    }
    size_t used = static_cast<size_t>(n);
    for (size_t i = 0; i < settings.count; ++i) {
        n = nbt_setting_format(&settings.items[i], out.data() + used, out.size() - used);
        if (n < 0 || static_cast<size_t>(n) + 2 > out.size() - used) {
            return 500;
        }
        used += static_cast<size_t>(n);
        out[used++] = '\n';
        out[used] = 0;
    }
    return 200;
}
int phoneManager(Worker &worker, core::Span<char> out) {
    nbt_fs *fs = worker.root().legacy();
    nbt_manager_state state;
    char version[49];
    if (nbt_manager_store_inspect(fs, &state) || nbt_manager_launcher_current(fs, version) < 0) {
        return 503;
    }
    // The formats a phone update can use: the stable launcher stages it
    // (manager_upload.cpp); NBTRLS1 needs nothing from it.
    char formats[32] = "NBTRLS1";
    for (unsigned format = 2; format <= 3; ++format) {
        char name[8];
        snprintf(name, sizeof(name), "NBTRLS%u", format);
        const int supported = nbt_manager_launcher_supports_format(fs, name);
        if (supported < 0) {
            return 503;
        }
        if (supported) {
            const size_t used = strlen(formats);
            snprintf(formats + used, sizeof(formats) - used, ",%s", name);
        }
    }
    const int n = snprintf(out.data(), out.size(), "MANAGER\t%s\t%llu\t%s\n", version,
                           static_cast<unsigned long long>(state.active.sequence), formats);
    return n > 0 && static_cast<size_t>(n) < out.size() ? 200 : 500;
}
} // namespace worker
} // namespace nbt
