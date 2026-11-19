#include "nbt/gui/view_builder.hpp"
#include "nbt/gui/language_ui.hpp"
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

void addText(UiView *v, TextId id, const char *a = NULL, const char *b = NULL) {
    if (v->lineCount < kLines) {
        textf(v->lines[v->lineCount++], sizeof(v->lines[0]), id, a, b);
    }
}

void addLine(UiView *v, const char *line) {
    if (v->lineCount < kLines) {
        ViewBuilder::formatText(v->lines[v->lineCount++], sizeof(v->lines[0]), "%s", line);
    }
}

// A reason too long for one line goes on two, broken at a space when there is
// one in the second half (Chinese and Japanese have none: at a character).
void addWrapped(UiView *v, const char *reason) {
    size_t cut = utf8Fit(reason, kTextMax, kWrapColumns);
    if (reason[cut]) {
        size_t space = cut;
        while (space > cut / 2 && reason[space] != ' ') {
            --space;
        }
        cut = reason[space] == ' ' ? space : cut;
    }
    char first[kTextMax + 1];
    memcpy(first, reason, cut);
    first[cut] = 0;
    if (cut) {
        addLine(v, first);
    }
    if (reason[cut]) {
        addLine(v, reason + cut + (reason[cut] == ' ' ? 1 : 0));
    }
}

void buildMainMenu(const UiController &ui, UiView *v) {
    ViewBuilder::formatText(v->title, sizeof(v->title), "NBT App Manager %s", ui.version);
    for (size_t i = 0; i < ui.appCount; ++i) {
        char label[kItemTextMax + 1];
        if (ui.running[i]) {
            ViewBuilder::formatText(label, sizeof(label), "%s  (%s)", ui.apps[i],
                                    ui.appStatus[i][0] ? ui.appStatus[i] : text(T_RUNNING));
        } else {
            ViewBuilder::formatText(label, sizeof(label), "%s", ui.apps[i]);
        }
        addItem(v, label);
    }
    addAction(v, text(T_SETTINGS)); // a paired phone sends packages while this menu shows
    if (ui.message[0]) {
        addLine(v, ui.message);
    }
    if (ui.notice[0]) {
        addLine(v, ui.notice);
    }
    if (ui.captureNote[0]) {
        addLine(v, ui.captureNote);
    }
    addText(v, ui.appCount ? T_MENU_HINT : T_NO_APPS);
    if (ui.holding && ui.holdIndex < ui.appCount && ui.clockMs >= ui.holdMs &&
        ui.clockMs - ui.holdMs >= kHoldShowMs) {
        const uint64_t held = ui.clockMs - ui.holdMs;
        v->modal.shown = 1;
        textf(v->modal.title, sizeof(v->modal.title), T_APP_SETTINGS, ui.apps[ui.holdIndex]);
        textf(v->modal.text, sizeof(v->modal.text), T_KEEP_HOLDING);
        v->modal.progress = held >= kHoldMs ? 100 : static_cast<int>(held * 100u / kHoldMs);
    }
}

void buildSettings(const UiController &ui, UiView *v) {
    const bool manager = ui.managerSettings();
    textf(v->title, sizeof(v->title), manager ? T_SETTINGS : T_APP_SETTINGS, ui.settingsApp);
    if (ui.message[0]) {
        addLine(v, ui.message);
    }
    addText(v,
            manager                                ? T_APPLY_MANAGER_NEXT
            : ui.settings.count && ui.liveSettings ? T_APPLY_NOW
            : ui.settings.count                    ? T_APPLY_APP_NEXT
                                                   : T_NO_SETTINGS,
            ui.settingsApp);
    char label[kTextMax + 1];
    for (size_t i = 0; i < ui.settings.count; ++i) {
        const nbt_setting &s = ui.settings.items[i];
        textf(label, sizeof(label), T_SETTING_ROW, settingLabel(ui, s), settingChoice(ui, s.value));
        addItem(v, label);
    }
    if (ui.stopEntry()) {
        textf(label, sizeof(label), T_STOP_APP, ui.settingsApp);
        addAction(v, label);
    }
    if (ui.unblockEntry()) {
        textf(label, sizeof(label), T_UNBLOCK_APP, ui.settingsApp);
        addAction(v, label);
    }
    if (manager) {
        languageEntry(ui, label, sizeof(label)); // EntryLanguage, as a setting
        addItem(v, label);
        addAction(v, text(T_PAIR_PHONE));
        addAction(v, text(T_PAIRED_PHONES));
        addAction(v, text(T_STATUS)); // status_page.hpp
    } else {
        textf(label, sizeof(label), T_UNINSTALL_APP, ui.settingsApp);
        addAction(v, label);
    }
    if (!v->itemCount) {
        addAction(v, text(T_BACK));
    }
}

