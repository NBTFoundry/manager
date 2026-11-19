#include "nbt/worker/wifi_direct_job.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

namespace nbt {
namespace worker {

WifiDirectJob::WifiDirectJob(WifiDirectController &wfd)
    : wfd_(wfd), mutex_(), thread_(), joinable_(false), phase_(Idle), startMs_(0), ok_(false),
      result_() {
    iface_[0] = 0;
    wake_[0] = wake_[1] = -1;
}

WifiDirectJob::~WifiDirectJob() {
    join();
    for (int i = 0; i < 2; ++i) {
        if (wake_[i] >= 0) {
            close(wake_[i]);
        }
    }
}

void WifiDirectJob::initialize() {
    wfd_.initialize();
    if (wake_[0] < 0 && !pipe(wake_)) {
        for (int i = 0; i < 2; ++i) {
            const int flags = fcntl(wake_[i], F_GETFL);
            (void)fcntl(wake_[i], F_SETFL, flags < 0 ? O_NONBLOCK : flags | O_NONBLOCK);
            (void)fcntl(wake_[i], F_SETFD, FD_CLOEXEC);
        }
    }
}

bool WifiDirectJob::required(const char *iface) const {
    core::LockGuard guard(mutex_);
    return phase_ != Idle || wfd_.requiredForInterface(iface); // busy: start() refuses
}

bool WifiDirectJob::start(const char *iface, uint64_t nowMs) {
    join(); // a finished preparation's thread
    {
        core::LockGuard guard(mutex_);
        if (phase_ != Idle || !iface || strlen(iface) >= sizeof(iface_)) {
            return false;
        }
        snprintf(iface_, sizeof(iface_), "%s", iface);
        startMs_ = nowMs;
        result_.clear();
        phase_ = Running;
    }
    // Signals stay with the worker's loop.
    sigset_t all, previous;
    sigfillset(&all);
    (void)pthread_sigmask(SIG_BLOCK, &all, &previous);
    const int error = pthread_create(&thread_, NULL, run, this);
    (void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
    core::LockGuard guard(mutex_);
    if (error) {
        phase_ = Idle;
        errno = error;
        return false;
    }
    joinable_ = true;
    return true;
}

void *WifiDirectJob::run(void *self) {
    WifiDirectJob &job = *static_cast<WifiDirectJob *>(self);
    char iface[sizeof(job.iface_)];
    uint64_t startMs;
    {
        core::LockGuard guard(job.mutex_);
        memcpy(iface, job.iface_, sizeof(iface));
        startMs = job.startMs_;
    }
    // The controller is this thread's alone while the phase is Running.
    ReadyWifiCredentials wifi;
    const bool ok = job.wfd_.prepareSession(iface, startMs, &wifi);
    {
        core::LockGuard guard(job.mutex_);
        job.ok_ = ok;
        job.result_.managed = wifi.managed;
        memcpy(job.result_.ssid, wifi.ssid, sizeof(wifi.ssid));
        (void)job.result_.pass.set(wifi.pass.data());
        job.result_.source = wifi.source;
        job.phase_ = Done;
    }
    wifi.clear();
    if (job.wake_[1] >= 0) {
        (void)!write(job.wake_[1], "w", 1);
    }
    return NULL;
}

bool WifiDirectJob::finish(bool *ok, ReadyWifiCredentials *out) {
    char drained[16];
    while (wake_[0] >= 0 && read(wake_[0], drained, sizeof(drained)) > 0) {
    }
    {
        core::LockGuard guard(mutex_);
        if (phase_ != Done || !ok || !out) {
            return false;
        }
        *ok = ok_;
        out->clear();
        out->managed = result_.managed;
        memcpy(out->ssid, result_.ssid, sizeof(out->ssid));
        (void)out->pass.set(result_.pass.data());
        out->source = result_.source;
        result_.clear();
        phase_ = Idle;
    }
    join();
    return true;
}

bool WifiDirectJob::running() const {
    core::LockGuard guard(mutex_);
    return phase_ != Idle;
}

void WifiDirectJob::join() {
    bool joinable;
    {
        core::LockGuard guard(mutex_);
        joinable = joinable_ && phase_ != Running;
        if (joinable) {
            joinable_ = false;
        }
    }
    if (joinable) {
        (void)pthread_join(thread_, NULL);
    }
}

int WifiDirectJob::fd() const {
    core::LockGuard guard(mutex_);
    if (phase_ == Running) {
        return wake_[0];
    }
    return wfd_.fd();
}

void WifiDirectJob::sets(fd_set *readers, int *highest) const {
    core::LockGuard guard(mutex_);
    if (phase_ != Running) {
        wfd_.sets(readers, highest);
    }
    if (phase_ != Idle && wake_[0] >= 0) { // a preparation's end wakes the loop at once
        FD_SET(wake_[0], readers);
        *highest = wake_[0] > *highest ? wake_[0] : *highest;
    }
}

void WifiDirectJob::poll(uint64_t nowMs) {
    core::LockGuard guard(mutex_);
    if (phase_ == Idle) {
        wfd_.poll(nowMs);
    }
}

bool WifiDirectJob::enabledByUs() const {
    core::LockGuard guard(mutex_);
    return phase_ == Idle && wfd_.enabledByUs();
}

void WifiDirectJob::restore(uint64_t nowMs) {
    core::LockGuard guard(mutex_);
    if (phase_ == Idle) {
        wfd_.restore(nowMs);
    }
}

void WifiDirectJob::shutdown() {
    for (;;) { // a running preparation ends within its 10 s
        {
            core::LockGuard guard(mutex_);
            if (phase_ != Running) {
                break;
            }
        }
        const struct timespec pause = {0, 20000000};
        nanosleep(&pause, NULL);
    }
    join();
    core::LockGuard guard(mutex_);
    result_.clear();
    phase_ = Idle;
    wfd_.shutdown();
}

} // namespace worker
} // namespace nbt
