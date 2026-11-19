#include "nbt/gui/phone_ui.hpp"
#include "nbt/gui/setup_ui.hpp"
#include <stdio.h>
namespace nbt {
namespace gui {
void phoneView(const UiController &ui, UiView &v) {
    if (ui.screen == RestartIdrive) {
        setupView(v);
    } else if (ui.screen == Pairing) {
        snprintf(v.title, sizeof(v.title), "Pair a phone");
        snprintf(v.lines[v.lineCount++], sizeof(v.lines[0]), "Scan this Wi-Fi code in NBT Manager");
        snprintf(v.lines[v.lineCount++], sizeof(v.lines[0]), "Confirm the matching digits here");
        snprintf(v.lines[v.lineCount++], sizeof(v.lines[0]), "Closes in %u s", ui.pairingLeftS);
        snprintf(v.items[v.itemCount++], sizeof(v.items[0]), "Cancel");
    } else if (ui.screen == Waiting) {
        snprintf(v.title, sizeof(v.title), "Install or update");
        snprintf(v.lines[v.lineCount++], sizeof(v.lines[0]), "Waiting for a paired phone");
        if (ui.message[0]) {
            snprintf(v.lines[v.lineCount++], sizeof(v.lines[0]), "%s", ui.message);
        }
        snprintf(v.items[v.itemCount++], sizeof(v.items[0]), "Pair a phone");
        snprintf(v.items[v.itemCount++], sizeof(v.items[0]), "Cancel");
    } else if (ui.screen == PhoneConfirm) {
        snprintf(v.title, sizeof(v.title), "Pair %s?", ui.phoneName);
        snprintf(v.lines[v.lineCount++], sizeof(v.lines[0]),
                 "Confirm only if the phone shows this code:");
        snprintf(v.code, sizeof(v.code), "%.3s %.3s", ui.phoneCode, ui.phoneCode + 3);
        snprintf(v.items[v.itemCount++], sizeof(v.items[0]), "No");
        snprintf(v.items[v.itemCount++], sizeof(v.items[0]), "Yes, pair");
    } else if (ui.screen == Phones) {
        snprintf(v.title, sizeof(v.title), "Paired phones");
        if (!ui.phoneCount) {
            snprintf(v.lines[v.lineCount++], sizeof(v.lines[0]), "No paired phones");
        }
        for (size_t i = 0; i < ui.phoneCount; ++i) {
            snprintf(v.items[v.itemCount++], sizeof(v.items[0]), "%s", ui.phoneNames[i]);
        }
        snprintf(v.items[v.itemCount++], sizeof(v.items[0]), "Back");
    } else if (ui.screen == PhoneForget && ui.phoneSelected < ui.phoneCount) {
        snprintf(v.title, sizeof(v.title), "Remove %s?", ui.phoneNames[ui.phoneSelected]);
        snprintf(v.lines[v.lineCount++], sizeof(v.lines[0]), "This phone will need to pair again");
        snprintf(v.items[v.itemCount++], sizeof(v.items[0]), "No");
        snprintf(v.items[v.itemCount++], sizeof(v.items[0]), "Yes, remove phone");
    }
}
} // namespace gui
} // namespace nbt
