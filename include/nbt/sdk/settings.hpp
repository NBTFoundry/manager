#ifndef NBT_SDK_SETTINGS_HPP
#define NBT_SDK_SETTINGS_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/settings.h"

namespace nbt {
namespace sdk {

class Setting {
public:
    Setting() : value_() {
    }
    explicit Setting(const nbt_setting &value) : value_(value) {
    }
    static core::Result<Setting> parse(core::StringRef text);
    core::Result<size_t> format(core::Span<char> output) const;
    bool allows(core::StringRef value) const;
    const char *key() const {
        return value_.key;
    }
    const char *label() const {
        return value_.label;
    }
    const char *value() const {
        return value_.value;
    }
    const nbt_setting &legacy() const {
        return value_;
    }

private:
    nbt_setting value_;
};

class SettingsList {
public:
    SettingsList() : values_() {
    }
    explicit SettingsList(const nbt_settings &values) : values_(values) {
    }
    core::Status add(core::StringRef declaration);
    static core::Result<SettingsList> parseSchema(core::Span<const char> text);
    core::Result<size_t> formatSchema(core::Span<char> output) const;
    core::Status applyValues(core::Span<const char> text);
    core::Result<size_t> formatValues(core::Span<char> output) const;
    int find(core::StringRef key) const;
    size_t size() const {
        return values_.count;
    }
    const nbt_settings &legacy() const {
        return values_;
    }
    nbt_settings &legacy() {
        return values_;
    }

private:
    nbt_settings values_;
};

class Snapshot {
public:
    static core::Status read(const char *path, SettingsList *settings);
};

} // namespace sdk
} // namespace nbt

#endif
