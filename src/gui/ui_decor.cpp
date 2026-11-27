// SPDX-License-Identifier: MPL-2.0
/* What a theme draws around the text (nbt/gui/ui_draw.hpp): the page, its
 * header strip and emblem, and the mark of the focused row. */
#include "nbt/gui/ui_draw.hpp"

namespace nbt {
namespace gui {
namespace draw {
namespace {

const int32_t kKnob = 10, kDot = 6, kSquare = 5, kRail = 6;
const lv_point_precise_t kChevron[] = {{0, 0}, {5, 6}, {0, 12}};

/* A circle's outline, centred on (cx, cy). */
lv_obj_t *ring(lv_obj_t *parent, int32_t cx, int32_t cy, int32_t r, uint32_t color, lv_opa_t opa,
               int32_t width) {
    lv_obj_t *o = box(parent, cx - r, cy - r, 2 * r, 2 * r, 0, LV_OPA_TRANSP, 0);
    if (o) {
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(o, lv_color_hex(color), 0);
        lv_obj_set_style_border_width(o, width, 0);
        lv_obj_set_style_border_opa(o, opa, 0);
    }
    return o;
}

/* Tiles: the header strip, and four squares before the title, one lit. */
int tilesPage(const Palette &c, lv_obj_t *screen, int32_t w, int32_t header) {
    lv_obj_t *strip = box(screen, 0, 0, w, header, c.track, LV_OPA_COVER, 0);
    if (!strip || !box(screen, 0, header, w, 1, c.rule, LV_OPA_COVER, 0)) {
        return -1;
    }
    lv_obj_set_style_bg_grad_color(strip, lv_color_hex(c.panel), 0);
    lv_obj_set_style_bg_grad_dir(strip, LV_GRAD_DIR_VER, 0);
    const int32_t y = (header - 2 * kSquare - 2) / 2;
    for (int i = 0; i < 4; ++i) {
        if (!box(screen, kMargin + (i % 2) * (kSquare + 2), y + (i / 2) * (kSquare + 2), kSquare,
                 kSquare, i == 1 ? c.accent : c.foreground, LV_OPA_COVER, 1)) {
            return -1;
        }
    }
    return 0;
}

/* Bars: the round display's rings off the right edge, under a status strip. */
int barsPage(const Palette &c, lv_obj_t *screen, int32_t w, int32_t h, int32_t header) {
    const int32_t cx = w - 70, cy = h / 2 + 46;
    return ring(screen, cx, cy, 168, c.highlight, LV_OPA_50, 2) &&
                   ring(screen, cx, cy, 128, c.accent, LV_OPA_20, 1) &&
                   ring(screen, cx, cy, 84, c.highlight, LV_OPA_30, 1) &&
                   box(screen, 0, 0, w, header, c.panel, LV_OPA_COVER, 0) &&
                   box(screen, 0, header, w, 1, c.highlight, LV_OPA_COVER, 0)
               ? 0
               : -1;
}

/* Glow: the rail, from the line under the title down the left of the page. */
int glowPage(const Palette &c, lv_obj_t *screen, int32_t h, int32_t header) {
    lv_obj_t *rail = box(screen, kRail, header, 1, h - header - 6, c.accent, LV_OPA_COVER, 0);
    if (!rail || !box(screen, kRail, header, kMargin - kRail, 1, c.accent, LV_OPA_COVER, 0)) {
        return -1;
    }
    lv_obj_set_style_bg_grad_color(rail, lv_color_hex(c.background), 0); // fades downwards
    lv_obj_set_style_bg_grad_dir(rail, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(rail, 120, 0);
    return 0;
}

} // namespace

int backdrop(const Palette &c, lv_obj_t *screen, int32_t w, int32_t h, int32_t header) {
    lv_obj_set_style_bg_color(screen, lv_color_hex(c.background), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(c.deep), 0);
    lv_obj_set_style_bg_grad_dir(screen,
                                 c.deep == c.background ? LV_GRAD_DIR_NONE : LV_GRAD_DIR_VER, 0);
    switch (c.look) {
    case LookTiles:
        return tilesPage(c, screen, w, header);
    case LookBars:
        return barsPage(c, screen, w, h, header);
    default:
        return glowPage(c, screen, h, header);
    }
}

int32_t titleInset(const Palette &c) {
    return c.look == LookTiles ? 2 * kSquare + 10 : 0;
}

int32_t gutter(const Palette &c) {
    return kMargin / 2 + (c.look == LookGlow ? kRail : 0);
}

int mark(const Palette &c, lv_obj_t *line, lv_obj_t *body, int32_t w, int32_t h) {
    if (c.look == LookBars) {
        lv_obj_t *dot = box(line, 1, (h - kDot) / 2, kDot, kDot, c.accent, LV_OPA_COVER, kDot / 2);
        return dot ? 0 : -1;
    }
    if (c.look == LookGlow) { // the knob, on the rail, and the glow over the underline
        lv_obj_t *knob = ring(line, kRail, h / 2, kKnob / 2, c.accent, LV_OPA_COVER, 1);
        lv_obj_t *glow =
            box(body, c.edge, h - c.underline - 5, w - c.edge, 5, c.accent, LV_OPA_30, 0);
        if (!knob || !glow) {
            return -1;
        }
        lv_obj_set_style_bg_color(knob, lv_color_hex(c.track), 0);
        lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_grad_color(glow, lv_color_hex(c.highlightFade), 0);
        lv_obj_set_style_bg_grad_dir(glow, LV_GRAD_DIR_HOR, 0);
        return 0;
    }
    lv_obj_t *chevron = lv_line_create(body);
    if (!chevron) {
        return -1;
    }
    lv_line_set_points(chevron, kChevron, sizeof(kChevron) / sizeof(kChevron[0]));
    lv_obj_set_pos(chevron, w - kMargin, (h - 12) / 2);
    lv_obj_set_style_line_width(chevron, 2, 0);
    lv_obj_set_style_line_color(chevron, lv_color_hex(c.foreground), 0);
    lv_obj_set_style_line_rounded(chevron, true, 0);
    return 0;
}

} // namespace draw
} // namespace gui
} // namespace nbt
