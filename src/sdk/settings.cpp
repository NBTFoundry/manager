// SPDX-License-Identifier: MPL-2.0
#include "nbt/sdk/settings.hpp"

#include "nbt/core/text.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace nbt {
namespace sdk {
namespace {

const size_t kSchemaMax = 16 + NBT_SETTINGS_MAX * (NBT_SETTING_TEXT_MAX + 10);

bool keyValid(const char *s, size_t n) {
    if (!n || n > NBT_SETTING_KEY_MAX || s[0] < 'a' || s[0] > 'z') {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        const char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

bool choiceValid(const char *s, size_t n) {
    if (!n || n > NBT_SETTING_CHOICE_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        const char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool labelValid(const char *s, size_t n) {
    if (!n || n > NBT_SETTING_LABEL_MAX || s[0] == ' ' || s[n - 1] == ' ') {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        if (s[i] < 32 || s[i] > 126 || s[i] == '|') {
            return false;
        }
    }
    return true;
}

} // namespace

core::Result<Setting> Setting::parse(core::StringRef text) {
    char copy[NBT_SETTING_TEXT_MAX];
    if (!core::copyTerminated(text, core::Span<char>(copy)).ok()) {
        return core::Result<Setting>::failure(
            core::Status::error(core::kInvalidArgument, "invalid setting declaration"));
    }
    const char *part[4];
    size_t length[4];
    const char *p = copy;
    for (size_t i = 0; i < 4; ++i) {
        part[i] = p;
        const char *end = i < 3 ? strchr(p, '|') : p + strlen(p);
        if (!end) {
            return core::Result<Setting>::failure(
                core::Status::error(core::kInvalidArgument, "invalid setting declaration"));
        }
        length[i] = static_cast<size_t>(end - p);
        p = end + (i < 3 ? 1 : 0);
    }
    if (strchr(part[3], '|') || !keyValid(part[0], length[0]) || !labelValid(part[1], length[1]) ||
        !choiceValid(part[2], length[2])) {
        return core::Result<Setting>::failure(
            core::Status::error(core::kInvalidArgument, "invalid setting declaration"));
    }
    nbt_setting parsed;
    memset(&parsed, 0, sizeof(parsed));
    memcpy(parsed.key, part[0], length[0]);
    memcpy(parsed.label, part[1], length[1]);
    memcpy(parsed.value, part[2], length[2]);
    for (p = part[3];;) {
        const char *end = strchr(p, ',');
        const size_t n = end ? static_cast<size_t>(end - p) : strlen(p);
        if (parsed.choice_count == NBT_SETTING_CHOICES_MAX || !choiceValid(p, n)) {
            return core::Result<Setting>::failure(
                core::Status::error(core::kInvalidArgument, "invalid setting choice"));
        }
        memcpy(parsed.choices[parsed.choice_count], p, n);
        for (size_t i = 0; i < parsed.choice_count; ++i) {
            if (!strcmp(parsed.choices[i], parsed.choices[parsed.choice_count])) {
                return core::Result<Setting>::failure(
                    core::Status::error(core::kInvalidArgument, "duplicate setting choice"));
            }
        }
        ++parsed.choice_count;
        if (!end) {
            break;
        }
        p = end + 1;
    }
    Setting result(parsed);
    if (!result.allows(core::StringRef(parsed.value))) {
        return core::Result<Setting>::failure(
            core::Status::error(core::kInvalidArgument, "default choice not in setting choices"));
    }
    return core::Result<Setting>::success(result);
}

core::Result<size_t> Setting::format(core::Span<char> output) const {
    if (!output.data() || !output.size()) {
        return core::Result<size_t>::failure(
            core::Status::error(core::kInvalidArgument, "invalid setting output buffer"));
    }
    int n =
        snprintf(output.data(), output.size(), "%s|%s|%s|", value_.key, value_.label, value_.value);
    if (n < 0 || static_cast<size_t>(n) >= output.size()) {
        return core::Result<size_t>::failure(
            core::Status::error(core::kInvalidArgument, "setting buffer too small"));
    }
    size_t used = static_cast<size_t>(n);
    for (size_t i = 0; i < value_.choice_count; ++i) {
        n = snprintf(output.data() + used, output.size() - used, "%s%s", i ? "," : "",
                     value_.choices[i]);
        if (n < 0 || static_cast<size_t>(n) >= output.size() - used) {
            return core::Result<size_t>::failure(
                core::Status::error(core::kInvalidArgument, "setting buffer too small"));
        }
        used += static_cast<size_t>(n);
    }
    return core::Result<size_t>::success(used);
}

bool Setting::allows(core::StringRef value) const {
    char copy[NBT_SETTING_CHOICE_MAX + 1];
    if (!core::copyTerminated(value, core::Span<char>(copy)).ok()) {
        return false;
    }
    for (size_t i = 0; i < value_.choice_count; ++i) {
        if (!strcmp(value_.choices[i], copy)) {
            return true;
        }
    }
    return false;
}

int SettingsList::find(core::StringRef key) const {
    char copy[NBT_SETTING_KEY_MAX + 1];
    if (!core::copyTerminated(key, core::Span<char>(copy)).ok()) {
        return -1;
    }
    for (size_t i = 0; i < values_.count; ++i) {
        if (!strcmp(values_.items[i].key, copy)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

core::Status SettingsList::add(core::StringRef declaration) {
    if (values_.count >= NBT_SETTINGS_MAX) {
        return core::Status::error(core::kInvalidArgument, "settings list full");
    }
    core::Result<Setting> parsed = Setting::parse(declaration);
    if (!parsed.get() || find(core::StringRef(parsed.value().legacy().key)) >= 0) {
        return core::Status::error(core::kInvalidArgument,
                                   "invalid or duplicate setting declaration");
    }
    values_.items[values_.count++] = parsed.value().legacy();
    return core::Status();
}

core::Result<SettingsList> SettingsList::parseSchema(core::Span<const char> text) {
    char copy[kSchemaMax];
    if (!core::copyTerminated(core::StringRef(text.data(), text.size()), core::Span<char>(copy))
             .ok() ||
        strncmp(copy, "NBTSETTINGS1\n", 13)) {
        return core::Result<SettingsList>::failure(
            core::Status::error(core::kInvalidArgument, "invalid settings schema"));
    }
    SettingsList result;
    for (char *p = copy + 13; *p;) {
        char *value = nbt_field(&p, "setting");
        if (!value || !result.add(core::StringRef(value)).ok()) {
            return core::Result<SettingsList>::failure(
                core::Status::error(core::kInvalidArgument, "invalid settings schema"));
        }
    }
    return core::Result<SettingsList>::success(result);
}

core::Result<size_t> SettingsList::formatSchema(core::Span<char> output) const {
    if (values_.count > NBT_SETTINGS_MAX || !output.data() || output.size() < 14) {
        return core::Result<size_t>::failure(
            core::Status::error(core::kInvalidArgument, "invalid settings schema buffer"));
    }
    memcpy(output.data(), "NBTSETTINGS1\n", 14);
    size_t used = 13;
    for (size_t i = 0; i < values_.count; ++i) {
        if (output.size() - used < 10) {
            return core::Result<size_t>::failure(
                core::Status::error(core::kInvalidArgument, "settings schema buffer too small"));
        }
        memcpy(output.data() + used, "setting=", 8);
        used += 8;
        core::Result<size_t> formatted =
            Setting(values_.items[i]).format(output.slice(used, output.size() - used));
        if (!formatted.get() || formatted.value() + 2 > output.size() - used) {
            return core::Result<size_t>::failure(
                core::Status::error(core::kInvalidArgument, "settings schema buffer too small"));
        }
        used += formatted.value();
        output.data()[used++] = '\n';
        output.data()[used] = 0;
    }
    return core::Result<size_t>::success(used);
}

} // namespace sdk
} // namespace nbt

int nbt_setting_allows(const nbt_setting *setting, const char *value) {
    if (!setting || !value) {
        return 0;
    }
    return nbt::sdk::Setting(*setting).allows(nbt::core::StringRef(value)) ? 1 : 0;
}

int nbt_setting_parse(const char *text, nbt_setting *out) {
    if (!text || !out) {
        return -1;
    }
    nbt::core::Result<nbt::sdk::Setting> parsed =
        nbt::sdk::Setting::parse(nbt::core::StringRef(text));
    if (!parsed.get()) {
        return -1;
    }
    *out = parsed.value().legacy();
    return 0;
}

int nbt_setting_format(const nbt_setting *setting, char *out, size_t size) {
    if (!setting || !out || !size) {
        return -1;
    }
    nbt::core::Result<size_t> formatted =
        nbt::sdk::Setting(*setting).format(nbt::core::Span<char>(out, size));
    return formatted.get() ? static_cast<int>(formatted.value()) : -1;
}

int nbt_settings_find(const nbt_settings *settings, const char *key) {
    if (!settings || !key) {
        return -1;
    }
    nbt::sdk::SettingsList list;
    list.legacy() = *settings;
    return list.find(nbt::core::StringRef(key));
}

int nbt_settings_add(nbt_settings *settings, const char *text) {
    if (!settings || !text) {
        return -1;
    }
    nbt::sdk::SettingsList list;
    list.legacy() = *settings;
    if (!list.add(nbt::core::StringRef(text)).ok()) {
        return -1;
    }
    *settings = list.legacy();
    return 0;
}

int nbt_settings_schema_parse(const char *text, size_t size, nbt_settings *out) {
    if (!text || !out) {
        return -1;
    }
    nbt::core::Result<nbt::sdk::SettingsList> parsed =
        nbt::sdk::SettingsList::parseSchema(nbt::core::Span<const char>(text, size));
    if (!parsed.get()) {
        return -1;
    }
    *out = parsed.value().legacy();
    return 0;
}

int nbt_settings_schema_format(const nbt_settings *settings, char *out, size_t size) {
    if (!settings || !out || !size) {
        return -1;
    }
    nbt::sdk::SettingsList list;
    list.legacy() = *settings;
    nbt::core::Result<size_t> formatted = list.formatSchema(nbt::core::Span<char>(out, size));
    return formatted.get() ? static_cast<int>(formatted.value()) : -1;
}
