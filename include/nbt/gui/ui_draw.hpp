// SPDX-License-Identifier: MPL-2.0
/* The pieces the renderer draws with LVGL, in a theme's colours and shapes
 * (theme.hpp): boxes, labels, the progress bar, the cursor on the focused
 * row, the rule under a title and the dialog. A NULL or -1 result: LVGL
 * could not create an object. */
#ifndef NBT_GUI_UI_DRAW_HPP
#define NBT_GUI_UI_DRAW_HPP

#include "lvgl/lvgl.h"
#include "nbt/gui/theme.hpp"
#include "nbt/gui/ui_view.hpp"

namespace nbt {
namespace gui {
namespace draw {

static const int32_t kMargin = 16, kBarHeight = 8;

lv_obj_t *box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color,
              lv_opa_t opa, int32_t radius);
lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color,
                int32_t x, int32_t y, int32_t width);
/* The theme's corner for a shape `height` tall: its radius, or a pill. */
int32_t corner(const Palette &c, int32_t height);
/* Where a row's text starts: past the cursor's edge, or a pill's round end. */
int32_t inset(const Palette &c);
int bar(const Palette &c, lv_obj_t *parent, int32_t x, int32_t y, int32_t width, int progress);
/* A row's body, w x h at (x, y): transparent, or with `focus` the theme's
 * cursor (its fill fading to the right, its bright edge and underline). */
lv_obj_t *row(const Palette &c, lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
              bool focus, bool action);
/* The thin line under a title, or with `rows` between rows; nothing in a
 * theme without (Palette::ruled). */
int rule(const Palette &c, lv_obj_t *parent, int32_t x, int32_t y, int32_t width, bool rows);
/* The page behind everything: its colours, the header strip `header` pixels
 * tall with the theme's emblem, rings or rail. */
int backdrop(const Palette &c, lv_obj_t *screen, int32_t w, int32_t h, int32_t header);
/* Where a title starts, past the emblem of a theme that has one. */
int32_t titleInset(const Palette &c);
/* Room at the left of a list's rows for the mark of the focused one. */
int32_t gutter(const Palette &c);
/* The focused list row's mark: a chevron at its right, a dot or the
 * controller's knob at its left. `line` is the row's full line (the gutter,
 * then `body`, w x h). */
int mark(const Palette &c, lv_obj_t *line, lv_obj_t *body, int32_t w, int32_t h);
int dialog(const Palette &c, lv_obj_t *screen, const Modal *m, const lv_font_t *titleFont,
           const lv_font_t *lineFont, const lv_font_t *itemFont, int32_t width, int32_t height);

} // namespace draw
} // namespace gui
} // namespace nbt

#endif
