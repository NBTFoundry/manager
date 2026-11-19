#include "nbt/sdk/settings.hpp"

#include "nbt/core/text.hpp"
#include "nbt/core/unique_fd.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace sdk {
namespace {

const size_t kValuesTextMax = 16 + NBT_SETTINGS_MAX * (NBT_SETTING_TEXT_MAX + 10);

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

} // namespace

core::Status SettingsList::applyValues(core::Span<const char> text) {
    char copy[kValuesTextMax];
    if (!core::copyTerminated(core::StringRef(text.data(), text.size()), core::Span<char>(copy))
             .ok() ||
        strncmp(copy, "NBTVALUES1\n", 11)) {
        return core::Status::error(core::kInvalidArgument, "invalid settings values");
    }
    SettingsList applied = *this;
    for (char *p = copy + 11; *p;) {
        char *line = p;
        char *end = strchr(line, '\n');
        if (!end) {
            return core::Status::error(core::kInvalidArgument, "unterminated settings value line");
        }
        *end = 0;
        char *equals = strchr(line, '=');
        if (!equals) {
            return core::Status::error(core::kInvalidArgument, "malformed settings value line");
        }
        *equals = 0;
        const char *value = equals + 1;
        if (!keyValid(line, strlen(line)) || !choiceValid(value, strlen(value))) {
            return core::Status::error(core::kInvalidArgument, "invalid settings key or choice");
        }
        const int index = applied.find(core::StringRef(line));
        if (index >= 0) {
            nbt_setting &item = applied.values_.items[index];
            if (Setting(item).allows(core::StringRef(value))) {
                strcpy(item.value, value);
            }
        }
        p = end + 1;
    }
    *this = applied;
    return core::Status();
}

core::Result<size_t> SettingsList::formatValues(core::Span<char> output) const {
    if (values_.count > NBT_SETTINGS_MAX || !output.data() || output.size() < 12) {
        return core::Result<size_t>::failure(
            core::Status::error(core::kInvalidArgument, "invalid settings values buffer"));
    }
    memcpy(output.data(), "NBTVALUES1\n", 12);
    size_t used = 11;
    for (size_t i = 0; i < values_.count; ++i) {
        const int n = snprintf(output.data() + used, output.size() - used, "%s=%s\n",
                               values_.items[i].key, values_.items[i].value);
        if (n < 0 || static_cast<size_t>(n) >= output.size() - used) {
            return core::Result<size_t>::failure(
                core::Status::error(core::kInvalidArgument, "settings values buffer too small"));
        }
        used += static_cast<size_t>(n);
    }
    return core::Result<size_t>::success(used);
}

core::Status Snapshot::read(const char *path, SettingsList *settings) {
    char text[NBT_SETTINGS_VALUES_MAX];
    struct stat st, before;
    if (!path || !settings || lstat(path, &before) || !S_ISREG(before.st_mode)) {
        return core::Status::error(core::kInvalidArgument, "invalid settings snapshot path");
    }
    int flags = O_RDONLY | O_NONBLOCK;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    core::UniqueFd fd(open(path, flags));
    if (!fd.valid()) {
        return core::Status::fromErrno("cannot open settings snapshot");
    }
    if (fcntl(fd.get(), F_SETFD, FD_CLOEXEC) || fstat(fd.get(), &st) || !S_ISREG(st.st_mode) ||
        st.st_uid != geteuid() || st.st_nlink != 1 || (st.st_mode & 0077) ||
        st.st_dev != before.st_dev || st.st_ino != before.st_ino) {
        errno = EPERM;
        return core::Status::error(core::kPermissionDenied, "unsafe settings snapshot file");
    }
    size_t used = 0;
    bool eof = false;
    while (used < sizeof(text)) {
        const ssize_t n = ::read(fd.get(), text + used, sizeof(text) - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            return core::Status::fromErrno("settings snapshot read failed");
        }
        if (n == 0) {
            eof = true;
            break;
        }
        used += static_cast<size_t>(n);
    }
    if (!fd.close().ok() || !eof) {
        return core::Status::error(core::kIoError, "settings snapshot incomplete or oversized");
    }
    return settings->applyValues(core::Span<const char>(text, used));
}

} // namespace sdk
} // namespace nbt

int nbt_settings_values_apply(nbt_settings *settings, const char *text, size_t size) {
    if (!settings || !text) {
        return -1;
    }
    nbt::sdk::SettingsList list;
    list.legacy() = *settings;
    if (!list.applyValues(nbt::core::Span<const char>(text, size)).ok()) {
        return -1;
    }
    *settings = list.legacy();
    return 0;
}

int nbt_settings_values_format(const nbt_settings *settings, char *out, size_t size) {
    if (!settings || !out || !size) {
        return -1;
    }
    nbt::sdk::SettingsList list;
    list.legacy() = *settings;
    nbt::core::Result<size_t> formatted = list.formatValues(nbt::core::Span<char>(out, size));
    return formatted.get() ? static_cast<int>(formatted.value()) : -1;
}

int nbt_settings_values_read(const char *path, nbt_settings *settings) {
    if (!path || !settings) {
        return -1;
    }
    nbt::sdk::SettingsList list;
    list.legacy() = *settings;
    if (!nbt::sdk::Snapshot::read(path, &list).ok()) {
        return -1;
    }
    *settings = list.legacy();
    return 0;
}
