#include "nbt/gui/worker_channel.hpp"

#include "nbt/core/system.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

namespace nbt {
namespace gui {
namespace {

const uint64_t kPingIdleMs = 1000;
const unsigned kPingTimeoutMs = 2000;
const uint64_t kAliveMs = 3000;

uint64_t nowOr(uint64_t fallback) {
    const core::Result<uint64_t> now = core::monotonicMs();
    return now.get() ? now.value() : fallback;
}

bool privatePipe(int (&ends)[2]) {
    if (pipe(ends)) {
        ends[0] = ends[1] = -1;
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        const int flags = fcntl(ends[i], F_GETFL);
        if (flags < 0 || fcntl(ends[i], F_SETFL, flags | O_NONBLOCK) ||
            fcntl(ends[i], F_SETFD, FD_CLOEXEC)) {
            close(ends[0]);
            close(ends[1]);
            ends[0] = ends[1] = -1;
            return false;
        }
    }
    return true;
}

} // namespace

WorkerChannel::WorkerChannel(WorkerClient &client)
    : client_(client), mutex_(), thread_(), started_(false), stopping_(false), connected_(false),
      phase_(Idle), request_(), ok_(false), answered_(false), lastOkMs_(0), deadlineMs_(0) {
    reply_[0] = 0;
    request_.command[0] = 0;
    request_.timeoutMs = 0;
    toThread_[0] = toThread_[1] = toUi_[0] = toUi_[1] = -1;
}

WorkerChannel::~WorkerChannel() {
    stop();
}

core::Status WorkerChannel::start() {
    if (started_ || !client_.connected()) {
        return core::Status::error(EINVAL, "worker channel cannot start");
    }
    if (!privatePipe(toThread_) || !privatePipe(toUi_)) {
        const core::Status status = core::Status::fromErrno("worker channel pipes failed");
        stop();
        return status;
    }
    stopping_ = false;
    connected_ = true;
    phase_ = Idle;
    answered_ = false; // HELLO is not enough: a LIST must succeed before ready
    lastOkMs_ = 0;
    // Signals (SIGTERM, SIGCHLD) stay with the UI thread, which handles them.
    sigset_t all, previous;
    sigfillset(&all);
    if (pthread_sigmask(SIG_BLOCK, &all, &previous)) {
        stop();
        return core::Status::error(EINVAL, "worker channel signal mask failed");
    }
    const int error = pthread_create(&thread_, NULL, run, this);
    (void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
    if (error) {
        stop();
        return core::Status::error(error, "worker channel thread failed");
    }
    started_ = true;
    return core::Status();
}

void WorkerChannel::stop() {
    if (started_) {
        {
            core::LockGuard guard(mutex_);
            stopping_ = true;
        }
        wake(toThread_[1]);
        (void)pthread_join(thread_, NULL);
        started_ = false;
    }
    int *const fds[4] = {&toThread_[0], &toThread_[1], &toUi_[0], &toUi_[1]};
    for (size_t i = 0; i < 4; ++i) {
        if (*fds[i] >= 0) {
            close(*fds[i]);
            *fds[i] = -1;
        }
    }
    connected_ = false;
    phase_ = Idle;
}

bool WorkerChannel::submit(const Request &request) {
    {
        core::LockGuard guard(mutex_);
        if (!started_ || !connected_ || phase_ != Idle) {
            return false;
        }
        request_ = request;
        deadlineMs_ = nowOr(0) + request.timeoutMs + kPingTimeoutMs; // a ping may be ahead
        phase_ = Pending;
    }
    wake(toThread_[1]);
    return true;
}

bool WorkerChannel::take(char *reply, size_t size, bool *ok, char *command, size_t commandSize) {
    drain(toUi_[0]);
    core::LockGuard guard(mutex_);
    if (phase_ != Done || !reply || !size || !ok) {
        return false;
    }
    snprintf(reply, size, "%s", reply_);
    if (command && commandSize) {
        snprintf(command, commandSize, "%s", request_.command);
    }
    *ok = ok_;
    phase_ = Idle;
    return true;
}

bool WorkerChannel::busy() const {
    core::LockGuard guard(mutex_);
    return phase_ != Idle;
}

bool WorkerChannel::connected() const {
    core::LockGuard guard(mutex_);
    return connected_;
}

bool WorkerChannel::alive(uint64_t nowMs) const {
    core::LockGuard guard(mutex_);
    if (!connected_ || !answered_) {
        return false;
    }
    if (nowMs >= lastOkMs_ && nowMs - lastOkMs_ < kAliveMs) {
        return true;
    }
    return (phase_ == Pending || phase_ == Running) && nowMs < deadlineMs_;
}

void *WorkerChannel::run(void *self) {
    static_cast<WorkerChannel *>(self)->loop();
    return NULL;
}

void WorkerChannel::loop() {
    uint64_t lastActivityMs = nowOr(0);
    bool pingNow = true; // readiness needs a LIST answered first
    for (;;) {
        Request request;
        bool haveRequest = false;
        {
            core::LockGuard guard(mutex_);
            if (stopping_) {
                return;
            }
            if (!connected_) {
                break;
            }
            if (phase_ == Pending && !pingNow) { // readiness: the first LIST goes first
                request = request_;
                phase_ = Running;
                haveRequest = true;
            }
        }
        if (haveRequest) {
            char reply[kReplyMax];
            const bool ok = client_
                                .request(request.command, core::Span<char>(reply, sizeof(reply)),
                                         request.timeoutMs)
                                .ok();
            if (!ok) {
                client_.terminate();
            }
            lastActivityMs = nowOr(lastActivityMs);
            {
                core::LockGuard guard(mutex_);
                snprintf(reply_, sizeof(reply_), "%s", ok ? reply : "");
                ok_ = ok;
                connected_ = ok;
                if (ok) {
                    lastOkMs_ = lastActivityMs;
                }
                phase_ = Done;
            }
            wake(toUi_[1]);
            continue;
        }
        const uint64_t now = nowOr(lastActivityMs);
        if (pingNow || (now >= lastActivityMs && now - lastActivityMs >= kPingIdleMs)) {
            pingNow = false;
            if (!ping(now)) {
                break;
            }
            lastActivityMs = nowOr(now);
            continue;
        }
        const uint64_t waitMs = kPingIdleMs - (now >= lastActivityMs ? now - lastActivityMs : 0);
        fd_set readers;
        FD_ZERO(&readers);
        FD_SET(toThread_[0], &readers);
        struct timeval wait;
        wait.tv_sec = static_cast<time_t>(waitMs / 1000);
        wait.tv_usec = static_cast<suseconds_t>(waitMs % 1000) * 1000;
        const int rc = select(toThread_[0] + 1, &readers, NULL, NULL, &wait);
        if (rc < 0 && errno != EINTR) {
            client_.terminate();
            break;
        }
        drain(toThread_[0]);
    }
    lost();
}

void WorkerChannel::lost() {
    {
        core::LockGuard guard(mutex_);
        connected_ = false;
        if (phase_ == Pending || phase_ == Running) {
            reply_[0] = 0;
            ok_ = false;
            phase_ = Done;
        }
    }
    wake(toUi_[1]);
}

/* LIST while idle: the worker is alive and answering. */
bool WorkerChannel::ping(uint64_t nowMs) {
    char reply[kReplyMax];
    const bool ok =
        client_.request("LIST", core::Span<char>(reply, sizeof(reply)), kPingTimeoutMs).ok() &&
        !strncmp(reply, "APPS\t", 5);
    if (!ok) {
        client_.terminate();
    }
    core::LockGuard guard(mutex_);
    connected_ = ok;
    if (ok) {
        lastOkMs_ = nowOr(nowMs);
        answered_ = true;
    }
    return ok;
}

void WorkerChannel::wake(int fd) {
    if (fd >= 0) {
        (void)!write(fd, "w", 1); // full pipe: a wake-up is already pending
    }
}

void WorkerChannel::drain(int fd) {
    char bytes[64];
    while (fd >= 0 && read(fd, bytes, sizeof(bytes)) > 0) {
    }
}

} // namespace gui
} // namespace nbt
