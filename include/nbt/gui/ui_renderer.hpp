// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_UI_RENDERER_HPP
#define NBT_GUI_UI_RENDERER_HPP

#include "nbt/core/status.hpp"
#include "nbt/gui/frame.hpp"
#include "nbt/gui/qr_code.hpp"
#include "nbt/gui/ui_controller.hpp"

namespace nbt {
namespace gui {

class UiRenderer {
public:
    core::Status render(const UiView &view, const QrCode *qr, Frame &frame);
    /* The item of the last frame's list at a point of the frame, or -1. */
    static int itemAt(int x, int y);
    /* That list's row height in frame pixels (a usual one without a list). */
    static int rowHeight();
};

} // namespace gui
} // namespace nbt

#endif
