// SPDX-License-Identifier: MPL-2.0
#include "nbt/gamepad/service.hpp"
#include "nbt/log.h"
#include "nbt/storage/byte_lock.hpp"
#include "nbt/util.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
namespace nbt {
namespace gamepad {
namespace {
// The car never logged why the service did not start (dev86, dev89).
bool failed(const char *step) {
    nbt_logf("gamepad: %s failed (errno %d)", step, errno);
    return false;
}
/* Non-blocking and close-on-exec, read back. fcntl's set commands promise only
 * "a value other than -1" on success: on the car a socket's F_SETFL with the
 * flags F_GETFL gave returned neither 0 nor -1 (errno 0, log 2026-11-18). */
bool nonBlockingPrivate(int fd, const char *step) {
    const int flags = fcntl(fd, F_GETFL);
    if (flags == -1 ||
        ((flags & O_NONBLOCK) == 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) ||
        fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
        return failed(step);
    }
    const int now = fcntl(fd, F_GETFL), fdFlags = fcntl(fd, F_GETFD);
    if (now == -1 || fdFlags == -1 || !(now & O_NONBLOCK) || !(fdFlags & FD_CLOEXEC)) {
        nbt_logf("gamepad: %s not applied (flags %#x, fd flags %#x, errno %d)", step,
                 static_cast<unsigned>(now), static_cast<unsigned>(fdFlags), errno);
        return false;
    }
    return true;
}
} // namespace
Service::Service()
    : mutex_(), socket_(), lock_(), usb_(), fs_(NULL), thread_(), started_(false), stopping_(false),
      connected_(false), retryAt_(0), sentAt_(0), sequence_(0), published_(), warned_(0), state_(),
      gip_(), report_() {
}
Service::~Service() {
    stop();
}
bool Service::socket(int &host, int &child) {
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, pair)) {
        return failed("socketpair");
    }
    core::UniqueFd a(pair[0]), b(pair[1]);
    for (unsigned i = 0; i < 2; ++i) {
        if (!nonBlockingPrivate(pair[i], "socket flags")) {
            return false;
        }
    }
    host = a.release();
    child = b.release();
    return true;
}
bool Service::start(nbt_fs *fs, int fd, std::unique_ptr<UsbTransport> usb, const char *statePath) {
    core::UniqueFd incoming(fd);
    if (started_ || !fs || !usb || fd < 0) {
        nbt_logf("gamepad: service not started (%s)",
                 !usb ? "no USB on this system" : "invalid arguments");
        return false;
    }
    // The descriptor is socket()'s datagram end; its type is not checked again
    // (getsockopt(SO_TYPE) had never run on the car).
    if (!nonBlockingPrivate(fd, "service socket flags")) {
        return false;
    }
    lock_.reset(nbt_fs_open(fs, NBT_FS_GAMEPAD_LOCK, "manager", NULL, NBT_FS_OPEN_LOCK));
    if (!lock_.valid()) {
        return failed("opening state/manager/gamepad.lock");
    }
    fs_ = fs;
    stopping_ = false;
    retryAt_ = sentAt_ = 0;
    if (statePath && !published_.open(statePath)) {
        (void)failed("opening the shared pad state (applications get no pad)");
    }
    socket_ = std::move(incoming);
    usb_ = std::move(usb);
    const int threadError = pthread_create(&thread_, NULL, &entry, this);
    if (threadError) {
        errno = threadError;
        (void)failed("service thread");
        socket_.reset();
        lock_.reset();
        usb_.reset();
        return false;
    }
    started_ = true;
    nbt_logf("gamepad: service ready for the GUI and the applications");
    return true;
}
void Service::stop() {
    if (!started_) {
        return;
    }
    {
        core::LockGuard guard(mutex_);
        stopping_ = true;
    }
    NBT_CHECK(pthread_join(thread_, NULL) == 0);
    started_ = false;
    usb_.reset();
    socket_.reset();
    lock_.reset();
}
Service::Report Service::report() const {
    core::LockGuard guard(mutex_);
    return report_;
}
void Service::refresh(Family family) {
    Report r;
    memset(&r, 0, sizeof(r));
    r.running = true;
    r.owner = connected_;
    r.lockRefused = (warned_ & kOwnershipWarned) != 0;
    r.family = connected_ ? family : Unsupported;
    if (r.family != Unsupported) {
        r.vendor = usb_->vendor();
        r.product = usb_->product();
    }
    r.problem = usb_->problem();
    core::LockGuard guard(mutex_);
    report_ = r;
}
void *Service::entry(void *self) {
    static_cast<Service *>(self)->run();
    return NULL;
}
bool Service::acquire() {
    const bool owned = storage::ByteLock::set(lock_.get(), F_WRLCK, 0);
    warn(kOwnershipWarned, owned, "USB ownership lock (another process may hold the USB)");
    if (!owned) {
        return false;
    }
    if (!usb_->start()) {
        NBT_CHECK(storage::ByteLock::set(lock_.get(), F_UNLCK, 0));
        return false;
    }
    connected_ = true;
    nbt_logf("gamepad: USB service acquired by the GUI");
    return true;
}
void Service::disconnect() {
    if (connected_) {
        usb_->stop();
        connected_ = false;
        NBT_CHECK(storage::ByteLock::set(lock_.get(), F_UNLCK, 0));
        nbt_logf("gamepad: USB service detached");
    }
    memset(&state_, 0, sizeof(state_));
}
void Service::warn(unsigned bit, bool ok, const char *step) {
    if (ok) {
        warned_ &= ~bit;
    } else if (!(warned_ & bit)) { // once per failure streak: retried every 5 to 500 ms
        warned_ |= bit;
        (void)failed(step);
    }
}
void Service::deliver(uint64_t now) {
    uint8_t bytes[NBT_GAMEPAD_WIRE_SIZE];
    state_.sequence = sequence_++;
    published_.publish(state_, now);
    if (nbt_gamepad_wire_encode(&state_, bytes, sizeof(bytes)) > 0) {
        const ssize_t n = send(socket_.get(), bytes, sizeof(bytes), 0);
        // A full datagram receiver gives ENOBUFS on BSD-derived stacks such as
        // QNX's io-pkt: a reader that falls behind, not a closed socket.
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR &&
            errno != ENOBUFS) {
            (void)failed("sending to the GUI; service stopped");
            core::LockGuard guard(mutex_);
            stopping_ = true;
        }
    }
    sentAt_ = now;
}
void Service::run() {
    Family family = Unsupported;
    for (;;) {
        {
            core::LockGuard guard(mutex_);
            if (stopping_) {
                break;
            }
        }
        uint64_t now = 0;
        if (nbt_monotonic_ms(&now)) {
            break;
        }
        if (!connected_ && now >= retryAt_) {
            (void)acquire();
            retryAt_ = now + 500;
        }
        bool changed = false;
        if (connected_) {
            uint8_t bytes[64];
            size_t size = 0;
            const int result = usb_->poll(bytes, size);
            if (result < 0) {
                disconnect();
                family = Unsupported;
                changed = true;
            } else {
                if (family != usb_->family()) {
                    family = usb_->family();
                    gip_.reset(usb_->vendor(), usb_->product());
                    // The PS4's own first-player blue; cosmetic, so a failure only logs.
                    uint8_t led[64];
                    if (family == DualShock4 &&
                        !usb_->write(led, dualShock4LightBar(led, 0x00, 0x00, 0x40))) {
                        nbt_logf("gamepad: DualShock 4 light bar not set");
                    }
                }
                if (result > 0) {
                    changed = (family == Gip ? gip_.feed(bytes, size, state_)
                                             : decodeSony(family, bytes, size, state_)) > 0;
                }
                if (family == Gip && (size = gip_.output(bytes)) && !usb_->write(bytes, size)) {
                    disconnect();
                    family = Unsupported;
                    changed = true;
                }
            }
        }
        if (changed || now >= sentAt_ + 100) {
            deliver(now);
        }
        refresh(family);
        // Applications cannot send commands over this input-only capability.
        uint8_t discard[1];
        for (unsigned i = 0; i < 8; ++i) {
            if (recv(socket_.get(), discard, sizeof(discard), 0) < 0) {
                break;
            }
        }
        (void)poll(NULL, 0, 5);
    }
    disconnect();
    core::LockGuard guard(mutex_);
    memset(&report_, 0, sizeof(report_));
}
} // namespace gamepad
} // namespace nbt
