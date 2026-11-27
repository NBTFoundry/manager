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
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

namespace nbt {
namespace process {
namespace {

// A connection to the mixer listening there, or -1.
int reach(const char *address) {
    char host[32];
    const char *colon = strrchr(address, ':');
    struct sockaddr_in where;
    if (!colon || static_cast<size_t>(colon - address) >= sizeof(host)) {
        return -1;
    }
    memcpy(host, address, static_cast<size_t>(colon - address));
    host[colon - address] = 0;
    memset(&where, 0, sizeof(where));
    where.sin_family = AF_INET;
    where.sin_port = htons(static_cast<uint16_t>(atoi(colon + 1)));
    const int fd = inet_aton(host, &where.sin_addr) ? socket(AF_INET, SOCK_STREAM, 0) : -1;
    if (fd >= 0 && connect(fd, reinterpret_cast<struct sockaddr *>(&where), sizeof(where))) {
        close(fd);
        return -1;
    }
    return fd;
}

// Whether a mixer listens there (it drops this connection at once).
bool answers(const char *address) {
    const int fd = reach(address);
    if (fd >= 0) {
        close(fd);
    }
    return fd >= 0;
}

// The mixer keeps the audio connection it was started with (its environment).
// Where the manager notes it, to tell when a launch wants another: RAM, gone
// with the mixer at a restart of the head unit.
const char *routePath() {
#ifdef NBT_TESTING
    return getenv("NBT_TEST_MIXER_ROUTE");
#elif defined(__QNXNTO__)
    return "/dev/shmem/nbt-mixer-route";
#else
    return NULL;
#endif
}

// "NBT_AUDIO_CONN=0x1d0" as the launch has it, or "" for an application's default.
const char *routeWanted(const nbt_app_environment *environment) {
    static const char kName[] = "NBT_AUDIO_CONN=";
    for (size_t i = 0; environment->list[i]; ++i) {
        if (!strncmp(environment->list[i], kName, sizeof(kName) - 1)) {
            return environment->list[i] + sizeof(kName) - 1;
        }
    }
    return "";
}

bool routeNoted(const char *wanted) {
    char noted[24] = "";
    const char *path = routePath();
    if (!path || !path[0]) {
        return true; // nowhere to note it: the mixer stays, as before
    }
    FILE *in = fopen(path, "r");
    if (in) {
        if (!fgets(noted, sizeof(noted), in)) {
            noted[0] = 0;
        }
        fclose(in);
        noted[strcspn(noted, "\n")] = 0;
    }
    return !strcmp(noted, wanted);
}

void noteRoute(const char *wanted) {
    const char *path = routePath();
    FILE *out = path && path[0] ? fopen(path, "w") : NULL;
    if (out) {
        fprintf(out, "%s\n", wanted);
        fclose(out);
    }
}

// Asks a mixer nobody plays through to end (the SDK's "NBTMIXE": "1" it ends,
// "0" somebody plays; an older mixer gives no answer and stays).
bool endMixer(const char *address) {
    const int fd = reach(address);
    char reply[10];
    size_t have = 0;
    struct timeval limit = {1, 0};
    if (fd < 0) {
        return false;
    }
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit));
    if (send(fd, "NBTMIXE\n", 8, 0) == 8) {
        while (have < sizeof(reply)) {
            const ssize_t n = recv(fd, reply + have, sizeof(reply) - have, 0);
            if (n <= 0) {
                break;
            }
            have += static_cast<size_t>(n);
        }
    }
    close(fd);
    if (have != sizeof(reply) || memcmp(reply, "NBTMIXE 1\n", sizeof(reply))) {
        return false;
    }
    for (unsigned tries = 0; tries < 50 && answers(address); ++tries) {
        const struct timespec pause = {0, 20000000};
        nanosleep(&pause, NULL);
    }
    return !answers(address);
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
    const char *wanted = routeWanted(environment);
    bool running = answers(address);
    // The Native audio setting changed since that mixer started: a new one,
    // unless an application still plays through it.
    if (running && !routeNoted(wanted)) {
        if (endMixer(address)) {
            nbt_logf("audio mixer: ended for another audio connection (%s)",
                     wanted[0] ? wanted : "default");
            running = false;
        } else {
            nbt_logf("audio mixer: still in use; the audio connection changes once "
                     "applications with sound have stopped");
        }
    }
    if (!running) {
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
        noteRoute(wanted);
    }
    snprintf(environment->mixer, sizeof(environment->mixer), "NBT_AUDIO_MIXER=%s", address);
    environment->list[count] = environment->mixer;
    environment->list[count + 1] = NULL;
    return true;
}

} // namespace process
} // namespace nbt
