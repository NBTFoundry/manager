// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/language_ui.hpp"

#include "nbt/gui/reply_parser.hpp"
#include "nbt/gui/view_builder.hpp"
#include "nbt/log.h"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace gui {
namespace {

/* A choice as listed: a language by its own name; Automatic with the
 * language it shows now, English when iDrive's is unknown or not offered. */
void choiceName(const UiController &ui, int choice, char *out, size_t size) {
    if (model::languageCode(choice)) {
        ViewBuilder::formatText(out, size, "%s", model::languageName(choice));
    } else {
        textf(out, size, T_LANGUAGE_AUTO_NAMED,
              model::languageName(model::languageEffective(model::kLanguageAuto, ui.carLanguage)));
    }
}

void backToSettings(UiController &ui) {
    ui.go(Settings);
    ui.focus = ui.managerEntry(EntryLanguage);
}

} // namespace

bool languagePress(UiController &ui, Key key) {
    if (ui.screen == Settings) {
        if (!ui.managerSettings() || key != KeySelect ||
            ui.focus != ui.managerEntry(EntryLanguage)) {
            return false;
        }
        ui.message[0] = 0;
        ui.go(LanguagePick);
        ui.focus =
            model::languageCode(ui.languageChoice) ? 1 + static_cast<size_t>(ui.languageChoice) : 0;
        return true;
    }
    if (ui.screen != LanguagePick) {
        return false;
    }
    if (key == KeyBack || ui.focus > model::kLanguageCount) {
        backToSettings(ui);
        return true;
    }
    ui.languagePick = ui.focus ? static_cast<int>(ui.focus) - 1 : model::kLanguageAuto;
    ui.queued = R_LANGUAGE;
    return true;
}

bool languageRequest(const UiController &ui, int kind, Request &request) {
    if (kind != R_LANGUAGE) {
        return false;
    }
    snprintf(request.command, sizeof(request.command), "SET manager language %s",
             model::languageSettingCode(ui.languagePick));
    return true;
}

int languageReply(UiController &ui, const char *reply) {
    const bool saved = !strcmp(reply, "OK");
    if (!saved && !ReplyParser::isError(reply)) {
        ui.fail(1, text(T_WORKER_PROTOCOL_ERROR));
        return -1;
    }
    backToSettings(ui);
    if (saved) {
        ui.languageChoice = ui.languagePick;
        ui.applyLanguage();
        nbt_logf("language %s", model::languageSettingCode(ui.languageChoice));
    } else {
        textf(ui.message, sizeof(ui.message), T_CANNOT_SAVE_SETTING);
    }
    return 0;
}

void languageView(const UiController &ui, UiView &v) {
    textf(v.title, sizeof(v.title), T_LANGUAGE);
    textf(v.lines[v.lineCount++], sizeof(v.lines[0]), T_LANGUAGE_HINT);
    for (int choice = model::kLanguageAuto; choice < static_cast<int>(model::kLanguageCount);
         ++choice) {
        char name[kTextMax + 1];
        choiceName(ui, choice, name, sizeof(name));
        if (choice == ui.languageChoice) {
            textf(v.items[v.itemCount++], sizeof(v.items[0]), T_CURRENT_CHOICE, name);
        } else {
            ViewBuilder::formatText(v.items[v.itemCount++], sizeof(v.items[0]), "%s", name);
        }
    }
}

void languageEntry(const UiController &ui, char *out, size_t size) {
    char name[kTextMax + 1];
    choiceName(ui, ui.languageChoice, name, sizeof(name));
    textf(out, size, T_SETTING_ROW, text(T_LANGUAGE), name);
}

const char *settingLabel(const UiController &ui, const nbt_setting &setting) {
    if (ui.managerSettings()) { // src/lifecycle/manager_settings.cpp
        if (!strcmp(setting.key, "debug_log")) {
            return text(T_SETTING_DEBUG_LOG);
        }
        if (!strcmp(setting.key, "invert")) {
            return text(T_SETTING_INVERT);
        }
        if (!strcmp(setting.key, "release")) {
            return text(T_SETTING_RELEASE);
        }
        if (!strcmp(setting.key, "native_audio")) {
            return text(T_SETTING_NATIVE_AUDIO);
        }
    }
    return setting.label;
}

const char *settingChoice(const UiController &ui, const char *choice) {
    static const struct {
        const char *choice;
        TextId text;
    } kChoices[] = {{"on", T_VALUE_ON},
                    {"off", T_VALUE_OFF},
                    {"interrupt", T_VALUE_AUDIO_INTERRUPT},
                    {"media", T_VALUE_AUDIO_MEDIA}};
    for (size_t i = 0; ui.managerSettings() && i < sizeof(kChoices) / sizeof(kChoices[0]); ++i) {
        if (!strcmp(choice, kChoices[i].choice)) {
            return text(kChoices[i].text);
        }
    }
    return choice;
}

} // namespace gui
} // namespace nbt
