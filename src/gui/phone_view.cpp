// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/phone_ui.hpp"
#include "nbt/gui/setup_ui.hpp"
#include "nbt/gui/view_builder.hpp"
#include <stdio.h>
namespace nbt {
namespace gui {
namespace {
void title(UiView &v, TextId id, const char *a = NULL) {
    textf(v.title, sizeof(v.title), id, a);
}
void line(UiView &v, TextId id, const char *a = NULL) {
    textf(v.lines[v.lineCount++], sizeof(v.lines[0]), id, a);
}
void item(UiView &v, TextId id) {
    textf(v.items[v.itemCount++], sizeof(v.items[0]), id);
}
} // namespace
void phoneView(const UiController &ui, UiView &v) {
    if (ui.screen == RestartIdrive) {
        setupView(ui, v);
    } else if (ui.screen == Pairing) {
        char seconds[12];
        snprintf(seconds, sizeof(seconds), "%u", ui.pairingLeftS);
        title(v, T_PAIR_PHONE);
        line(v, T_SCAN_CODE);
        line(v, T_CONFIRM_DIGITS);
        line(v, T_CLOSES_IN, seconds);
        item(v, T_CANCEL);
    } else if (ui.screen == Waiting) {
        title(v, T_INSTALL_OR_UPDATE);
        line(v, T_WAITING_PHONE);
        if (ui.message[0]) {
            ViewBuilder::formatText(v.lines[v.lineCount++], sizeof(v.lines[0]), "%s", ui.message);
        }
        item(v, T_PAIR_PHONE);
        item(v, T_CANCEL);
    } else if (ui.screen == PhoneConfirm && ui.phoneKey) {
        const bool add = ui.phoneKey == 'k';
        title(v, add ? T_ADD_KEY_QUESTION : T_REMOVE_KEY_QUESTION, ui.keyName);
        line(v, T_REQUESTED_BY, ui.phoneName);
        line(v, T_KEY_FINGERPRINT, ui.keyPrint);
        line(v, add ? T_KEY_WARNING : T_KEY_REMOVE_NOTE);
        item(v, T_NO);
        item(v, add ? T_YES_ADD_KEY : T_YES_REMOVE_KEY);
    } else if (ui.screen == PhoneConfirm) {
        title(v, T_PAIR_QUESTION, ui.phoneName);
        line(v, T_CONFIRM_CODE);
        snprintf(v.code, sizeof(v.code), "%.3s %.3s", ui.phoneCode, ui.phoneCode + 3);
        item(v, T_NO);
        item(v, T_YES_PAIR);
    } else if (ui.screen == Phones) {
        title(v, T_PAIRED_PHONES);
        if (!ui.phoneCount) {
            line(v, T_NO_PHONES);
        }
        for (size_t i = 0; i < ui.phoneCount; ++i) {
            ViewBuilder::formatText(v.items[v.itemCount++], sizeof(v.items[0]), "%s",
                                    ui.phoneNames[i]);
        }
        item(v, T_BACK);
    } else if (ui.screen == PhoneForget && ui.phoneSelected < ui.phoneCount) {
        title(v, T_REMOVE_QUESTION, ui.phoneNames[ui.phoneSelected]);
        line(v, T_PAIR_AGAIN);
        item(v, T_NO);
        item(v, T_YES_REMOVE_PHONE);
    }
}
} // namespace gui
} // namespace nbt
