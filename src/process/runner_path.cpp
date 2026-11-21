// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/runner_path.hpp"

#include "nbt/fs.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace process {
namespace {

char runner[NBT_PATH_MAX];

} // namespace

void setRunnerBeside(const char *self) {
    static const char kName[] = "runner";
    const char *slash = self && self[0] == '/' ? strrchr(self, '/') : NULL;
    const size_t dir = slash ? static_cast<size_t>(slash - self) + 1 : 0;
    if (!dir || dir + sizeof(kName) > sizeof(runner)) {
        runner[0] = 0;
        return;
    }
    memcpy(runner, self, dir);
    memcpy(runner + dir, kName, sizeof(kName));
}

const char *runnerPath() {
#ifdef NBT_TESTING
    const char *test = getenv("NBT_TEST_RUNNER");
    if (test && test[0] == '/') {
        return test;
    }
#endif
    return runner[0] ? runner : NULL;
}

void withoutMissingRunner(nbt_bridge_provider *provider) {
    const char *path = runnerPath();
    if (!provider || (path && !access(path, X_OK))) {
        return;
    }
    for (unsigned i = 0; i < provider->count; ++i) {
        char caps[NBT_BRIDGE_CAPS_MAX + 1];
        nbt_bridge_caps_without(provider->versions[i].caps, "runner", caps);
        memcpy(provider->versions[i].caps, caps, sizeof(caps));
    }
}

} // namespace process
} // namespace nbt
