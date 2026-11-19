// SPDX-License-Identifier: MPL-2.0
#include "nbt/model/language.hpp"

#include "nbt/core/unique_fd.hpp"

#include <errno.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace model {
namespace {

const char *const kCodes[kLanguageCount] = {"en", "fr", "de", "it", "es", "zh", "ja"};
const char *const kNames[kLanguageCount] = {"English", "Français", "Deutsch", "Italiano",
                                            "Español", "中文",     "日本語"};

} // namespace

const char *languageCode(int language) {
    return language >= 0 && static_cast<unsigned>(language) < kLanguageCount ? kCodes[language]
                                                                             : NULL;
}

const char *languageName(int language) {
    return language >= 0 && static_cast<unsigned>(language) < kLanguageCount ? kNames[language]
                                                                             : NULL;
}

bool languageSettingParse(const char *text, int &choice) {
    if (!text) {
        return false;
    }
    if (!strcmp(text, "auto")) {
        choice = kLanguageAuto;
        return true;
    }
    for (unsigned i = 0; i < kLanguageCount; ++i) {
        if (!strcmp(text, kCodes[i])) {
            choice = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

const char *languageSettingCode(int choice) {
    const char *code = languageCode(choice);
    return code ? code : "auto";
}

bool languageFromCar(unsigned value, Language &language) {
    switch (value) {
    case 1:
        language = LangDe;
        return true;
    case 2: // English (UK)
    case 3: // English (US)
        language = LangEn;
        return true;
    case 4:  // Spanish (Spain)
    case 16: // Spanish (Mexico)
        language = LangEs;
        return true;
    case 5:
        language = LangIt;
        return true;
    case 6:  // French (France)
    case 15: // French (Canada)
        language = LangFr;
        return true;
    case 10: // Chinese (Taiwan): the manager has Simplified Chinese only
    case 11: // Chinese (China)
        language = LangZh;
        return true;
    case 13:
        language = LangJa;
        return true;
    default:
        return false;
    }
}

Language languageEffective(int choice, int car) {
    if (languageCode(choice)) {
        return static_cast<Language>(choice);
    }
    return languageCode(car) ? static_cast<Language>(car) : LangEn;
}

int languageSettingLoad(nbt_fs *fs, int &choice) {
    choice = kLanguageAuto;
    if (!fs) {
        errno = EINVAL;
        return -1;
    }
    core::UniqueFd fd(nbt_fs_open(fs, NBT_FS_MANAGER_LANGUAGE, "manager", NULL, NBT_FS_READ));
    if (!fd.valid()) {
        return errno == ENOENT ? 0 : -1;
    }
    char text[kLanguageFileMax + 1];
    size_t used = 0;
    while (used < sizeof(text)) {
        const ssize_t n = ::read(fd.get(), text + used, sizeof(text) - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            break;
        }
        used += static_cast<size_t>(n);
    }
    // One line, newline-terminated; anything else leaves Automatic.
    if (used < 2 || used > kLanguageFileMax || text[used - 1] != '\n' ||
        memchr(text, '\n', used - 1) || memchr(text, 0, used)) {
        return 0;
    }
    text[used - 1] = 0;
    int parsed = kLanguageAuto;
    if (languageSettingParse(text, parsed)) {
        choice = parsed;
    }
    return 0;
}

} // namespace model
} // namespace nbt
