#include "nbt/gui/view_builder.hpp"
#include "nbt/gui/phone_ui.hpp"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace nbt {
namespace gui {
namespace {

void addItem(UiView *v, const char *label) {
    if (v->itemCount < kItems) {
        ViewBuilder::formatText(v->items[v->itemCount++], sizeof(v->items[0]), "%s", label);
    }
}

void addAction(UiView *v, const char *label) {
    if (v->itemCount < kItems) {
        v->itemStyle[v->itemCount] = ItemAction;
    }
    addItem(v, label);
}

void addLine(UiView *v, const char *format, const char *a, const char *b) {
    if (v->lineCount < kLines) {
        ViewBuilder::formatText(v->lines[v->lineCount++], sizeof(v->lines[0]), format, a, b);
    }
}

// A reason too long for one line goes on two, broken at a space.
void addWrapped(UiView *v, const char *text) {
    size_t cut = strlen(text);
    if (cut > kTextMax) {
        for (cut = kTextMax; cut > kTextMax / 2 && text[cut] != ' ';) {
            --cut;
        }
        cut = text[cut] == ' ' ? cut : kTextMax;
    }
    char first[kTextMax + 1];
    memcpy(first, text, cut);
    first[cut] = 0;
    if (cut) {
        addLine(v, "%s", first, NULL);
    }
    if (text[cut]) {
        addLine(v, "%s", text + cut + (text[cut] == ' ' ? 1 : 0), NULL);
    }
}

void buildMainMenu(const UiController &ui, UiView *v) {
    ViewBuilder::formatText(v->title, sizeof(v->title), "NBT App Manager %s", ui.version);
    for (size_t i = 0; i < ui.appCount; ++i) {
        char label[kItemTextMax + 1];
        if (ui.running[i]) {
            ViewBuilder::formatText(label, sizeof(label), "%s  (%s)", ui.apps[i],
                                    ui.appStatus[i][0] ? ui.appStatus[i] : "running");
        } else {
            ViewBuilder::formatText(label, sizeof(label), "%s", ui.apps[i]);
        }
        addItem(v, label);
    }
    addAction(v, "Settings"); // a paired phone sends packages while this menu shows
    if (ui.message[0]) {
        addLine(v, "%s", ui.message, NULL);
    }
    if (ui.notice[0]) {
        addLine(v, "%s", ui.notice, NULL);
    }
    if (ui.captureNote[0]) {
        addLine(v, "%s", ui.captureNote, NULL);
    }
    if (!ui.appCount) {
        addLine(v, "No applications installed", NULL, NULL);
    } else {
        addLine(v, "Push to start; hold for settings", NULL, NULL);
    }
    if (ui.holding && ui.holdIndex < ui.appCount && ui.clockMs >= ui.holdMs &&
        ui.clockMs - ui.holdMs >= kHoldShowMs) {
        const uint64_t held = ui.clockMs - ui.holdMs;
        v->modal.shown = 1;
        ViewBuilder::formatText(v->modal.title, sizeof(v->modal.title), "%s settings",
                                ui.apps[ui.holdIndex]);
        ViewBuilder::formatText(v->modal.text, sizeof(v->modal.text), "Keep holding to open them");
        v->modal.progress = held >= kHoldMs ? 100 : static_cast<int>(held * 100u / kHoldMs);
    }
}

void buildSettings(const UiController &ui, UiView *v) {
    if (ui.managerSettings()) {
        ViewBuilder::formatText(v->title, sizeof(v->title), "Settings");
    } else {
        ViewBuilder::formatText(v->title, sizeof(v->title), "%s settings", ui.settingsApp);
    }
    if (ui.message[0]) {
        addLine(v, "%s", ui.message, NULL);
    }
    if (ui.managerSettings()) {
        addLine(v, "Changes apply when the manager next opens", NULL, NULL);
    } else if (ui.settings.count && ui.liveSettings) {
        addLine(v, "Changes apply at once", NULL, NULL);
    } else if (ui.settings.count) {
        addLine(v, "Changes apply when %s next starts", ui.settingsApp, NULL);
    } else {
        addLine(v, "%s has no settings", ui.settingsApp, NULL);
    }
    for (size_t i = 0; i < ui.settings.count; ++i) {
        char label[kTextMax + 1];
        ViewBuilder::formatText(label, sizeof(label), "%s: %s", ui.settings.items[i].label,
                                ui.settings.items[i].value);
        addItem(v, label);
    }
    if (ui.stopEntry()) {
        char label[kTextMax + 1];
        ViewBuilder::formatText(label, sizeof(label), "Stop %s", ui.settingsApp);
        addAction(v, label);
    }
    if (ui.unblockEntry()) {
        char label[kTextMax + 1];
        ViewBuilder::formatText(label, sizeof(label), "Unblock %s", ui.settingsApp);
        addAction(v, label);
    }
    if (ui.managerSettings()) {
        addAction(v, "Pair a phone");
        addAction(v, "Paired phones");
        addAction(v, "Status"); // status_page.hpp
    } else {
        char label[kTextMax + 1];
        ViewBuilder::formatText(label, sizeof(label), "Uninstall %s", ui.settingsApp);
        addAction(v, label);
    }
    if (!v->itemCount) {
        addAction(v, "Back");
    }
}

// Only what the driver can act on: which application, which versions. The
// approval stays bound to the package's SHA-256 (APPROVE), never shown.
void buildReviewConfirm(const UiController &ui, UiView *v) {
    const bool update = ui.review.installed[0] != 0;
    if (ui.screen == Review) {
        ViewBuilder::formatText(v->title, sizeof(v->title), "%s",
                                update ? "Review update" : "Review new application");
    } else {
        ViewBuilder::formatText(v->title, sizeof(v->title), "%s",
                                ui.recovery ? "Confirm older version" : "Confirm install");
    }
    const bool manager = !strcmp(ui.review.app, "manager");
    addLine(v, "%s", manager ? "NBT Manager" : ui.review.app, NULL);
    if (update) {
        addLine(v, "Installed: %s, new: %s", ui.review.installed, ui.review.proposed);
    } else {
        addLine(v, "Version %s", ui.review.proposed, NULL);
    }
    if (ui.screen == Review) {
        addItem(v, "Reject");
        addItem(v, "Install");
        if (update && !manager) { // only newer manager releases are accepted
            addItem(v, "Install, even if older");
        }
    } else {
        if (ui.recovery) {
            addLine(v, "It may replace it with an older version", NULL, NULL);
        }
        addItem(v, "No");
        addItem(v, "Yes, install");
    }
}

} // namespace

void ViewBuilder::formatText(char *out, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(out, size, format, args);
    va_end(args);
    if (n < 0) {
        out[0] = 0;
    }
}

core::Status ViewBuilder::build(const UiController &ui, UiView &v) {
    memset(&v, 0, sizeof(v));
    v.screen = ui.screen;
    v.progress = -1;
    v.modal.progress = -1;
    // A background poll (every kPollMs on the waiting, pairing and review screens) does
    // not light the indicator; requests the driver started do.
    const bool poll = !ui.queued && (ui.outstanding == R_STATUS || ui.outstanding == R_RUNNING ||
                                     ui.outstanding == R_PHONE_STATUS);
    v.busy =
        (ui.inputBusy() && !poll) || ui.stopping || ui.screen == Loading || ui.screen == Progress;
    switch (ui.screen) {
    case Loading:
        formatText(v.title, sizeof(v.title), "NBT App Manager %s", ui.version);
        addLine(&v, "Loading applications", NULL, NULL);
        break;
    case Apps:
        buildMainMenu(ui, &v);
        break;
    case Settings:
        buildSettings(ui, &v);
        break;
    case Uninstall:
        formatText(v.title, sizeof(v.title), "Uninstall %s?", ui.settingsApp);
        if (ui.queued == R_REMOVE || ui.outstanding == R_REMOVE) {
            addLine(&v, "%s", ui.message, NULL);
        }
        if (ui.phoneAction[0]) {
            addLine(&v, "Requested by %s", ui.phoneName, NULL);
        }
        addLine(&v, "%s and all its data are deleted", ui.settingsApp, NULL);
        addLine(&v, "This cannot be undone", NULL, NULL);
        addItem(&v, "No");
        addItem(&v, "Yes, uninstall");
        break;
    case Choice:
        if (ui.settingIndex < ui.settings.count) {
            const nbt_setting *s = &ui.settings.items[ui.settingIndex];
            formatText(v.title, sizeof(v.title), "%s", s->label);
            if (ui.managerSettings()) {
                addLine(&v, "Settings", NULL, NULL);
            } else {
                addLine(&v, "%s settings", ui.settingsApp, NULL);
            }
            for (size_t i = 0; i < s->choice_count; ++i) {
                char label[kTextMax + 1];
                formatText(label, sizeof(label),
                           strcmp(s->choices[i], s->value) ? "%s" : "%s  (current)", s->choices[i]);
                addItem(&v, label);
            }
        }
        break;
    case Waiting:
    case PhoneConfirm:
    case Phones:
    case PhoneForget:
    case RestartIdrive:
    case Pairing:
        phoneView(ui, v);
        break;
    case Status:
    case GamepadTest:
        statusView(ui, v);
        break;
    case Review:
    case Confirm:
        buildReviewConfirm(ui, &v);
        break;
    case Progress:
        formatText(v.title, sizeof(v.title), "Installing");
        addLine(&v, "%s", ui.message, NULL);
        addWrapped(&v, ui.detail);
        if (ui.healthMs && ui.clockMs >= ui.healthMs) { // it has to run kAppHealthMs
            const uint64_t done = (ui.clockMs - ui.healthMs) * 100u / kAppHealthMs;
            v.progress = done > 99u ? 99 : static_cast<int>(done);
        }
        break;
    case Result:
    case Error:
        formatText(v.title, sizeof(v.title), ui.screen == Result ? "Install or update" : "Error");
        addLine(&v, "%s", ui.message, NULL);
        addWrapped(&v, ui.detail);
        addItem(&v, ui.screen == Result ? "OK" : (ui.fatal ? "Close" : "Retry"));
        break;
    }
    v.focus = (v.itemCount && ui.focus < v.itemCount) ? ui.focus : 0;
    return core::Status();
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
