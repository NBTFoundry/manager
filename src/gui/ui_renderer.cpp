// SPDX-License-Identifier: MPL-2.0
/* Renders the GUI view model with LVGL into a Frame. */
#include "nbt/gui/ui_renderer.hpp"

#include "lvgl/lvgl.h"
#include "nbt/gui/text.hpp"
#include "nbt/gui/ui_draw.hpp"
#include "nbt/gui/ui_fonts.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {
namespace gui {
namespace {

const Palette *colors; // the view's theme, set by render()
const int32_t kScrollbarWidth = 6, kScrollbarGap = 6;
using draw::box;
using draw::kBarHeight;
using draw::kMargin;
using draw::label;
const size_t kDrawLines = 40, kMaxSide = 4096u;

lv_display_t *display;
size_t displayWidth, displayHeight, listCount, listRows;
void *drawBuffer;
Frame *target;
int initialized;
int32_t listScroll;
char listTitle[kTextMax + 1];

uint32_t tick() {
    uint64_t ms = 0;
    return nbt_monotonic_ms(&ms) ? 0 : static_cast<uint32_t>(ms);
}

void flush(lv_display_t *d, const lv_area_t *area, uint8_t *pixels) {
    const int32_t width = lv_area_get_width(area);
    const uint32_t stride =
        lv_draw_buf_width_to_stride(static_cast<uint32_t>(width), LV_COLOR_FORMAT_XRGB8888);
    if (target && area->x1 >= 0 && area->y1 >= 0 && static_cast<size_t>(area->x2) < target->width &&
        static_cast<size_t>(area->y2) < target->height) {
        for (int32_t y = area->y1; y <= area->y2; ++y) {
            memcpy(target->pixels + static_cast<size_t>(y) * target->stride +
                       static_cast<size_t>(area->x1) * 4,
                   pixels + static_cast<size_t>(y - area->y1) * stride,
                   static_cast<size_t>(width) * 4);
        }
    }
    lv_display_flush_ready(d);
}

int ensureDisplay(size_t width, size_t height) {
    if (!initialized) {
        lv_init();
        lv_tick_set_cb(tick);
        initialized = 1;
    }
    if (display && displayWidth == width && displayHeight == height) {
        return 0;
    }
    if (display) {
        lv_display_delete(display);
    }
    free(drawBuffer);
    const size_t bytes = static_cast<size_t>(lv_draw_buf_width_to_stride(
                             static_cast<uint32_t>(width), LV_COLOR_FORMAT_XRGB8888)) *
                         (height < kDrawLines ? height : kDrawLines);
    drawBuffer = malloc(bytes);
    display = drawBuffer
                  ? lv_display_create(static_cast<int32_t>(width), static_cast<int32_t>(height))
                  : NULL;
    if (!display) {
        free(drawBuffer);
        drawBuffer = NULL;
        return -1;
    }
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, drawBuffer, NULL, static_cast<uint32_t>(bytes),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    displayWidth = width;
    displayHeight = height;
    return 0;
}

int renderList(lv_obj_t *screen, const UiView *v, const lv_font_t *itemFont, int32_t textRight,
               int32_t y, size_t frameHeight) {
    const int32_t rowHeight = lv_font_get_line_height(itemFont) + 8;
    size_t rows = y < static_cast<int32_t>(frameHeight)
                      ? (frameHeight - static_cast<size_t>(y)) / static_cast<size_t>(rowHeight)
                      : 0;
    rows = rows > v->itemCount ? v->itemCount : rows;
    if (!rows || textRight <= kMargin / 2 + 2 * kMargin + kScrollbarWidth + kScrollbarGap) {
        return 0;
    }
    const bool overflow = rows < v->itemCount;
    const int32_t listWidth = textRight - kMargin / 2;
    const int32_t rowWidth = listWidth - (overflow ? kScrollbarWidth + kScrollbarGap : 0);
    const int32_t listHeight = static_cast<int32_t>(rows) * rowHeight;
    lv_obj_t *focused = NULL;
    if (overflow && !box(screen, textRight - kScrollbarWidth, y, kScrollbarWidth, listHeight,
                         colors->track, LV_OPA_COVER, draw::corner(*colors, kScrollbarWidth))) {
        return -1;
    }
    lv_obj_t *list = box(screen, kMargin / 2, y, listWidth, listHeight, 0, LV_OPA_TRANSP, 0);
    if (!list) {
        return -1;
    }
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, overflow ? LV_SCROLLBAR_MODE_ON : LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_width(list, kScrollbarWidth, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(list, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_ver(list, 0, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list, lv_color_hex(colors->muted), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list, draw::corner(*colors, kScrollbarWidth), LV_PART_SCROLLBAR);
    for (size_t i = 0; i < v->itemCount; ++i) {
        const bool isAction = v->itemStyle[i] == ItemAction;
        const bool isFocus = i == v->focus;
        lv_obj_t *row = box(list, 0, static_cast<int32_t>(i) * rowHeight, rowWidth, rowHeight, 0,
                            LV_OPA_TRANSP, 0);
        lv_obj_t *body =
            row ? draw::row(*colors, row, 0, 0, rowWidth, rowHeight - 2, isFocus, isAction) : NULL;
        if (!body ||
            (!isFocus && i + 1 != v->focus &&
             draw::rule(*colors, row, kMargin / 2, rowHeight - 1, rowWidth - kMargin, true))) {
            return -1;
        }
        lv_obj_set_scrollable(row, false);
        if (isFocus) {
            focused = row;
        }
        label(body, v->items[i], itemFont,
              (!isFocus && isAction) ? colors->action : colors->foreground, draw::inset(*colors), 3,
              rowWidth - kMargin / 2 - 2 * draw::inset(*colors));
    }
    lv_obj_update_layout(list);
    if (overflow && !strcmp(listTitle, v->title) && listCount == v->itemCount && listRows == rows) {
        lv_obj_scroll_to_y(list, listScroll, LV_ANIM_OFF);
    }
    if (focused) {
        lv_obj_scroll_to_view(focused, LV_ANIM_OFF);
    }
    listScroll = lv_obj_get_scroll_y(list);
    memcpy(listTitle, v->title, sizeof(listTitle));
    listCount = v->itemCount;
    listRows = rows;
    return 0;
}

} // namespace

core::Status UiRenderer::render(const UiView &viewRef, const QrCode *qr, Frame &frameRef) {
    const UiView *v = &viewRef;
    Frame *frame = &frameRef;
    const UiFonts fonts = uiFonts(currentLanguage());
    colors = &palette(v->theme);
    const lv_font_t *titleFont = fonts.title, *lineFont = fonts.line, *itemFont = fonts.item;
    size_t qrScale = 0, qx = 0, qy = 0;
    if (!v || !frame->valid() || frame->width > kMaxSide || frame->height > kMaxSide ||
        !v->valid() || (qr && (!qr->size() || qr->size() > kQrMaxSize)) ||
        ensureDisplay(frame->width, frame->height)) {
        return core::Status::error(EINVAL, "cannot render UI");
    }
    int32_t textRight = static_cast<int32_t>(frame->width) - kMargin;
    if (qr) {
        const size_t side = qr->size() + 2 * kQrQuiet;
        const size_t maxScale = frame->width / 2 / side;
        qrScale = frame->height / side < maxScale ? frame->height / side : maxScale;
        if (qrScale >= 2) {
            qx = frame->width - side * qrScale - kMargin / 2;
            qy = (frame->height - side * qrScale) / 2;
            textRight = static_cast<int32_t>(qx) - kMargin / 2;
        } else {
            qrScale = 0;
        }
    }
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_clean(screen);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_style_bg_color(screen, lv_color_hex(colors->background), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    label(screen, v->title, titleFont, colors->foreground, kMargin, 10,
          textRight - kMargin - (v->busy ? 40 : 0));
    if (v->busy) {
        label(screen, "...", titleFont, colors->muted, textRight - 32, 10, 32);
    }
    int32_t y = 10 + lv_font_get_line_height(titleFont) + 6;
    if (draw::rule(*colors, screen, kMargin, y - 4, textRight - kMargin, false)) {
        return core::Status::error(EINVAL, "cannot render UI");
    }
    const int32_t lineHeight = lv_font_get_line_height(lineFont) + 2;
    for (size_t i = 0; i < v->lineCount; ++i, y += lineHeight) {
        label(screen, v->lines[i], lineFont, colors->muted, kMargin, y, textRight - kMargin);
    }
    if (v->code[0]) { // the pairing code, large on a highlighted panel, as on the phone
        const lv_font_t *codeFont = &lv_font_montserrat_48;
        const int32_t panelHeight = lv_font_get_line_height(codeFont) + 16;
        lv_obj_t *panel = draw::row(*colors, screen, kMargin, y + 6, textRight - kMargin,
                                    panelHeight, true, false);
        lv_obj_t *digits = panel ? label(panel, v->code, codeFont, colors->foreground, 0, 8,
                                         textRight - 2 * kMargin)
                                 : NULL;
        if (!digits) {
            return core::Status::error(EINVAL, "cannot render UI");
        }
        lv_obj_set_style_text_align(digits, LV_TEXT_ALIGN_CENTER, 0);
        y += panelHeight + 12;
    }
    if (v->progress >= 0 && textRight > kMargin) {
        if (draw::bar(*colors, screen, kMargin, y + 2, textRight - kMargin, v->progress)) {
            return core::Status::error(EINVAL, "cannot render UI");
        }
        y += kBarHeight + 6;
    }
    if (v->itemCount && renderList(screen, v, itemFont, textRight, y + 6, frame->height)) {
        return core::Status::error(EINVAL, "cannot render UI");
    }
    if (v->modal.shown &&
        draw::dialog(*colors, screen, &v->modal, titleFont, lineFont, itemFont,
                     static_cast<int32_t>(frame->width), static_cast<int32_t>(frame->height))) {
        return core::Status::error(EINVAL, "cannot render UI");
    }
    target = frame;
    lv_obj_invalidate(screen);
    lv_refr_now(display);
    target = NULL;
    return qrScale ? qr->render(*frame, qx, qy, qrScale) : core::Status();
}

} // namespace gui
} // namespace nbt
