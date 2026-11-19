#include "nbt/gui/screen_window.hpp"

#include "nbt/display/layer_manager.hpp"

#include <errno.h>
#include <new>
#include <stdio.h>
#include <string.h>

namespace nbt {
namespace gui {

ScreenWindow::ScreenWindow()
    : context_(NULL), window_(NULL), buffer_(NULL), canvas_(), frame_(), target_(), scale_(1),
      offsetX_(0), offsetY_(0), visible_(false), surface_(0) {
}

ScreenWindow::~ScreenWindow() {
    close();
}

core::Status ScreenWindow::open(int width, int height, int surface, bool alpha, int panelWidth,
                                int panelHeight) {
    close();
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || surface < 0) {
        return core::Status::error(EINVAL, "invalid window size");
    }
    // A surface window fills the panel, its source and destination regions
    // equal, as NBT-AAD's windows do: LayerManager scaling is unproven on the
    // car, so the frame is scaled here. display::show() draws the whole panel
    // from it, so without the panel's size there is no such window.
    int windowWidth = panelWidth > 0 ? panelWidth : width;
    int windowHeight = panelHeight > 0 ? panelHeight : height;
    if (surface && (panelWidth <= 0 || panelHeight <= 0) &&
        !display::panelSize(windowWidth, windowHeight)) {
        return core::Status::error(ENODEV, "panel size unknown or smaller than the frame");
    }
    if (surface && (windowWidth < width || windowHeight < height)) {
        return core::Status::error(ENODEV, "panel size unknown or smaller than the frame");
    }
    frame_.width = static_cast<size_t>(width);
    frame_.height = static_cast<size_t>(height);
    frame_.stride = frame_.width * 4;
    frame_.bytes = frame_.stride * frame_.height;
    canvas_.reset(new (std::nothrow) unsigned char[frame_.bytes]);
    if (!canvas_) {
        frame_ = Frame();
        return core::Status::error(ENOMEM, "no memory for the window's frame");
    }
    memset(canvas_.get(), 0, frame_.bytes);
    frame_.pixels = canvas_.get();
    char id[16];
    snprintf(id, sizeof(id), "%d", surface);
    int size[2] = {windowWidth, windowHeight};
    int stride = 0, bytes = 0;
    int usage = alpha ? (NBT_SCREEN_USAGE_NATIVE | NBT_SCREEN_USAGE_WRITE) : NBT_SCREEN_USAGE_WRITE;
    int format = alpha ? NBT_SCREEN_FORMAT_RGBA8888 : NBT_SCREEN_FORMAT_RGBX8888;
    void *pixels = NULL;
    if (screen_create_context(&context_, NBT_SCREEN_APPLICATION_CONTEXT) ||
        screen_create_window(&window_, context_) ||
        (surface && screen_set_window_property_cv(window_, NBT_SCREEN_PROPERTY_ID_STRING,
                                                  static_cast<int>(strlen(id)), id)) ||
        screen_set_window_property_iv(window_, NBT_SCREEN_PROPERTY_USAGE, &usage) ||
        screen_set_window_property_iv(window_, NBT_SCREEN_PROPERTY_FORMAT, &format) ||
        screen_set_window_property_iv(window_, NBT_SCREEN_PROPERTY_BUFFER_SIZE, size) ||
        screen_set_window_property_iv(window_, NBT_SCREEN_PROPERTY_SIZE, size) ||
        screen_create_window_buffers(window_, 1) ||
        screen_get_window_property_pv(window_, NBT_SCREEN_PROPERTY_RENDER_BUFFERS,
                                      reinterpret_cast<void **>(&buffer_)) ||
        !buffer_ || screen_get_buffer_property_pv(buffer_, NBT_SCREEN_PROPERTY_POINTER, &pixels) ||
        screen_get_buffer_property_iv(buffer_, NBT_SCREEN_PROPERTY_STRIDE, &stride) ||
        screen_get_buffer_property_iv(buffer_, NBT_SCREEN_PROPERTY_SIZE, &bytes) || !pixels ||
        stride <= 0 || bytes <= 0) {
        close();
        return core::Status::fromErrno("screen window creation failed");
    }
    target_.pixels = static_cast<unsigned char *>(pixels);
    target_.bytes = static_cast<size_t>(bytes);
    target_.width = static_cast<size_t>(windowWidth);
    target_.height = static_cast<size_t>(windowHeight);
    target_.stride = static_cast<size_t>(stride);
    if (!target_.valid()) {
        close();
        return core::Status::error(EINVAL, "invalid screen buffer");
    }
    scale_ = target_.width / frame_.width < target_.height / frame_.height
                 ? target_.width / frame_.width
                 : target_.height / frame_.height;
    offsetX_ = (target_.width - frame_.width * scale_) / 2;
    offsetY_ = (target_.height - frame_.height * scale_) / 2;
    memset(target_.pixels, 0, target_.bytes); // black around the frame
    surface_ = surface;
    return core::Status();
}

core::Status ScreenWindow::post() {
    if (!window_ || !buffer_ || !canvas_) {
        return core::Status::error(EBADF, "screen window is closed");
    }
    // open() checked that frame_ times scale_, at the offsets, fits target_.
    const size_t rowBytes = frame_.width * scale_ * 4;
    for (size_t y = 0; y < frame_.height; ++y) {
        const unsigned char *source = frame_.pixels + y * frame_.stride;
        unsigned char *row =
            target_.pixels + (offsetY_ + y * scale_) * target_.stride + offsetX_ * 4;
        if (scale_ == 1) {
            memcpy(row, source, rowBytes);
        } else {
            for (size_t x = 0; x < frame_.width; ++x) {
                for (size_t k = 0; k < scale_; ++k) {
                    memcpy(row + (x * scale_ + k) * 4, source + x * 4, 4);
                }
            }
        }
        for (size_t k = 1; k < scale_; ++k) {
            memcpy(row + k * target_.stride, row, rowBytes);
        }
    }
    const int rectangle[4] = {0, 0, static_cast<int>(target_.width),
                              static_cast<int>(target_.height)};
    if (screen_post_window(window_, buffer_, 1, rectangle, 0)) {
        return core::Status::fromErrno("screen post failed");
    }
    return core::Status();
}

core::Status ScreenWindow::setVisible(bool visible) {
    if (!window_) {
        return core::Status::error(EBADF, "screen window is closed");
    }
    const int value = visible ? 1 : 0;
    if (!surface_ && screen_set_window_property_iv(window_, NBT_SCREEN_PROPERTY_VISIBLE, &value)) {
        return core::Status::fromErrno("screen visibility failed");
    }
    visible_ = visible;
    return core::Status();
}

void ScreenWindow::close() {
    if (window_ && visible_) {
        (void)setVisible(false);
    }
    surface_ = 0;
    if (window_) {
        (void)screen_destroy_window(window_);
    }
    if (context_) {
        (void)screen_destroy_context(context_);
    }
    buffer_ = NULL;
    window_ = NULL;
    context_ = NULL;
    canvas_.reset();
    frame_ = Frame();
    target_ = Frame();
    scale_ = 1;
    offsetX_ = offsetY_ = 0;
    visible_ = false;
}

} // namespace gui
} // namespace nbt
