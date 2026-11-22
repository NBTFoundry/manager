// SPDX-License-Identifier: MPL-2.0
/* The manager GUI's view model: what one frame shows, built from the
 * controller (view_builder.hpp) and drawn by the renderer. */
#ifndef NBT_GUI_UI_VIEW_HPP
#define NBT_GUI_UI_VIEW_HPP

#include "nbt/bridge.h"

#include <stddef.h>

namespace nbt {
namespace gui {

static const size_t kAppStatusMax = NBT_BRIDGE_STATUS_MAX;
static const size_t kIdMax = 48;
/* Bytes of UTF-8 in a title, line or dialog text: translated texts are longer
 * than English ones, and a Chinese or Japanese character takes three bytes.
 * The renderer cuts what does not fit the panel (src/gui/text/en.txt). */
static const size_t kTextMax = 128;
/* An item: an application with its status ("name  (status)"), or a text. */
static const size_t kItemTextMax =
    kIdMax + kAppStatusMax + 4 > kTextMax ? kIdMax + kAppStatusMax + 4 : kTextMax;
/* Display columns of a line before a long reason wraps (ViewBuilder). */
static const size_t kWrapColumns = 52;
static const size_t kLines = 6;
static const size_t kAppsMax = 16;
static const size_t kItems = kAppsMax + 2;
static const size_t kModalItems = 2;

enum Screen {
    Loading,
    Apps,
    Settings,
    Pairing,
    Review,
    Confirm,
    Progress,
    Result,
    Error,
    Choice,
    Uninstall,
    Waiting,
    PhoneConfirm,
    Phones,
    PhoneForget,
    RestartIdrive,
    Status,       // Settings > Status (status_page.hpp)
    GamepadTest,  // its gamepad test
    LanguagePick, // Settings > Language (language_ui.hpp)
    Files,        // an application's files (files_ui.hpp)
    FileDelete,   // Delete <file>?
    UsbFiles,     // its files on USB sticks
    Tools,        // Settings > Tools (tools_ui.hpp): restart the head unit, enable SSH
    SshConfirm,   // Enable SSH?
};
enum Key { KeyUp, KeyDown, KeySelect, KeyBack, KeyOption };
enum ItemStyle { ItemEntry, ItemAction };

const char *screenName(Screen screen);

struct Modal {
    int shown;
    char title[kTextMax + 1];
    char text[kTextMax + 1];
    int progress;
    char items[kModalItems][kTextMax + 1];
    size_t itemCount;
    size_t focus;

    bool valid() const;
    bool operator==(const Modal &other) const;
};

struct UiView {
    Screen screen;
    char title[kTextMax + 1];
    char code[8]; // a pairing code, drawn large and highlighted ("123 456")
    char lines[kLines][kTextMax + 1];
    size_t lineCount;
    char items[kItems][kItemTextMax + 1];
    unsigned char itemStyle[kItems];
    size_t itemCount;
    size_t focus;
    int busy;
    int progress;
    Modal modal;

    bool valid() const;
    void setErrorMessage(const char *message);
    bool operator==(const UiView &other) const;
    bool operator!=(const UiView &other) const {
        return !(*this == other);
    }
};

} // namespace gui
} // namespace nbt

#endif
