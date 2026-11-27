// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/audio_mixer.hpp"

#include "nbt/log.h"
#include "nbt/process/runner_path.hpp"
#include "nbt/process/spawn.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

namespace nbt {
namespace process {
namespace {

// Whether a mixer listens there (it drops this connection at once).
bool answers(const char *address) {
    char host[32];
    const char *colon = strrchr(address, ':');
    struct sockaddr_in where;
    if (!colon || static_cast<size_t>(colon - address) >= sizeof(host)) {
        return false;
    }
    memcpy(host, address, static_cast<size_t>(colon - address));
    host[colon - address] = 0;
    memset(&where, 0, sizeof(where));
    where.sin_family = AF_INET;
    where.sin_port = htons(static_cast<uint16_t>(atoi(colon + 1)));
    const int fd = inet_aton(host, &where.sin_addr) ? socket(AF_INET, SOCK_STREAM, 0) : -1;
    const bool up =
        fd >= 0 && !connect(fd, reinterpret_cast<struct sockaddr *>(&where), sizeof(where));
    if (fd >= 0) {
        close(fd);
    }
    return up;
}

} // namespace

const char *mixerAddress() {
#ifdef NBT_TESTING
    const char *test = getenv("NBT_TEST_MIXER_ADDRESS");
    if (test && test[0]) {
        return test;
    }
#endif
    return "127.0.0.1:42863";
}

bool mixerState(MixerState *out) {
    const char *path = NULL;
#ifdef NBT_TESTING
    path = getenv("NBT_TEST_MIXER_STATE");
#endif
#ifdef __QNXNTO__
    if (!path || !path[0]) {
        path = "/dev/shmem/nbt-mixer-level";
    }
#endif
    FILE *in = path && path[0] ? fopen(path, "r") : NULL;
    if (!in) {
        return false;
    }
    MixerState state = {0, 0, 0};
    const int fields = fscanf(in, "%d %u %u", &state.level, &state.gaps, &state.grown);
    fclose(in);
    if (fields < 1 || state.level < 0 || state.level > 9) {
        return false;
    }
    if (fields < 2) {
        state.gaps = 0;
    }
    if (fields < 3) {
        state.grown = 0;
    }
    *out = state;
    return true;
}

bool grantMixer(nbt_app_environment *environment) {
    const char *runner = runnerPath();
    size_t count = 0; // the builder ends the list with NULL
    while (environment->list[count]) {
        ++count;
    }
    const size_t capacity = sizeof(environment->list) / sizeof(environment->list[0]);
    if (!runner || access(runner, X_OK) || count + 3 > capacity) { // and NBT_SURFACE after
        return false;
    }
    const char *address = mixerAddress();
    if (!answers(address)) {
        spawn::ChildPlan plan;
        pid_t intermediate = 0;
        plan.executable = runner;
        plan.arguments[0] = const_cast<char *>(runner);
        plan.arguments[1] = const_cast<char *>("--mixer");
        plan.arguments[2] = const_cast<char *>(address);
        plan.arguments[3] = NULL;
        plan.environment = environment->list;
        const core::Status started = spawn::detach(plan, &intermediate);
        if (!started.ok()) {
            nbt_logf("audio mixer: cannot start (errno %d)", started.code());
            return false;
        }
    }
    snprintf(environment->mixer, sizeof(environment->mixer), "NBT_AUDIO_MIXER=%s", address);
    environment->list[count] = environment->mixer;
    environment->list[count + 1] = NULL;
    return true;
}

} // namespace process
} // namespace nbt
