/* Renders the GUI view model with LVGL into a Frame. */
#include "nbt/gui/ui_renderer.hpp"

#include "lvgl/lvgl.h"
#include "nbt/util.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

namespace nbt {
namespace gui {
namespace {

const uint32_t kBackground = 0x0a0f17, kForeground = 0xffffff, kMuted = 0xa8b3c2,
               kHighlight = 0x1d5fa8, kTrack = 0x26303d, kAction = 0x9cc8f2,
               kActionHighlight = 0x2c6e8a, kDialog = 0x141c27;
const int32_t kDialogWidth = 440, kBarHeight = 8, kScrollbarWidth = 6, kScrollbarGap = 6,
              kMargin = 16;
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

int bar(lv_obj_t *parent, int32_t x, int32_t y, int32_t width, int progress) {
    const int32_t filled = width * progress / 100;
    lv_obj_t *track = box(parent, x, y, width, kBarHeight, kTrack, LV_OPA_COVER, kBarHeight / 2);
    if (!track || (filled > 0 && !box(track, 0, 0, filled, kBarHeight, kForeground, LV_OPA_COVER,
                                      kBarHeight / 2))) {
        return -1;
    }
    return 0;
}

int dialog(lv_obj_t *screen, const Modal *m, const lv_font_t *titleFont, const lv_font_t *lineFont,
           const lv_font_t *itemFont, int32_t width, int32_t height) {
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
            boxWidth, boxHeight, kDialog, LV_OPA_COVER, 8);
    if (!panel) {
        return -1;
    }
    lv_obj_set_style_border_color(panel, lv_color_hex(kMuted), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_scrollable(panel, false);
    int32_t y = kMargin;
    label(panel, m->title, titleFont, kForeground, kMargin, y, boxWidth - 2 * kMargin);
    y += lv_font_get_line_height(titleFont) + 6;
    label(panel, m->text, lineFont, kMuted, kMargin, y, boxWidth - 2 * kMargin);
    y += lv_font_get_line_height(lineFont) + kMargin;
    if (m->progress >= 0) {
        if (bar(panel, kMargin, y, boxWidth - 2 * kMargin, m->progress)) {
            return -1;
        }
        y += kBarHeight + kMargin;
    }
    for (size_t i = 0; i < m->itemCount; ++i, y += rowHeight) {
        lv_obj_t *row = box(panel, kMargin / 2, y, boxWidth - kMargin, rowHeight - 2, kHighlight,
                            i == m->focus ? LV_OPA_COVER : LV_OPA_TRANSP, 4);
        if (!row) {
            return -1;
        }
        label(row, m->items[i], itemFont, kForeground, kMargin / 2, 3, boxWidth - 2 * kMargin);
    }
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
                         kTrack, LV_OPA_COVER, kScrollbarWidth / 2)) {
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
    lv_obj_set_style_bg_color(list, lv_color_hex(kMuted), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list, kScrollbarWidth / 2, LV_PART_SCROLLBAR);
    for (size_t i = 0; i < v->itemCount; ++i) {
        const bool isAction = v->itemStyle[i] == ItemAction;
        const bool isFocus = i == v->focus;
        lv_obj_t *row = box(list, 0, static_cast<int32_t>(i) * rowHeight, rowWidth, rowHeight, 0,
                            LV_OPA_TRANSP, 0);
        lv_obj_t *body =
            row ? box(row, 0, 0, rowWidth, rowHeight - 2, isAction ? kActionHighlight : kHighlight,
                      isFocus ? LV_OPA_COVER : LV_OPA_TRANSP, 4)
                : NULL;
        if (!body) {
            return -1;
        }
        lv_obj_set_scrollable(row, false);
        if (isFocus) {
            focused = row;
        }
        label(body, v->items[i], itemFont, (!isFocus && isAction) ? kAction : kForeground,
              kMargin / 2, 3, rowWidth - 3 * kMargin / 2);
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
    const lv_font_t *titleFont = &lv_font_montserrat_24, *lineFont = &lv_font_montserrat_14,
                    *itemFont = &lv_font_montserrat_18;
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
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    label(screen, v->title, titleFont, kForeground, kMargin, 10,
          textRight - kMargin - (v->busy ? 40 : 0));
    if (v->busy) {
        label(screen, "...", titleFont, kMuted, textRight - 32, 10, 32);
    }
    int32_t y = 10 + lv_font_get_line_height(titleFont) + 6;
    const int32_t lineHeight = lv_font_get_line_height(lineFont) + 2;
    for (size_t i = 0; i < v->lineCount; ++i, y += lineHeight) {
        label(screen, v->lines[i], lineFont, kMuted, kMargin, y, textRight - kMargin);
    }
    if (v->code[0]) { // the pairing code, large on a highlighted panel, as on the phone
        const lv_font_t *codeFont = &lv_font_montserrat_48;
        const int32_t panelHeight = lv_font_get_line_height(codeFont) + 16;
        lv_obj_t *panel = box(screen, kMargin, y + 6, textRight - kMargin, panelHeight, kHighlight,
                              LV_OPA_COVER, 8);
        lv_obj_t *digits =
            panel ? label(panel, v->code, codeFont, kForeground, 0, 8, textRight - 2 * kMargin)
                  : NULL;
        if (!digits) {
            return core::Status::error(EINVAL, "cannot render UI");
        }
        lv_obj_set_style_text_align(digits, LV_TEXT_ALIGN_CENTER, 0);
        y += panelHeight + 12;
    }
    if (v->progress >= 0 && textRight > kMargin) {
        if (bar(screen, kMargin, y + 2, textRight - kMargin, v->progress)) {
            return core::Status::error(EINVAL, "cannot render UI");
        }
        y += kBarHeight + 6;
    }
    if (v->itemCount && renderList(screen, v, itemFont, textRight, y + 6, frame->height)) {
        return core::Status::error(EINVAL, "cannot render UI");
    }
    if (v->modal.shown &&
        dialog(screen, &v->modal, titleFont, lineFont, itemFont, static_cast<int32_t>(frame->width),
               static_cast<int32_t>(frame->height))) {
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
