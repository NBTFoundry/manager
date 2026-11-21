// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/worker_client.hpp"

#include "nbt/core/system.hpp"
#include "nbt/process/child_spawn.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern "C" char **environ;

namespace nbt {
namespace gui {
namespace {

#ifdef NBT_TESTING
// The worker's copy stays open across exec, as the descriptors NBT-AAD maps.
core::Result<core::UniqueFd> duplicatePrivate(int fd, bool closeOnExec) {
    core::UniqueFd copy(fcntl(fd, F_DUPFD, 10));
    if (!copy.valid()) {
        return core::Result<core::UniqueFd>::failure(
            core::Status::fromErrno("duplicate descriptor failed"));
    }
    if (closeOnExec && fcntl(copy.get(), F_SETFD, FD_CLOEXEC) < 0) {
        return core::Result<core::UniqueFd>::failure(
            core::Status::fromErrno("private descriptor failed"));
    }
    return core::Result<core::UniqueFd>::success(std::move(copy));
}

#endif

} // namespace

WorkerClient::WorkerClient() : fd_(), pid_(0) {
}
WorkerClient::WorkerClient(WorkerClient &&other) : fd_(std::move(other.fd_)), pid_(other.pid_) {
    other.pid_ = 0;
}
WorkerClient &WorkerClient::operator=(WorkerClient &&other) {
    if (this == &other) {
        return *this;
    }
    close();
    fd_ = std::move(other.fd_);
    pid_ = other.pid_;
    other.pid_ = 0;
    return *this;
}
WorkerClient::~WorkerClient() {
    close();
}

#ifdef NBT_TESTING
core::Status WorkerClient::spawnWorker(const char *path, const char *testRoot) {
    close();
    if (!path) {
        return core::Status::error(EINVAL, "worker path required");
    }
    int endpoints[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, endpoints)) {
        return core::Status::fromErrno("worker socket failed");
    }
    core::UniqueFd first(endpoints[0]), second(endpoints[1]);
    core::Result<core::UniqueFd> parentResult = duplicatePrivate(first.get(), true);
    core::Result<core::UniqueFd> childResult = duplicatePrivate(second.get(), false);
    core::UniqueFd nullfd(open("/dev/null", O_RDWR));
    if (!parentResult.get() || !childResult.get() || !nullfd.valid()) {
        return core::Status::fromErrno("worker descriptors failed");
    }
    core::UniqueFd parent(std::move(parentResult.value()));
    core::UniqueFd child(std::move(childResult.value()));
    first.reset();
    second.reset();
    if (parent.get() >= FD_SETSIZE) {
        errno = EMFILE;
        return core::Status::fromErrno("worker descriptor setup failed");
    }
    char *argv[6];
    argv[0] = const_cast<char *>(path);
    argv[1] = const_cast<char *>("--control-fd");
    argv[2] = const_cast<char *>("3");
    argv[3] = NULL;
    argv[4] = NULL;
    argv[5] = NULL;
    if (testRoot) {
        argv[3] = const_cast<char *>("--root");
        argv[4] = const_cast<char *>(testRoot);
    }
    // As NBT-AAD starts its daemon: the GUI's environment, and its output (the
    // debug log while that is on) for the worker's early errors.
    const int fds[4] = {nullfd.get(), 1, 2, child.get()};
    pid_t pid = -1;
    const int spawnError = process::spawnChild(path, argv, environ, fds, 4, &pid);
    if (spawnError) {
        return core::Status::error(spawnError, "worker spawn failed");
    }
    fd_ = std::move(parent);
    pid_ = pid;
    return core::Status();
}
#endif

core::Status WorkerClient::handshake() {
    char reply[32];
    const core::Status status = request("HELLO 1", core::Span<char>(reply, sizeof(reply)), 5000);
    if (status.ok() && !strcmp(reply, "HELLO\t1")) {
        return core::Status();
    }
    close();
    return status.ok() ? core::Status::error(EPROTO, "worker handshake failed") : status;
}

core::Status WorkerClient::attach(int fd) {
    close();
    if (fd < 3 || fd >= FD_SETSIZE) {
        return core::Status::error(EINVAL, "invalid worker channel");
    }
    fd_.reset(fd);
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        const core::Status status = core::Status::fromErrno("worker channel setup failed");
        fd_.reset();
        return status;
    }
    pid_ = 0;
    return handshake();
}

