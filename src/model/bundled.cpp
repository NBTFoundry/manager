// SPDX-License-Identifier: MPL-2.0
#include "nbt/bundled.h"

#include <string.h>

extern "C" {

const nbt_bundled_app nbt_bundled_apps[NBT_BUNDLED_COUNT] = {
    {"selftest_native", "manager-selftest-native", "native"},
    {"selftest_guest", "manager-selftest-guest", "guest"},
    {"selftest", "manager-selftest", "module"},
};

int nbt_app_bundled(const char *app) {
    return app && !strncmp(app, NBT_BUNDLED_PREFIX, sizeof(NBT_BUNDLED_PREFIX) - 1) ? 1 : 0;
}

} // extern "C"
