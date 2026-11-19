#ifndef NBT_GUI_SCREEN_WINDOW_HPP
#define NBT_GUI_SCREEN_WINDOW_HPP

#include "nbt/core/status.hpp"
#include "nbt/gui/frame.hpp"
#include "nbt/qnx_screen.h"

#include <memory>
#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace gui {

class ScreenWindow {
public:
    ScreenWindow();
    ~ScreenWindow();
    ScreenWindow(const ScreenWindow &) = delete;
    ScreenWindow &operator=(const ScreenWindow &) = delete;

    /* frame() is width x height. A positive `surface` names the window for
     * LayerManager (see nbt/display/layer_manager.hpp): the window then fills
     * the panel as NBT-AAD's do, the frame drawn in it at the largest whole
     * scale, centred on black. LayerManager alone decides what such a window
     * shows, and only the manager GUI puts a surface on the panel
     * (display::Placement): setVisible() leaves its Screen visibility alone. */
    core::Status open(int width, int height, int surface = 0, bool alpha = false,
                      int panelWidth = 0, int panelHeight = 0);
    core::Status post();
    core::Status setVisible(bool visible);
    void close();
    Frame &frame() {
        return frame_;
    }
    size_t targetWidth() const {
        return target_.width;
    }
    size_t targetHeight() const {
        return target_.height;
    }
    bool opened() const {
        return context_ != NULL;
    }

private:
    nbt_screen_context context_;
    nbt_screen_window window_;
    nbt_screen_buffer buffer_;
    std::unique_ptr<unsigned char[]> canvas_;
    Frame frame_;  // what callers draw: canvas_
    Frame target_; // the window's buffer
    size_t scale_, offsetX_, offsetY_;
    bool visible_;
    int surface_;
};

} // namespace gui
} // namespace nbt

#endif
