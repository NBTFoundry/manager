#ifndef NBT_GUI_FRAME_HPP
#define NBT_GUI_FRAME_HPP

#include "nbt/core/status.hpp"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace gui {

struct Frame {
    unsigned char *pixels;
    size_t bytes;
    size_t width;
    size_t height;
    size_t stride;

    bool valid() const;
    core::Status fill(uint32_t color);
    core::Status text(const char *text, size_t x, size_t y, size_t scale, uint32_t color);
    core::Status message(const char *version);
    core::Status hello(const char *version, const char *color, const char *hint);
};

} // namespace gui
} // namespace nbt

#endif
