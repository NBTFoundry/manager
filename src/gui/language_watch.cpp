// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/language_watch.hpp"

#include "nbt/gui/car_language.hpp"
#include "nbt/log.h"

#include <errno.h>

namespace nbt {
namespace gui {

void LanguageWatch::start(const char *root, const char *carPath, UiController &ui) {
    path_ = carPath;
    nbt_fs fs;
    char error[160];
    int choice = model::kLanguageAuto;
    if (!root || nbt_fs_init(&fs, root, error, sizeof(error))) {
        nbt_logf("language setting: no manager root; automatic");
    } else if (model::languageSettingLoad(&fs, choice)) {
        nbt_logf("language setting unreadable (errno %d); automatic", errno);
    }
    ui.languageChoice = choice;
    readCar(ui);
    ui.applyLanguage();
    nbt_logf("language %s, iDrive %d, shown %s", model::languageSettingCode(choice), seen_,
             model::languageCode(currentLanguage()));
}

void LanguageWatch::poll(UiController &ui, uint64_t nowMs) {
    if (nowMs < nextMs_) {
        return;
    }
    nextMs_ = nowMs + 1000;
    const int before = seen_;
    readCar(ui);
    if (seen_ != before) {
        ui.applyLanguage();
        nbt_logf("iDrive language %d; shown %s", seen_, model::languageCode(currentLanguage()));
    }
}

void LanguageWatch::readCar(UiController &ui) {
    unsigned value = 0;
    model::Language language = model::LangEn;
    const bool known = path_ && carLanguageRead(path_, value);
    seen_ = known ? static_cast<int>(value) : -1;
    ui.carLanguage = known && model::languageFromCar(value, language) ? language : -1;
}

} // namespace gui
} // namespace nbt
