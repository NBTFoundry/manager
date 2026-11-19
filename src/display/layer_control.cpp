// SPDX-License-Identifier: MPL-2.0
#include "nbt/display/layer_manager.hpp"

#include "nbt/core/system.hpp"
#include "nbt/core/unique_fd.hpp"
#include "nbt/process/child_spawn.hpp"

#include <errno.h>
#include <fcntl.h>
#include <memory>
#include <new>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" char **environ;

namespace nbt {
namespace display {
namespace {

const unsigned kControlMs = 3000, kHookMs = 250, kArgumentsMax = 9;
WaitHook waitHook = NULL;
void *waitContext = NULL;

const char *controlPath() {
#ifdef NBT_TESTING
    const char *path = getenv("NBT_TEST_LAYER_MANAGER");
    if (path) {
        return path;
    }
#endif
    return "/opt/sys/bin/LayerManagerControl";
}

// Reads the child's output until it closes it or the deadline passes.
bool readOutput(int fd, uint64_t deadline, char *output, size_t size) {
    size_t used = 0;
    for (;;) {
        if (waitHook) {
            waitHook(waitContext);
        }
        const core::Result<uint64_t> now = core::monotonicMs();
        if (!now.get() || now.value() >= deadline) {
            return false;
        }
        const uint64_t left = deadline - now.value() < kHookMs ? deadline - now.value() : kHookMs;
        struct timeval wait = {0, static_cast<suseconds_t>(left * 1000)};
        fd_set readers;
        FD_ZERO(&readers);
        FD_SET(fd, &readers);
        const int ready = select(fd + 1, &readers, NULL, NULL, &wait);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready < 0) {
            return false;
        }
        if (ready == 0) {
            continue; // the deadline is checked at the top
        }
        char chunk[256];
        const ssize_t count = read(fd, chunk, sizeof(chunk));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return count == 0;
        }
        const size_t keep = static_cast<size_t>(count) < size - 1 - used
                                ? static_cast<size_t>(count)
                                : size - 1 - used;
        memcpy(output + used, chunk, keep);
        used += keep;
        output[used] = 0;
    }
}

} // namespace

void setWaitHook(WaitHook hook, void *context) {
    waitHook = hook;
    waitContext = context;
}

int control(const char *const args[], char *output, size_t size) {
    char *argv[kArgumentsMax + 2];
    unsigned count = 0;
    argv[count++] = const_cast<char *>(controlPath());
    for (; args && args[count - 1] && count <= kArgumentsMax; ++count) {
        argv[count] = const_cast<char *>(args[count - 1]);
    }
    argv[count] = NULL;
    if (!output || !size || (args && args[count - 1])) {
        errno = EINVAL;
        return -1;
    }
    output[0] = 0;
    int ends[2];
    if (pipe(ends)) {
        return -1;
    }
    core::UniqueFd reader(ends[0]), writer(ends[1]);
    core::UniqueFd null(open("/dev/null", O_RDONLY));
    const core::Result<uint64_t> start = core::monotonicMs();
    if (!null.valid() || !start.get() || fcntl(reader.get(), F_SETFD, FD_CLOEXEC) ||
        reader.get() >= FD_SETSIZE) {
        return -1;
    }
    // LayerManagerControl does not use EGL/GLESv2/KD; omit LD_PRELOAD so it
    // does not initialize the HMI graphics stack on every poll.
    char *const *childEnv = environ;
    std::unique_ptr<char *[]> filteredEnv;
    if (getenv("LD_PRELOAD") && environ) {
        size_t envCount = 0;
        while (environ[envCount]) {
            ++envCount;
        }
        filteredEnv.reset(new (std::nothrow) char *[envCount + 1]);
        if (!filteredEnv) {
            errno = ENOMEM;
            return -1;
        }
        size_t out = 0;
        for (size_t i = 0; i < envCount; ++i) {
            if (strncmp(environ[i], "LD_PRELOAD=", 11)) {
                filteredEnv[out++] = environ[i];
            }
        }
        filteredEnv[out] = NULL;
        childEnv = filteredEnv.get();
    }
    const int fds[3] = {null.get(), writer.get(), writer.get()};
    pid_t pid = 0;
    const int error = process::spawnChild(argv[0], argv, childEnv, fds, 3, &pid);
    writer.reset();
    if (error) {
        errno = error;
        return -1;
    }
    if (!readOutput(reader.get(), start.value() + kControlMs, output, size)) {
        (void)kill(pid, SIGKILL);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

} // namespace display
} // namespace nbt
