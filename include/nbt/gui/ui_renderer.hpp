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
};

} // namespace gui
} // namespace nbt

#endif
