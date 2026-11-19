// SPDX-License-Identifier: MPL-2.0
#include "nbt/gamepad/reports.hpp"
#include <string.h>
namespace nbt {
namespace gamepad {
Family identify(unsigned vid, unsigned pid, unsigned cls, unsigned sub, unsigned protocol) {
    if (cls == 0xff && sub == 0x47 && protocol == 0xd0) {
        return Gip;
    }
    if (vid != 0x054c || cls != 3) {
        return Unsupported;
    }
    if (pid == 0x05c4 || pid == 0x09cc) {
        return DualShock4;
    }
    return (pid == 0x0ce6 || pid == 0x0df2) ? DualSense : Unsupported;
}
namespace {
int16_t stick(unsigned v, bool invert) {
    const int n =
        v < 128 ? (static_cast<int>(v) - 128) * 256 : (static_cast<int>(v) - 128) * 32767 / 127;
    return static_cast<int16_t>(invert ? (n == -32768 ? 32767 : -n) : n);
}
} // namespace
int decodeSony(Family family, const uint8_t *p, size_t n, nbt_gamepad_wire_state &state) {
    if (!p || (family != DualShock4 && family != DualSense) || n != 64 || p[0] != 1) {
        return -1;
    }
    const size_t b = family == DualShock4 ? 5 : 8;
    const size_t t = family == DualShock4 ? 8 : 5;
    const unsigned hat = p[b] & 15;
    if (hat > 8) {
        return -1;
    }
    nbt_gamepad_wire_state s;
    memset(&s, 0, sizeof(s));
    s.connected = 1;
    s.lx = stick(p[1], false);
    s.ly = stick(p[2], true);
    s.rx = stick(p[3], false);
    s.ry = stick(p[4], true);
    s.lt = static_cast<uint16_t>(p[t] * 257u);
    s.rt = static_cast<uint16_t>(p[t + 1] * 257u);
    const uint32_t face[] = {NBT_GAMEPAD_WEST, NBT_GAMEPAD_SOUTH, NBT_GAMEPAD_EAST,
                             NBT_GAMEPAD_NORTH};
    const uint32_t middle[] = {
        NBT_GAMEPAD_LB, NBT_GAMEPAD_RB, 0, 0, NBT_GAMEPAD_VIEW, NBT_GAMEPAD_MENU,
        NBT_GAMEPAD_LS, NBT_GAMEPAD_RS};
    for (unsigned i = 0; i < 4; ++i) {
        if (p[b] & (16u << i)) {
            s.buttons |= face[i];
        }
    }
    for (unsigned i = 0; i < 8; ++i) {
        if (p[b + 1] & (1u << i)) {
            s.buttons |= middle[i];
        }
    }
    if (p[b + 2] & 1) {
        s.buttons |= NBT_GAMEPAD_GUIDE;
    }
    const uint32_t hats[] = {NBT_GAMEPAD_UP,
                             NBT_GAMEPAD_UP | NBT_GAMEPAD_RIGHT,
                             NBT_GAMEPAD_RIGHT,
                             NBT_GAMEPAD_DOWN | NBT_GAMEPAD_RIGHT,
                             NBT_GAMEPAD_DOWN,
                             NBT_GAMEPAD_DOWN | NBT_GAMEPAD_LEFT,
                             NBT_GAMEPAD_LEFT,
                             NBT_GAMEPAD_UP | NBT_GAMEPAD_LEFT,
                             0};
    s.buttons |= hats[hat];
    state = s;
    return 1;
}
size_t dualShock4LightBar(uint8_t out[64], uint8_t red, uint8_t green, uint8_t blue) {
    memset(out, 0, 32);
    out[0] = 0x05; // report id
    out[1] = 0x07; // motors, light bar and flash fields valid
    out[6] = red;
    out[7] = green;
    out[8] = blue;
    return 32;
}
} // namespace gamepad
} // namespace nbt
