#include "nbt/most/controller_feed.hpp"

#include "nbt/capture/protocol.h"
#include "nbt/most/ccs.hpp"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>

namespace nbt {
namespace most {
namespace {

const unsigned kController = 0xe5;
const unsigned kOpStatus = 0xc;
const uint16_t kFavouriteKey = NBT_CAPTURE_FBLOCK_CENTER_STACK << 8 | 0x0c; // 0x15.0C
const uint32_t kUp = NBT_CONTROLLER_TILT_UP, kDown = NBT_CONTROLLER_TILT_DOWN;
const uint32_t kLeft = NBT_CONTROLLER_TILT_LEFT, kRight = NBT_CONTROLLER_TILT_RIGHT;

/* CCS35up tilt: a 12-step clock, 1 up, 4 right, 7 down, 10 left, diagonals
 * 2, 5, 8 and 11 (docs/controller-input.md). */
uint32_t clockTilt(unsigned v) {
    static const uint32_t table[13] = {0,     kUp,   kUp | kRight,  kRight, kRight, kRight | kDown,
                                       kDown, kDown, kDown | kLeft, kLeft,  kLeft,  kLeft | kUp,
                                       kUp};
    return v <= 12 ? table[v] : 0;
}

uint32_t with(uint32_t keys, uint32_t bit, bool on) {
    return on ? keys | bit : keys & ~bit;
}

} // namespace

ControllerFeed::ControllerFeed()
    : lock_(), fd_(-1), config_(), state_(), position_(-1), ccsPosition_(-1), knobMotion_(0),
      knobKeys_(0), ccsMotion_(0), ccsKeys_(0), favourites_(0), paused_(false), shown_(false) {
    Ccs::defaults(&config_);
}

void ControllerFeed::start(int fd, const nbt_ccs_config *config) {
    core::LockGuard guard(lock_);
    if (Ccs::valid(config)) {
        config_ = *config;
    }
    fd_ = fd;
    sendLocked();
}

size_t ControllerFeed::controls(uint16_t *keys, size_t count, size_t max) const {
    uint16_t wanted[NBT_CCS_INSTANCES_MAX + 1];
    size_t n = 0;
    if (fd_ < 0) {
        return count; // no feed: only the application's own policy registers
    }
    for (size_t i = 0; i < config_.instance_count && i < NBT_CCS_INSTANCES_MAX; ++i) {
        wanted[n++] = static_cast<uint16_t>(kController << 8 | config_.instances[i]);
    }
    wanted[n++] = kFavouriteKey;
    for (size_t i = 0; i < n && count < max; ++i) {
        bool present = false;
        for (size_t j = 0; j < count; ++j) {
            present = present || keys[j] == wanted[i];
        }
        if (!present) {
            keys[count++] = wanted[i];
        }
    }
    return count;
}

uint32_t ControllerFeed::slotKey(unsigned slot, bool ccs) const {
    static const uint32_t others[] = {NBT_CONTROLLER_OPTION, NBT_CONTROLLER_COM,
                                      NBT_CONTROLLER_MEDIA, NBT_CONTROLLER_NAV, NBT_CONTROLLER_MAP};
    const unsigned menu = ccs ? config_.ccs_menu_slot : config_.knob_menu_slot;
    const unsigned back = ccs ? config_.ccs_back_slot : config_.knob_back_slot;
    if (slot == menu) {
        return NBT_CONTROLLER_MENU;
    }
    if (slot == back) {
        return NBT_CONTROLLER_BACK;
    }
    // The other keys in BMW's order: slots 0-6 are MENU BACK OPTION COM MEDIA NAV MAP.
    const unsigned rank = slot - (menu < slot ? 1u : 0u) - (back < slot ? 1u : 0u);
    return rank < 5 ? others[rank] : 0;
}

bool ControllerFeed::setKeys(uint32_t *field, uint32_t keys) {
    const bool changed = *field != keys;
    *field = keys;
    return changed;
}

bool ControllerFeed::rotate(int *last, int position) {
    const int previous = *last;
    *last = position;
    if (previous < 0) {
        return false;
    }
    int delta = static_cast<int>(static_cast<int16_t>(static_cast<uint16_t>(position - previous)));
    if (config_.invert) {
        delta = -delta;
    }
    if (delta == 0 || delta > NBT_CCS_MAX_STEP || delta < -NBT_CCS_MAX_STEP) {
        return false; // no turn, or a counter re-base
    }
    state_.rotation =
        static_cast<int32_t>(static_cast<uint32_t>(state_.rotation) + static_cast<uint32_t>(delta));
    return true;
}

bool ControllerFeed::knob(unsigned fkt, const unsigned char *d, size_t n) {
    if (fkt == 0xc46) {
        if (n < 5) {
            return false;
        }
        const bool turned = rotate(&position_, d[1] << 8 | d[2]);
        const uint32_t motion = (d[3] ? NBT_CONTROLLER_PUSH : 0u) | clockTilt(d[4]);
        return setKeys(&knobMotion_, motion) || turned;
    }
    if (fkt != 0xc47 && fkt != 0xc4f) {
        return false;
    }
    uint32_t keys = 0;
    for (size_t i = 0; i < n && i < NBT_CCS_SLOTS; ++i) {
        keys |= d[i] ? slotKey(static_cast<unsigned>(i), false) : 0u;
    }
    return setKeys(&knobKeys_, keys);
}

bool ControllerFeed::ccs(unsigned fkt, const unsigned char *d, size_t n) {
    if (fkt == 0xc10) {
        const size_t need = config_.ccs_instance == 1 ? 7 : 8;
        if (n < need || d[2] == 0xff || d[need - 2] != 0x1e) {
            ccsPosition_ = -1;
            return false;
        }
        if (d[need - 1]) {
            ccsPosition_ = d[3] << 8 | d[4];
            return false;
        }
        return rotate(&ccsPosition_, d[3] << 8 | d[4]);
    }
    if (fkt != 0xc20 || n < 7 || d[2] == 0xff || d[3] > 2) {
        return false;
    }
    const bool down = d[3] == 1 || d[3] == 2;
    const unsigned cluster = d[5], sensor = d[6];
    if (cluster == 0x1e && sensor == 1) {
        return setKeys(&ccsMotion_, with(ccsMotion_, NBT_CONTROLLER_PUSH, down));
    }
    if (cluster == 0x1d) {
        const uint32_t tilt = !down       ? 0u
                              : d[4] == 1 ? kUp
                              : d[4] == 4 ? kDown
                              : d[4] == 8 ? kLeft
                              : d[4] == 2 ? kRight
                                          : 0u;
        return setKeys(&ccsMotion_, (ccsMotion_ & NBT_CONTROLLER_PUSH) | tilt);
    }
    for (unsigned i = 0; cluster == 0 && i < NBT_CCS_SLOTS; ++i) {
        if (sensor == 1u << i) {
            return setKeys(&ccsKeys_, with(ccsKeys_, slotKey(i, true), down));
        }
    }
    return false;
}

bool ControllerFeed::decode(const nbt_most_message &m) {
    if (m.op != kOpStatus || !m.data) {
        return false;
    }
    if (m.fblock == NBT_CAPTURE_FBLOCK_CENTER_STACK && m.fkt == NBT_CAPTURE_FKT_FAVOURITES) {
        uint32_t keys = 0;
        for (size_t i = 0; i < m.length && i < 8; ++i) {
            keys |= m.data[i] ? NBT_CONTROLLER_FAVOURITE(i) : 0u;
        }
        return setKeys(&favourites_, keys);
    }
    if (m.fblock != kController) {
        return false;
    }
    if (m.inst == config_.knob_instance) {
        return knob(m.fkt, m.data, m.length);
    }
    return m.inst == config_.ccs_instance && ccs(m.fkt, m.data, m.length);
}

void ControllerFeed::message(const nbt_most_message &message) {
    core::LockGuard guard(lock_);
    if (fd_ >= 0 && decode(message)) {
        sendLocked();
    }
}

void ControllerFeed::link(bool up) {
    core::LockGuard guard(lock_);
    if (fd_ < 0 || (state_.connected != 0) == up) {
        return;
    }
    state_.connected = up ? 1 : 0;
    if (!up) {
        position_ = ccsPosition_ = -1;
        knobMotion_ = knobKeys_ = ccsMotion_ = ccsKeys_ = favourites_ = 0;
    }
    sendLocked();
}

void ControllerFeed::pause(bool paused) {
    core::LockGuard guard(lock_);
    const bool was = sending();
    paused_ = paused;
    changed(was);
}

void ControllerFeed::show(bool shown) {
    core::LockGuard guard(lock_);
    const bool was = sending();
    shown_ = shown;
    changed(was);
}

void ControllerFeed::changed(bool wasSending) {
    if (wasSending && !sending()) {
        // Last word before going quiet: nothing held, no link. The reader then
        // waits for every key up again, so a push held while the application
        // comes back is never a press, and the rotation restarts at 0.
        emit(true);
    } else if (!wasSending) {
        sendLocked();
    }
}

nbt_controller_wire_state ControllerFeed::state() {
    core::LockGuard guard(lock_);
    return state_;
}

void ControllerFeed::sendLocked() {
    state_.keys =
        state_.connected ? knobMotion_ | knobKeys_ | ccsMotion_ | ccsKeys_ | favourites_ : 0u;
    if (sending()) {
        emit(false);
    }
}

void ControllerFeed::emit(bool gone) {
    ++state_.sequence;
    nbt_controller_wire_state sent = state_;
    if (gone) {
        sent.connected = 0;
        sent.keys = 0;
    }
    uint8_t out[NBT_CONTROLLER_WIRE_SIZE];
    if (nbt_controller_wire_encode(&sent, out, sizeof(out)) < 0) {
        return;
    }
    ssize_t n;
    do {
        n = send(fd_, out, sizeof(out), 0);
    } while (n < 0 && errno == EINTR);
    // A full socket (the application not reading) drops this state; the next
    // one carries the keys and the total rotation, so nothing is lost for good.
}

} // namespace most
} // namespace nbt
