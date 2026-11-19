#include "nbt/gui/frame.hpp"

#include <stdint.h>
#include <string.h>

int main(void) {
    unsigned char memory[16 + 128 * 64 * 4 + 16];
    nbt::gui::Frame frame;
    size_t i;
    memset(memory, 0xa5, sizeof(memory));
    frame.pixels = memory + 16;
    frame.bytes = 128 * 64 * 4;
    frame.width = 128;
    frame.height = 64;
    frame.stride = 128 * 4;
    if (!frame.message("v1").ok() || !frame.message("v2").ok() || !frame.message("v3").ok()) {
        return 1;
    }
    if (frame.message("v0").ok() || frame.message("v10").ok() || frame.message("x1").ok()) {
        return 1;
    }
    if (!frame.text("NBT MANAGER", 0, 0, 1, 0xffffffffu).ok() ||
        frame.text("NBT MANAGER", 127, 0, 1, 0xffffffffu).ok() ||
        frame.text("NBT MANAGER", 0, 63, 1, 0xffffffffu).ok() ||
        frame.text("NBT MANAGER", 0, 0, SIZE_MAX, 0xffffffffu).ok()) {
        return 1;
    }
    for (i = 0; i < 16; ++i) {
        if (memory[i] != 0xa5 || memory[sizeof(memory) - 1 - i] != 0xa5) {
            return 1;
        }
    }
    frame.bytes--;
    if (frame.message("v1").ok()) {
        return 1;
    }
    frame.bytes++;
    frame.stride = 1;
    if (frame.fill(0).ok()) {
        return 1;
    }
    frame.stride = SIZE_MAX;
    if (frame.fill(0).ok()) {
        return 1;
    }
    frame.stride = 128 * 4;
    if (frame.message("invalid").ok()) {
        return 1;
    }
    return 0;
}
