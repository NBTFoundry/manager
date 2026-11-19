// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/bridge_environment.hpp"
#include <stdio.h>
#include <string.h>
namespace nbt {
namespace process {
size_t bridgeEnvironment(nbt_app_environment *out, size_t count, const nbt_bridge_version &grant,
                         const char *launch) {
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
    static const char *const channels[][2] = {{"status", "NBT_STATUS_FD=5"},
                                              {"most", "NBT_MOST_FD=6"},
                                              {"gamepad", "NBT_GAMEPAD_FD=7"},
                                              {"controller", "NBT_CONTROLLER_FD=8"}};
    for (unsigned i = 0; launch && !strcmp(launch, "user") && i < 4; ++i) {
        if (nbt_bridge_cap_has(grant.caps, channels[i][0])) {
            strcpy(out->text[count], channels[i][1]);
            out->list[count] = out->text[count];
            ++count;
        }
    }
    return count;
}
} // namespace process
} // namespace nbt
