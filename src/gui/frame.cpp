#include "nbt/gui/frame.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace nbt {
namespace gui {

bool Frame::valid() const {
    const Frame *f = this;
    if (!f || !f->pixels || !f->width || !f->height || f->width > SIZE_MAX / 4 ||
        f->stride < f->width * 4 || f->height > SIZE_MAX / f->stride ||
        f->bytes < f->stride * f->height) {
        return false;
    }
    return true;
}

core::Status Frame::fill(uint32_t color) {
    Frame *f = this;
    size_t x, y;
    if (!f->valid()) {
        return core::Status::error(EINVAL, "invalid frame operation");
    }
    for (y = 0; y < f->height; ++y) {
        for (x = 0; x < f->width; ++x) {
            memcpy(f->pixels + y * f->stride + x * 4, &color, sizeof(color));
        }
    }
    return core::Status();
}

/* A small original 5x7 font: space to 'Z', plus a distinct lowercase 'v'
 * for version labels. Other lowercase letters render as capitals and any
 * other byte renders as '?'. Each row holds five bits, most significant left. */
static const unsigned char font[59][7] = {
    {0, 0, 0, 0, 0, 0, 0},        /* space */ {4, 4, 4, 4, 4, 0, 4},    /* ! */
    {10, 10, 0, 0, 0, 0, 0},      /* " */ {10, 10, 31, 10, 31, 10, 10}, /* # */
    {4, 15, 20, 14, 5, 30, 4},    /* $ */ {24, 25, 2, 4, 8, 19, 3},     /* % */
    {12, 18, 20, 8, 21, 18, 13},  /* & */ {4, 4, 8, 0, 0, 0, 0},        /* ' */
    {2, 4, 8, 8, 8, 4, 2},        /* ( */ {8, 4, 2, 2, 2, 4, 8},        /* ) */
    {0, 4, 21, 14, 21, 4, 0},     /* * */ {0, 4, 4, 31, 4, 4, 0},       /* + */
    {0, 0, 0, 0, 12, 4, 8},       /* , */ {0, 0, 0, 31, 0, 0, 0},       /* - */
    {0, 0, 0, 0, 0, 12, 12},      /* . */ {1, 1, 2, 4, 8, 16, 16},      /* / */
    {14, 17, 19, 21, 25, 17, 14}, /* 0 */ {4, 12, 4, 4, 4, 4, 14},      /* 1 */
    {14, 17, 1, 2, 4, 8, 31},     /* 2 */ {31, 2, 4, 2, 1, 17, 14},     /* 3 */
    {2, 6, 10, 18, 31, 2, 2},     /* 4 */ {31, 16, 30, 1, 1, 17, 14},   /* 5 */
    {6, 8, 16, 30, 17, 17, 14},   /* 6 */ {31, 1, 2, 4, 8, 8, 8},       /* 7 */
    {14, 17, 17, 14, 17, 17, 14}, /* 8 */ {14, 17, 17, 15, 1, 2, 12},   /* 9 */
    {0, 12, 12, 0, 12, 12, 0},    /* : */ {0, 12, 12, 0, 12, 4, 8},     /* ; */
    {2, 4, 8, 16, 8, 4, 2},       /* < */ {0, 0, 31, 0, 31, 0, 0},      /* = */
    {8, 4, 2, 1, 2, 4, 8},        /* > */ {14, 17, 1, 2, 4, 0, 4},      /* ? */
    {14, 17, 23, 21, 23, 16, 14}, /* @ */ {14, 17, 17, 31, 17, 17, 17}, /* A */
    {30, 17, 17, 30, 17, 17, 30}, /* B */ {14, 17, 16, 16, 16, 17, 14}, /* C */
    {30, 17, 17, 17, 17, 17, 30}, /* D */ {31, 16, 16, 30, 16, 16, 31}, /* E */
    {31, 16, 16, 30, 16, 16, 16}, /* F */ {14, 17, 16, 23, 17, 17, 15}, /* G */
    {17, 17, 17, 31, 17, 17, 17}, /* H */ {14, 4, 4, 4, 4, 4, 14},      /* I */
    {7, 2, 2, 2, 2, 18, 12},      /* J */ {17, 18, 20, 24, 20, 18, 17}, /* K */
    {16, 16, 16, 16, 16, 16, 31}, /* L */ {17, 27, 21, 21, 17, 17, 17}, /* M */
    {17, 25, 21, 19, 17, 17, 17}, /* N */ {14, 17, 17, 17, 17, 17, 14}, /* O */
    {30, 17, 17, 30, 16, 16, 16}, /* P */ {14, 17, 17, 17, 21, 18, 13}, /* Q */
    {30, 17, 17, 30, 20, 18, 17}, /* R */ {15, 16, 16, 14, 1, 1, 30},   /* S */
    {31, 4, 4, 4, 4, 4, 4},       /* T */ {17, 17, 17, 17, 17, 17, 14}, /* U */
    {17, 17, 17, 17, 17, 10, 4},  /* V */ {17, 17, 17, 21, 21, 21, 10}, /* W */
    {17, 17, 10, 4, 10, 17, 17},  /* X */ {17, 17, 10, 4, 4, 4, 4},     /* Y */
    {31, 1, 2, 4, 8, 16, 31}                                            /* Z */
};

static const unsigned char *glyph(char value) {
    static const unsigned char small_v[7] = {0, 0, 17, 17, 17, 10, 4};
    unsigned char c = (unsigned char)value;
    if (c == 'v') {
        return small_v;
    }
    if (c >= 'a' && c <= 'z') {
        c = (unsigned char)(c - 'a' + 'A');
    }
    if (c < ' ' || c > 'Z') {
        c = '?';
    }
    return font[c - ' '];
}

core::Status Frame::text(const char *text, size_t x, size_t y, size_t scale, uint32_t color) {
    Frame *f = this;
    size_t i, row, col, dx, dy, count;
    if (!f->valid() || !text || !scale || x >= f->width || y >= f->height ||
        scale > (f->height - y) / 7 || scale > (f->width - x) / 6) {
        return core::Status::error(EINVAL, "invalid frame operation");
    }
    count = 0;
    while (count <= (f->width - x) / (6 * scale) && text[count]) {
        ++count;
    }
    if (!count || count > (f->width - x) / (6 * scale)) {
        return core::Status::error(EINVAL, "invalid frame operation");
    }
    for (i = 0; i < count; ++i) {
        const unsigned char *bits = glyph(text[i]);
        for (row = 0; row < 7; ++row) {
            for (col = 0; col < 5; ++col) {
                if (!(bits[row] & (1u << (4 - col)))) {
                    continue;
                }
                for (dy = 0; dy < scale; ++dy) {
                    for (dx = 0; dx < scale; ++dx) {
                        size_t px = x + (i * 6 + col) * scale + dx;
                        size_t py = y + row * scale + dy;
                        memcpy(f->pixels + py * f->stride + px * 4, &color, sizeof(color));
                    }
                }
            }
        }
    }
    return core::Status();
}

core::Status Frame::message(const char *version) {
    Frame *f = this;
    char label[11] = "TEST - v1";
    size_t count = 9, scale, left, top, i, row, col, dx, dy;
    const uint32_t foreground = 0xffffffffu, background = 0xff24150bu;
    if (!f->valid() || !version || version[0] != 'v' || version[1] < '1' || version[1] > '9' ||
        version[2]) {
        return core::Status::error(EINVAL, "invalid frame operation");
    }
    label[8] = version[1];
    scale = f->width / (count * 6 + 4);
    if (scale > f->height / 11) {
        scale = f->height / 11;
    }
    if (!scale) {
        return core::Status::error(EINVAL, "invalid frame operation");
    }
    if (!f->fill(background).ok()) {
        return core::Status::error(EINVAL, "invalid frame operation");
    }
    left = (f->width - count * 6 * scale) / 2;
    top = (f->height - 7 * scale) / 2;
    for (i = 0; i < count; ++i) {
        const unsigned char *bits = glyph(label[i]);
        for (row = 0; row < 7; ++row) {
            for (col = 0; col < 5; ++col) {
                if (!(bits[row] & (1u << (4 - col)))) {
                    continue;
                }
                for (dy = 0; dy < scale; ++dy) {
                    for (dx = 0; dx < scale; ++dx) {
                        size_t x = left + (i * 6 + col) * scale + dx;
                        size_t y = top + row * scale + dy;
                        memcpy(f->pixels + y * f->stride + x * 4, &foreground, sizeof(foreground));
                    }
                }
            }
        }
    }
    return core::Status();
}

/* One line of capitals, centred horizontally at `top`. */
static int centred(Frame *f, const char *text, size_t top, size_t scale, uint32_t color) {
    size_t width = strlen(text) * 6 * scale;
    if (!scale || width > f->width || top + 7 * scale > f->height) {
        return -1;
    }
    return f->text(text, (f->width - width) / 2, top, scale, color).ok() ? 0 : -1;
}

core::Status Frame::hello(const char *version, const char *color, const char *hint) {
    Frame *f = this;
    static const struct {
        const char *name;
        uint32_t value;
    } colors[] = {{"blue", 0xff1d4f91u},
                  {"red", 0xff9a2323u},
                  {"green", 0xff1f7a3eu},
                  {"black", 0xff000000u}};
    char label[24];
    uint32_t background = colors[0].value;
    size_t i, scale, small;
    if (!f->valid() || !version || strlen(version) > 16) {
        return core::Status::error(EINVAL, "invalid frame operation");
    }
    for (i = 0; color && i < sizeof(colors) / sizeof(colors[0]); ++i) {
        if (!strcmp(color, colors[i].name)) {
            background = colors[i].value;
        }
    }
    snprintf(label, sizeof(label), "TEST %s", version);
    scale = f->width / (11 * 6 + 4);
    if (scale > f->height / 16) {
        scale = f->height / 16;
    }
    small = scale / 3 ? scale / 3 : 1;
    if (!f->fill(background).ok()) {
        return core::Status::error(EINVAL, "invalid frame operation");
    }
    if (!scale) {
        return core::Status();
    }
    if (centred(f, "HELLO WORLD", f->height / 2 - 7 * scale, scale, 0xffffffffu) ||
        centred(f, label, f->height / 2 + 2 * scale, small * 2, 0xffd0d8e0u) ||
        (hint && centred(f, hint, f->height - 7 * small - 4 * small, small, 0xffd0d8e0u))) {
        return core::Status::error(EINVAL, "invalid frame operation");
    }
    return core::Status();
}

} // namespace gui
} // namespace nbt
