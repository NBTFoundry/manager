// SPDX-License-Identifier: MPL-2.0
#include "nbt/gamepad/reports.hpp"
#include <string.h>
namespace nbt {
namespace gamepad {
namespace {
unsigned word(const uint8_t *p) {
    return p[0] | static_cast<unsigned>(p[1]) << 8;
}
int16_t axis(const uint8_t *p, bool invert) {
    int v = static_cast<int>(word(p));
    if (v >= 32768) {
        v -= 65536;
    }
    return static_cast<int16_t>(invert ? (v == -32768 ? 32767 : -v) : v);
}
} // namespace
GipSession::GipSession()
    : packets_(), lengths_(), head_(0), count_(0), sequence_(0), vendor_(0), product_(0) {
}
bool GipSession::enqueue(uint8_t cmd, uint8_t options, uint8_t seq, const uint8_t *p, size_t n) {
    if (n > 60 || count_ == 16) {
        return false;
    }
    const unsigned tail = (head_ + count_) % 16;
    uint8_t *out = packets_[tail];
    out[0] = cmd;
    out[1] = options;
    out[2] = seq;
    out[3] = static_cast<uint8_t>(n);
    if (n) {
        memcpy(out + 4, p, n);
    }
    lengths_[tail] = static_cast<uint8_t>(n + 4);
    ++count_;
    return true;
}
void GipSession::initialize() {
    const uint8_t power[] = {0}, led[] = {0, 1, 20}, auth[] = {1, 0};
    const uint8_t hori[] = {0, 4, 0x20, 0x3a, 0, 0, 0, 0x80, 0};
    if ((vendor_ == 0x0e6f && product_ == 0x0165) || (vendor_ == 0x0f0d && product_ == 0x0067)) {
        (void)enqueue(1, 0x20, sequence_++, hori, sizeof(hori));
    }
    (void)enqueue(5, 0x20, sequence_++, power, sizeof(power));
    if (vendor_ == 0x045e && (product_ == 0x02ea || product_ == 0x0b00)) {
        const uint8_t mode[] = {6};
        (void)enqueue(5, 0x20, sequence_++, mode, sizeof(mode));
        // This firmware command intentionally declares 0x0f with one byte.
        packets_[(head_ + count_ - 1) % 16][3] = 0x0f;
    }
    (void)enqueue(0x0a, 0x20, sequence_++, led, sizeof(led));
    (void)enqueue(6, 0x20, sequence_++, auth, sizeof(auth));
    if (vendor_ == 0x24c6 && (product_ == 0x541a || product_ == 0x542a || product_ == 0x543a)) {
        uint8_t rumble[] = {0, 15, 0, 0, 29, 29, 255, 0, 0};
        (void)enqueue(9, 0, sequence_++, rumble, sizeof(rumble));
        memset(rumble + 4, 0, 3);
        (void)enqueue(9, 0, sequence_++, rumble, sizeof(rumble));
    }
}
void GipSession::reset(unsigned vendor, unsigned product) {
    head_ = count_ = sequence_ = 0;
    vendor_ = vendor;
    product_ = product;
    initialize();
}
size_t GipSession::output(uint8_t out[64]) {
    if (!out || !count_) {
        return 0;
    }
    const size_t n = lengths_[head_];
    memcpy(out, packets_[head_], n);
    head_ = static_cast<uint8_t>((head_ + 1) % 16);
    --count_;
    return n;
}
int GipSession::feed(const uint8_t *p, size_t n, nbt_gamepad_wire_state &state) {
    // Only the main wired client, unchunked reports; no arbitrary reassembly.
    if (!p || n < 4 || n > 64 || (p[1] & 0xcf) || p[3] >= 128 || p[3] != n - 4) {
        return -1;
    }
    const uint8_t cmd = p[0];
    if ((cmd == 0x20 && (n < 18 || word(p + 6) > 1023 || word(p + 8) > 1023)) ||
        (cmd == 7 && (n != 6 || p[4] > 1 || p[5] != 0x5b)) || (cmd == 2 && n != 32)) {
        return -1;
    }
    if (p[1] & 0x10) {
        const uint8_t ack[] = {0, cmd, static_cast<uint8_t>(p[1] & ~0x10), p[3], 0, 0, 0, 0, 0};
        if (!enqueue(1, 0x20, p[2], ack, sizeof(ack))) {
            return -1;
        }
    }
    if (cmd == 2) {
        // A fresh announcement means the device restarted. Replace stale init.
        head_ = count_ = 0;
        if (p[1] & 0x10) {
            const uint8_t ack[] = {0, 2, 0x20, 28, 0, 0, 0, 0, 0};
            (void)enqueue(1, 0x20, p[2], ack, sizeof(ack));
        }
        initialize();
        memset(&state, 0, sizeof(state)); // a restarted controller releases all controls
        return 1;
    }
    if (cmd == 7) {
        if (p[4]) {
            state.buttons |= NBT_GAMEPAD_GUIDE;
        } else {
            state.buttons &= ~NBT_GAMEPAD_GUIDE;
        }
        state.connected = 1;
        return 1;
    }
    if (cmd != 0x20) {
        return 0;
    }
    nbt_gamepad_wire_state s;
    memset(&s, 0, sizeof(s));
    s.connected = 1;
    s.buttons = state.buttons & NBT_GAMEPAD_GUIDE;
    const uint32_t buttons[] = {0,
                                0,
                                NBT_GAMEPAD_MENU,
                                NBT_GAMEPAD_VIEW,
                                NBT_GAMEPAD_SOUTH,
                                NBT_GAMEPAD_EAST,
                                NBT_GAMEPAD_WEST,
                                NBT_GAMEPAD_NORTH,
                                NBT_GAMEPAD_UP,
                                NBT_GAMEPAD_DOWN,
                                NBT_GAMEPAD_LEFT,
                                NBT_GAMEPAD_RIGHT,
                                NBT_GAMEPAD_LB,
                                NBT_GAMEPAD_RB,
                                NBT_GAMEPAD_LS,
                                NBT_GAMEPAD_RS};
    const unsigned b = word(p + 4);
    for (unsigned i = 0; i < 16; ++i) {
        if (b & (1u << i)) {
            s.buttons |= buttons[i];
        }
    }
    s.lt = static_cast<uint16_t>(word(p + 6) * 65535u / 1023u);
    s.rt = static_cast<uint16_t>(word(p + 8) * 65535u / 1023u);
    s.lx = axis(p + 10, false);
    s.ly = axis(p + 12, false);
    s.rx = axis(p + 14, false);
    s.ry = axis(p + 16, false);
    state = s;
    return 1;
}
} // namespace gamepad
} // namespace nbt