#ifdef NBT_TESTING
core::Status WorkerClient::spawnTest(const char *path, const char *root) {
    core::Status status = spawnWorker(path, root);
    return status.ok() ? handshake() : status;
}
#endif

core::Status WorkerClient::fail(int code) {
    terminate();
    return core::Status::error(code, "worker channel failed");
}

core::Status WorkerClient::request(const char *command, core::Span<char> reply,
                                   unsigned timeoutMs) {
    if (!fd_.valid() || fd_.get() >= FD_SETSIZE || !command || !reply.data() || reply.size() < 2 ||
        !timeoutMs) {
        return core::Status::error(EINVAL, "invalid worker request");
    }
    const size_t length = strlen(command);
    if (!length || length > 900) {
        return core::Status::error(EINVAL, "invalid worker command");
    }
    for (size_t i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(command[i]) < 32 ||
            static_cast<unsigned char>(command[i]) > 126) {
            return core::Status::error(EINVAL, "invalid worker command byte");
        }
    }
    core::Result<uint64_t> nowResult = core::monotonicMs();
    if (!nowResult.get()) {
        return nowResult.status();
    }
    const uint64_t now = nowResult.value();
    if (now > UINT64_MAX - timeoutMs) {
        return core::Status::error(EOVERFLOW, "worker deadline overflow");
    }
    const uint64_t deadline = now + timeoutMs;
    size_t sent = 0, used = 0;
    while (sent < length + 1 || used < reply.size() - 1) {
        core::Result<uint64_t> currentResult = core::monotonicMs();
        if (!currentResult.get()) {
            return fail(currentResult.status().code());
        }
        const uint64_t current = currentResult.value();
        if (current >= deadline) {
            return fail(ETIMEDOUT);
        }
        const uint64_t waitMs = deadline - current;
        struct timeval wait;
        wait.tv_sec = static_cast<time_t>(waitMs / 1000);
        wait.tv_usec = static_cast<suseconds_t>(waitMs % 1000) * 1000;
        fd_set readfds, writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        if (sent < length + 1) {
            FD_SET(fd_.get(), &writefds);
        } else {
            FD_SET(fd_.get(), &readfds);
        }
        const int result = select(fd_.get() + 1, &readfds, &writefds, NULL, &wait);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            return fail(result ? errno : ETIMEDOUT);
        }
        if (FD_ISSET(fd_.get(), &writefds)) {
            const char *data = sent < length ? command + sent : "\n";
            const size_t remaining = sent < length ? length - sent : 1;
            const ssize_t count = send(fd_.get(), data, remaining, MSG_NOSIGNAL | MSG_DONTWAIT);
            if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue;
            }
            if (count <= 0) {
                return fail(count == 0 ? EPIPE : errno);
            }
            sent += static_cast<size_t>(count);
        } else {
            char byte;
            const ssize_t count = read(fd_.get(), &byte, 1);
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
                return fail(count == 0 ? EPIPE : errno);
            }
            if (byte == '\n') {
                reply[used] = 0;
                return core::Status();
            }
            // Bytes from 0x80 on carry the UTF-8 of file names (FILES, FILES USB), checked
            // where they are parsed; other control bytes than a tab are refused.
            if ((static_cast<unsigned char>(byte) < 32 && byte != '\t') ||
                static_cast<unsigned char>(byte) == 127) {
                return fail(EPROTO);
            }
            reply[used++] = byte;
        }
    }
    return fail(EMSGSIZE);
}

void WorkerClient::close() {
    fd_.reset();
    if (pid_ > 0) {
        const struct timespec pauseTime = {0, 100000000};
        for (unsigned i = 0; i < 60; ++i) {
            const pid_t result = waitpid(pid_, NULL, WNOHANG);
            if (result == pid_ || (result < 0 && errno == ECHILD)) {
                break;
            }
            if (result < 0 && errno != EINTR) {
                break;
            }
            nanosleep(&pauseTime, NULL);
        }
        pid_ = 0;
    }
}

void WorkerClient::terminate() {
    fd_.reset();
    if (pid_ <= 0) {
        return;
    }
    if (kill(pid_, SIGKILL) && errno != ESRCH) {
        pid_ = 0;
        return;
    }
    while (waitpid(pid_, NULL, 0) < 0 && errno == EINTR) {
    }
    pid_ = 0;
}

} // namespace gui
} // namespace nbt
