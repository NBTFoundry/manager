// SPDX-License-Identifier: MPL-2.0
/* The GUI session and the touch screen: taken only while the manager's own
 * screen is on the panel, given back to iDrive otherwise
 * (include/nbt/gui/touch_input.hpp, docs/controller-input.md). */
#include "nbt/display/layer_manager.hpp"
#include "nbt/gui/gui_session.hpp"
#include "nbt/gui/ui_renderer.hpp"
#include "nbt/log.h"

#include <stdlib.h>

namespace nbt {
namespace gui {
namespace {

uint16_t touchPort() {
#ifdef NBT_TESTING
    const char *port = getenv("NBT_TEST_TOUCH_PORT");
    return port ? static_cast<uint16_t>(atoi(port)) : 0;
#elif defined(__QNXNTO__)
    return kTouchPort;
#else
    return 0;
#endif
}

} // namespace

void GuiSession::serviceTouch() {
    const bool mine = !hidden_ && panel_.onPanel(display::kManagerSurface);
    const size_t panelWidth = screen_.targetWidth(), panelHeight = screen_.targetHeight();
    const Frame &frame = screen_.frame();
    touchSocket_.setPort(touchPort());
    if (touchSocket_.own(mine && panelWidth && panelHeight && frame.height)) {
        nbt_logf(touchSocket_.owned() ? "touch: taken while the manager is shown"
                                      : "touch: back with iDrive");
        touchFrames_ = TouchFrames();
        touchTracker_.reset();
    }
    if (!touchSocket_.owned()) {
        return;
    }
    // A row's height on the panel per step; a tap forgives a twentieth of the
    // panel's height; a quarter of its width to the right is BACK.
    touchTracker_.configure(
        static_cast<int>(static_cast<size_t>(UiRenderer::rowHeight()) * panelHeight / frame.height),
        static_cast<int>(panelHeight / 20), static_cast<int>(panelWidth / 4));
    uint8_t datagram[512];
    size_t size = 0;
    for (unsigned n = 0; n < 32 && (size = touchSocket_.receive(datagram, sizeof(datagram))); ++n) {
        TouchSample samples[4];
        const int count = touchFrames_.feed(datagram, size, samples, 4);
        for (int i = 0; i < count; ++i) {
            applyTouch(touchTracker_.feed(samples[i]));
        }
    }
}

void GuiSession::applyTouch(const TouchGesture &gesture) {
    if (gesture.kind == TouchGesture::None) {
        return;
    }
    deadlineMs_ = nowMs_ + kIdleMs;
    if (gesture.kind == TouchGesture::Back) {
        nbt_logf("touch: swipe, back");
        ui_.press(KeyBack);
        return;
    }
    if (gesture.kind == TouchGesture::Step) {
        const Key key = gesture.steps > 0 ? KeyDown : KeyUp;
        for (int i = gesture.steps > 0 ? gesture.steps : -gesture.steps; i > 0; --i) {
            ui_.press(key);
        }
        return;
    }
    UiView now;
    if (!ui_.view(now).ok()) {
        return;
    }
    if (now.modal.shown) { // a dialog: a tap anywhere is its one answer
        if (now.modal.itemCount) {
            nbt_logf("touch: tap, the dialog's answer");
            ui_.press(KeySelect);
            ui_.release(KeySelect);
        }
        return;
    }
    const Frame &frame = screen_.frame();
    const int item = UiRenderer::itemAt(
        static_cast<int>(static_cast<size_t>(gesture.x) * frame.width / screen_.targetWidth()),
        static_cast<int>(static_cast<size_t>(gesture.y) * frame.height / screen_.targetHeight()));
    if (item < 0) {
        return;
    }
    // As the controller would: turn to the item, then push. Every screen keeps
    // its own focus, and the keys are what they all understand.
    for (size_t turns = 0; turns < kItems && now.focus != static_cast<size_t>(item); ++turns) {
        const size_t before = now.focus;
        ui_.press(now.focus < static_cast<size_t>(item) ? KeyDown : KeyUp);
        if (!ui_.view(now).ok() || now.focus == before) {
            return; // the focus does not move there
        }
    }
    nbt_logf("touch: tap, item %d", item + 1);
    ui_.press(KeySelect);
    ui_.release(KeySelect);
}

} // namespace gui
} // namespace nbt
