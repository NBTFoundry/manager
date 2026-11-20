// SPDX-License-Identifier: MPL-2.0
#include "car_language.hpp"
#include "car_data.hpp"
#include "nbt/gui/car_language.hpp"
#include "nbt/log.h"
#include <stddef.h>
namespace nbt {
namespace plugin {
namespace {
/* Off: the plugin reads no car data for the language and the manager shows
 * English unless a language is chosen in its Settings. */
const bool kCarLanguage = true;
const uint64_t kAskMs = 5000, kRefreshMs = 60000;
const char kProperty[] = "vehicle.language";
/* UTF-16 car data to ASCII; false for anything else or too long. */
bool ascii(const bmw::BasicString<unsigned short> &value, char *out, size_t size) {
    const unsigned short *p = value.data();
    if (!p) {
        return false;
    }
    for (size_t i = 0; i < size; ++i) {
        if (p[i] > 126 || (p[i] && p[i] < 32)) {
            return false;
        }
        out[i] = static_cast<char>(p[i]);
        if (!p[i]) {
            return true;
        }
    }
    return false;
}
} // namespace
void CarLanguage::start(bmw::PluginApp *instance, uint64_t now) {
    app = NULL;
    nextAsk = now + 2000;
    written = -1;
    known = false;
    if (!kCarLanguage) {
        return;
    }
    app = instance;
    bmw::BasicString<char> name(kProperty);
    bmw::CdsDelegate handler = {this, &CarLanguage::onLanguage};
    app->cdsAddPropertyChangedEventHandler(name, 0, handler);
}
void CarLanguage::stop() {
    if (!app) {
        return;
    }
    bmw::BasicString<char> name(kProperty);
    app->cdsRemovePropertyChangedEventHandler(name);
    app = NULL;
}
void CarLanguage::poll(uint64_t now) {
    if (!app || now < nextAsk) {
        return;
    }
    nextAsk = now + (known ? kRefreshMs : kAskMs);
    bmw::BasicString<char> name(kProperty);
    bmw::CdsDelegate handler = {this, &CarLanguage::onLanguage};
    app->cdsGetPropertyAsync(name, handler);
}
bool CarLanguage::onLanguage(void *self, const bmw::BasicString<unsigned short> &value) {
    CarLanguage &language = *static_cast<CarLanguage *>(self);
    CarData::offer("vehicle.language", value);
    char text[33];
    unsigned number = 0;
    if (!ascii(value, text, sizeof(text)) || !gui::carLanguageParse(text, number)) {
        return true;
    }
    language.known = true;
    if (static_cast<int>(number) != language.written) {
        if (gui::carLanguageWrite(gui::kCarLanguagePath, number)) {
            language.written = static_cast<int>(number);
        }
        (void)nbt_log_open_path(NBT_LOG_PATH, "plugin");
        nbt_logf("iDrive language %u%s", number,
                 language.written == static_cast<int>(number) ? "" : " (not recorded)");
    }
    return true;
}
} // namespace plugin
} // namespace nbt
