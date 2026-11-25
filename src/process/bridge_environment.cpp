// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/bridge_environment.hpp"
#include "nbt/gui/hmi_hold.hpp"
#include "nbt/log.h"
#include "nbt/manager_settings.h"
#include "nbt/process/companion_files.hpp"
#include "nbt/storage/files_root.hpp"
#include <errno.h>
#include <stdio.h>
#include <string.h>
namespace nbt {
namespace process {
bool filesDir(const char *app, char *out, size_t size) {
    storage::FilesRoot root;
    core::Status status = root.open(storage::FilesRoot::location());
    if (status.ok()) {
        status = root.appDir(app, true, core::Span<char>(out, size));
    }
    if (!status.ok()) {
        nbt_logf("host %s: files unavailable: %s (errno %d)", app, status.message(), status.code());
        return false;
    }
    return true;
}
size_t bridgeEnvironment(nbt_app_environment *out, size_t count, const nbt_bridge_version &grant,
                         const char *launch, const nbt_fs *fs, const char *app,
                         const nbt_bridge_requirement *bridge) {
    if (grant.major) {
        snprintf(out->text[count], sizeof(out->text[0]), "NBT_BRIDGE=%u.%u", grant.major,
                 grant.minor);
        out->list[count] = out->text[count];
        ++count;
        snprintf(out->text[count], sizeof(out->text[0]), "NBT_BRIDGE_CAPS=%s", grant.caps);
        out->list[count] = out->text[count];
        ++count;
    }
    // Descriptors exist only for a user launch (the host maps them; docs/app-bridge.md).
    static const char *const channels[][2] = {
        {"status", "NBT_STATUS_FD=5"},       {"most", "NBT_MOST_FD=6"},
        {"gamepad", "NBT_GAMEPAD_FD=7"},     {"controller", "NBT_CONTROLLER_FD=8"},
        {"hold", "NBT_HOLD_FD=9"},           {"diag", "NBT_DIAG_FD=10"},
        {"companion", "NBT_COMPANION_FD=11"}};
    const bool user = launch && !strcmp(launch, "user");
    for (unsigned i = 0; user && i < sizeof(channels) / sizeof(channels[0]); ++i) {
        if (nbt_bridge_cap_has(grant.caps, channels[i][0])) {
            strcpy(out->text[count], channels[i][1]);
            out->list[count] = out->text[count];
            ++count;
        }
    }
    // Capability `service`: which of the application's two processes this is
    // (the service is granted no `show`; docs/service-bridge.md).
    if (user && nbt_bridge_cap_has(grant.caps, "service")) {
        strcpy(out->text[count],
               nbt_bridge_cap_has(grant.caps, "show") ? "NBT_ROLE=app" : "NBT_ROLE=service");
        out->list[count] = out->text[count];
        ++count;
    }
    const char *dir = nbt_fs_bridge_dir(fs);
    char path[200]; // /dev/shmem/nbt-bridge1-<48>.hmi, well within an entry
    if (user && app && dir && nbt_bridge_cap_has(grant.caps, "hold") &&
        !nbt::gui::appHoldAnswerPath(dir, app, path, sizeof(path))) {
        snprintf(out->text[count], sizeof(out->text[0]), "NBT_HOLD_STATE=%s", path);
        out->list[count] = out->text[count];
        ++count;
    }
    if (user && app && dir && nbt_bridge_cap_has(grant.caps, "companion") &&
        !companionEventsPath(dir, app, path, sizeof(path))) {
        snprintf(out->text[count], sizeof(out->text[0]), "NBT_COMPANION_EVENTS=%s", path);
        out->list[count] = out->text[count];
        ++count;
    }
    if (user && dir && nbt_bridge_cap_has(grant.caps, "car-data")) {
        snprintf(out->text[count], sizeof(out->text[0]), "NBT_CDS_DIR=%s", dir);
        out->list[count] = out->text[count];
        ++count;
    }
    if (user && bridge && bridge->net[0] && nbt_bridge_cap_has(grant.caps, "net")) {
        snprintf(out->text[count], sizeof(out->text[0]), "NBT_NET=%s", bridge->net);
        out->list[count] = out->text[count];
        ++count;
    }
    // The manager's native_audio setting: the SDK's audio route, and an
    // application's own player, build this connection instead of their default,
    // and play into the io-amp input its source plays.
    if (user && fs && nbt_manager_native_audio(const_cast<nbt_fs *>(fs))) {
        snprintf(out->text[count], sizeof(out->text[0]), "NBT_AUDIO_CONN=0x%x",
                 NBT_NATIVE_AUDIO_MEDIA_CONN);
        out->list[count] = out->text[count];
        ++count;
        strcpy(out->text[count], "NBT_AUDIO_A2I=" NBT_NATIVE_AUDIO_MEDIA_INPUT);
        out->list[count] = out->text[count];
        ++count;
    }
    return count;
}
} // namespace process
} // namespace nbt
