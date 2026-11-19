// SPDX-License-Identifier: MPL-2.0
/* The view model (nbt/gui/ui_view.hpp): what one frame shows. */
#include "nbt/gui/ui_view.hpp"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace gui {

const char *screenName(Screen screen) {
    static const char *const names[] = {
        "loading",        "apps",     "settings",      "pairing", "review",
        "confirm",        "progress", "result",        "error",   "choice",
        "uninstall",      "waiting",  "phone-confirm", "phones",  "phone-forget",
        "restart-idrive", "status",   "gamepad-test",  "language"};
    return static_cast<unsigned>(screen) < sizeof(names) / sizeof(names[0]) ? names[screen]
                                                                            : "unknown";
}

void UiView::setErrorMessage(const char *message) {
    *this = UiView();
    screen = Error;
    snprintf(title, sizeof(title), "NBT MANAGER");
    snprintf(lines[0], sizeof(lines[0]), "%s", message);
    lineCount = 1;
}

bool Modal::valid() const {
    return !shown || (itemCount <= kModalItems && progress >= -1 && progress <= 100 &&
                      (!itemCount || focus < itemCount));
}

bool Modal::operator==(const Modal &other) const {
    return shown == other.shown && progress == other.progress && itemCount == other.itemCount &&
           focus == other.focus && !memcmp(title, other.title, sizeof(title)) &&
           !memcmp(text, other.text, sizeof(text)) && !memcmp(items, other.items, sizeof(items));
}

bool UiView::valid() const {
    return lineCount <= kLines && itemCount <= kItems && progress >= -1 && progress <= 100 &&
           (!itemCount || focus < itemCount) && modal.valid();
}

bool UiView::operator==(const UiView &other) const {
    return screen == other.screen && focus == other.focus && busy == other.busy &&
           progress == other.progress && lineCount == other.lineCount &&
           itemCount == other.itemCount && !memcmp(title, other.title, sizeof(title)) &&
           !memcmp(code, other.code, sizeof(code)) && !memcmp(lines, other.lines, sizeof(lines)) &&
           !memcmp(items, other.items, sizeof(items)) &&
           !memcmp(itemStyle, other.itemStyle, sizeof(itemStyle)) && modal == other.modal;
}

} // namespace gui
} // namespace nbt
