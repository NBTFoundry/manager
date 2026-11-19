// SPDX-License-Identifier: MPL-2.0
#include "nbt/gamepad/relay.hpp"
#include "nbt/display/layer_manager.hpp"
#include "nbt/log.h"
#include "nbt/util.h"
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
namespace nbt {
namespace gamepad {
namespace {
bool same(const nbt_gamepad_wire_state &a, const nbt_gamepad_wire_state &b) {
    return a.connected == b.connected && a.buttons == b.buttons && a.lx == b.lx && a.ly == b.ly &&
           a.rx == b.rx && a.ry == b.ry && a.lt == b.lt && a.rt == b.rt;
}
} // namespace
Relay::Relay()
    : mutex_(), socket_(), thread_(), path_(NBT_GAMEPAD_STATE_PATH), started_(false),
      stopping_(false), active_(false), focused_(false), focusAt_(0), sentAt_(0), sequence_(0),
      sent_(), reader_() {
}
Relay::~Relay() {
    stop();
}
bool Relay::start(int fd, const char *path) {
    core::UniqueFd incoming(fd);
    if (started_ || fd < 0) {
        return false;
    }
    socket_ = std::move(incoming);
    path_ = path ? path : NBT_GAMEPAD_STATE_PATH;
    stopping_ = active_ = focused_ = false;
    focusAt_ = sentAt_ = 0;
    memset(&sent_, 0, sizeof(sent_));
    const int error = pthread_create(&thread_, NULL, &entry, this);
    if (error) {
        nbt_logf("gamepad: relay thread failed (errno %d)", error);
        socket_.reset();
        return false;
    }
    started_ = true;
    nbt_logf("gamepad: relaying the manager's pads to the application");
    return true;
}
void Relay::setActive(bool active) {
    core::LockGuard guard(mutex_);
    active_ = active;
}
void Relay::stop() {
    if (!started_) {
        return;
    }
    {
        core::LockGuard guard(mutex_);
        stopping_ = true;
    }
    NBT_CHECK(pthread_join(thread_, NULL) == 0);
    started_ = false;
    socket_.reset();
}
void *Relay::entry(void *self) {
    static_cast<Relay *>(self)->run();
    return NULL;
}
bool Relay::shown(uint64_t now) {
    if (now >= focusAt_) {
        focused_ = display::holds(display::kAppSurface) > 0;
        focusAt_ = now + 250;
    }
    return focused_;
}
void Relay::run() {
    for (;;) {
        bool active;
        {
            core::LockGuard guard(mutex_);
            if (stopping_) {
                break;
            }
            active = active_;
        }
        uint64_t now = 0;
        if (nbt_monotonic_ms(&now)) {
            break;
        }
        nbt_gamepad_wire_state state;
        memset(&state, 0, sizeof(state));
        if (active && shown(now)) {
            state = reader_.read(path_, now);
        }
        if (!same(state, sent_) || now >= sentAt_ + 100) {
            state.sequence = sequence_++;
            uint8_t bytes[NBT_GAMEPAD_WIRE_SIZE];
            if (nbt_gamepad_wire_encode(&state, bytes, sizeof(bytes)) > 0) {
                const ssize_t n = send(socket_.get(), bytes, sizeof(bytes), MSG_DONTWAIT);
                // A reader that falls behind (ENOBUFS on io-pkt) is not a closed socket.
                if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR &&
                    errno != ENOBUFS) {
                    nbt_logf("gamepad: sending to the application failed (errno %d); relay "
                             "stopped",
                             errno);
                    break;
                }
            }
            sent_ = state;
            sentAt_ = now;
        }
        (void)poll(NULL, 0, 5);
    }
}
} // namespace gamepad
} // namespace nbt
