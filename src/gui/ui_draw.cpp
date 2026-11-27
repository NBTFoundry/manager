// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/ui_draw.hpp"

namespace nbt {
namespace gui {
namespace draw {
namespace {

const int32_t kDialogWidth = 440;

} // namespace

lv_obj_t *box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color,
              lv_opa_t opa, int32_t radius) {
    lv_obj_t *obj = lv_obj_create(parent);
    if (!obj) {
        return NULL;
    }
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    if (opa != LV_OPA_TRANSP) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(obj, opa, 0);
        lv_obj_set_style_radius(obj, radius, 0);
    }
    return obj;
}

lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color,
                int32_t x, int32_t y, int32_t width) {
    lv_obj_t *l = (width > 0 && text[0]) ? lv_label_create(parent) : NULL;
    if (!l) {
        return NULL;
    }
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(l, width, lv_font_get_line_height(font));
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    return l;
}

int32_t corner(const Palette &c, int32_t height) {
    return c.radius < 0 || c.radius > height / 2 ? height / 2 : c.radius;
}

int32_t inset(const Palette &c) {
    return kMargin / 2 + c.edge + (c.radius < 0 ? 8 : 0);
}

int bar(const Palette &c, lv_obj_t *parent, int32_t x, int32_t y, int32_t width, int progress) {
    const int32_t filled = width * progress / 100, r = corner(c, kBarHeight);
    lv_obj_t *track = box(parent, x, y, width, kBarHeight, c.track, LV_OPA_COVER, r);
    if (!track ||
        (filled > 0 && !box(track, 0, 0, filled, kBarHeight, c.accent, LV_OPA_COVER, r))) {
        return -1;
    }
    return 0;
}

lv_obj_t *row(const Palette &c, lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
              bool focus, bool action) {
    lv_obj_t *body = box(parent, x, y, w, h, action ? c.actionHighlight : c.highlight,
                         focus ? LV_OPA_COVER : LV_OPA_TRANSP, corner(c, h));
    if (!body || !focus) {
        return body;
    }
    lv_obj_set_scrollable(body, false);
    lv_obj_set_style_clip_corner(body, true, 0);
    lv_obj_set_style_bg_grad_color(body, lv_color_hex(c.highlightFade), 0);
    lv_obj_set_style_bg_grad_dir(body, LV_GRAD_DIR_HOR, 0);
    if ((c.edge && !box(body, 0, 0, c.edge, h, c.accent, LV_OPA_COVER, 0)) ||
        (c.underline &&
         !box(body, 0, h - c.underline, w, c.underline, c.accent, LV_OPA_COVER, 0))) {
        return NULL;
    }
    return body;
}

int rule(const Palette &c, lv_obj_t *parent, int32_t x, int32_t y, int32_t width, bool rows) {
    if (c.ruled < (rows ? 2 : 1) || width <= 0) {
        return 0;
    }
    lv_obj_t *line = box(parent, x, y, width, 1, c.rule, LV_OPA_COVER, 0);
    if (!line) {
        return -1;
    }
    lv_obj_set_style_bg_grad_color(line, lv_color_hex(c.background), 0); // fades to the right
    lv_obj_set_style_bg_grad_dir(line, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_main_stop(line, 150, 0);
    return 0;
}

int dialog(const Palette &c, lv_obj_t *screen, const Modal *m, const lv_font_t *titleFont,
           const lv_font_t *lineFont, const lv_font_t *itemFont, int32_t width, int32_t height) {
    const int32_t boxWidth =
        width - 2 * kMargin > kDialogWidth ? kDialogWidth : width - 2 * kMargin;
    const int32_t rowHeight = lv_font_get_line_height(itemFont) + 8;
    if (!box(screen, 0, 0, width, height, 0x000000, LV_OPA_60, 0)) {
        return -1;
    }
    if (boxWidth < 4 * kMargin) {
        return 0;
    }
    const int32_t boxHeight =
        kMargin + lv_font_get_line_height(titleFont) + 6 + lv_font_get_line_height(lineFont) +
        kMargin + (m->progress >= 0 ? kBarHeight + kMargin : 0) +
        static_cast<int32_t>(m->itemCount) * rowHeight + (m->itemCount ? kMargin : 0);
    lv_obj_t *panel =
        box(screen, (width - boxWidth) / 2, height > boxHeight ? (height - boxHeight) / 2 : 0,
            boxWidth, boxHeight, c.dialog, LV_OPA_COVER, c.radius < 0 ? 18 : c.radius * 2);
    if (!panel) {
        return -1;
    }
    lv_obj_set_style_border_color(panel, lv_color_hex(c.rule), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_scrollable(panel, false);
    int32_t y = kMargin;
    label(panel, m->title, titleFont, c.foreground, kMargin, y, boxWidth - 2 * kMargin);
    y += lv_font_get_line_height(titleFont) + 6;
    if (rule(c, panel, kMargin, y - 4, boxWidth - 2 * kMargin, false)) {
        return -1;
    }
    label(panel, m->text, lineFont, c.muted, kMargin, y, boxWidth - 2 * kMargin);
    y += lv_font_get_line_height(lineFont) + kMargin;
    if (m->progress >= 0) {
        if (bar(c, panel, kMargin, y, boxWidth - 2 * kMargin, m->progress)) {
            return -1;
        }
        y += kBarHeight + kMargin;
    }
    for (size_t i = 0; i < m->itemCount; ++i, y += rowHeight) {
        lv_obj_t *item =
            row(c, panel, kMargin / 2, y, boxWidth - kMargin, rowHeight - 2, i == m->focus, false);
        if (!item) {
            return -1;
        }
        label(item, m->items[i], itemFont, c.foreground, inset(c), 3,
              boxWidth - kMargin - 2 * inset(c));
    }
    return 0;
}

} // namespace draw
} // namespace gui
} // namespace nbt