// Only what the driver can act on: which application, which versions. The
// approval stays bound to the package's SHA-256 (APPROVE), never shown.
void buildReviewConfirm(const UiController &ui, UiView *v) {
    const bool update = ui.review.installed[0] != 0;
    if (ui.screen == Review) {
        textf(v->title, sizeof(v->title), update ? T_REVIEW_UPDATE : T_REVIEW_NEW);
    } else {
        textf(v->title, sizeof(v->title), ui.recovery ? T_CONFIRM_OLDER : T_CONFIRM_INSTALL);
    }
    const bool manager = !strcmp(ui.review.app, "manager");
    addLine(v, manager ? "NBT Manager" : ui.review.app);
    if (update) {
        addText(v, T_VERSIONS, ui.review.installed, ui.review.proposed);
    } else {
        addText(v, T_VERSION, ui.review.proposed);
    }
    if (ui.screen == Review) {
        addItem(v, text(T_REJECT));
        addItem(v, text(T_INSTALL));
        if (update && !manager) { // only newer manager releases are accepted
            addItem(v, text(T_INSTALL_OLDER));
        }
    } else {
        if (ui.recovery) {
            addText(v, T_OLDER_WARNING);
        }
        addItem(v, text(T_NO));
        addItem(v, text(T_YES_INSTALL));
    }
}

void buildUninstall(const UiController &ui, UiView *v) {
    textf(v->title, sizeof(v->title), T_UNINSTALL_QUESTION, ui.settingsApp);
    if (ui.queued == R_REMOVE || ui.outstanding == R_REMOVE) {
        addLine(v, ui.message);
    }
    if (ui.phoneAction[0]) {
        addText(v, T_REQUESTED_BY, ui.phoneName);
    }
    addText(v, T_UNINSTALL_DELETES, ui.settingsApp);
    addText(v, T_CANNOT_UNDO);
    addItem(v, text(T_NO));
    addItem(v, text(T_YES_UNINSTALL));
}

void buildChoice(const UiController &ui, UiView *v) {
    if (ui.settingIndex >= ui.settings.count) {
        return;
    }
    const nbt_setting *s = &ui.settings.items[ui.settingIndex];
    ViewBuilder::formatText(v->title, sizeof(v->title), "%s", settingLabel(ui, *s));
    addText(v, ui.managerSettings() ? T_SETTINGS : T_APP_SETTINGS, ui.settingsApp);
    for (size_t i = 0; i < s->choice_count; ++i) {
        char label[kTextMax + 1];
        const char *choice = settingChoice(ui, s->choices[i]);
        if (strcmp(s->choices[i], s->value)) {
            ViewBuilder::formatText(label, sizeof(label), "%s", choice);
        } else {
            textf(label, sizeof(label), T_CURRENT_CHOICE, choice);
        }
        addItem(v, label);
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
    } else if (static_cast<size_t>(n) >= size) {
        utf8TrimEnd(out); // cut at a whole character
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
        addText(&v, T_LOADING_APPS);
        break;
    case Apps:
        buildMainMenu(ui, &v);
        break;
    case Settings:
        buildSettings(ui, &v);
        break;
    case Uninstall:
        buildUninstall(ui, &v);
        break;
    case Choice:
        buildChoice(ui, &v);
        break;
    case LanguagePick:
        languageView(ui, v);
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
        textf(v.title, sizeof(v.title), T_INSTALLING_TITLE);
        addLine(&v, ui.message);
        addWrapped(&v, ui.detail);
        if (ui.healthMs && ui.clockMs >= ui.healthMs) { // it has to run kAppHealthMs
            const uint64_t done = (ui.clockMs - ui.healthMs) * 100u / kAppHealthMs;
            v.progress = done > 99u ? 99 : static_cast<int>(done);
        }
        break;
    case Result:
    case Error:
        textf(v.title, sizeof(v.title), ui.screen == Result ? T_INSTALL_OR_UPDATE : T_ERROR);
        addLine(&v, ui.message);
        addWrapped(&v, ui.detail);
        addItem(&v, text(ui.screen == Result ? T_OK : (ui.fatal ? T_CLOSE : T_RETRY)));
        break;
    }
    v.focus = (v.itemCount && ui.focus < v.itemCount) ? ui.focus : 0;
    return core::Status();
}

} // namespace gui
} // namespace nbt
