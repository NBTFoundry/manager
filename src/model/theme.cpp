// SPDX-License-Identifier: MPL-2.0
#include "nbt/model/theme.hpp"

#include "nbt/core/unique_fd.hpp"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace model {
namespace {

const char *const kCodes[kThemeCount] = {"bmw", "mini", "supra"};
const char *const kNames[kThemeCount] = {"BMW", "MINI", "Supra"};

char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

/* `word` (lower case) appears in the `n` bytes at `s`, whatever their case. */
bool contains(const char *s, size_t n, const char *word) {
    const size_t length = strlen(word);
    for (size_t i = 0; i + length <= n; ++i) {
        size_t k = 0;
        while (k < length && lower(s[i + k]) == word[k]) {
            ++k;
        }
        if (k == length) {
            return true;
        }
    }
    return false;
}

/* A make named in a value: Toyota is the Supra, MINI, BMW (BMW i too). */
bool makeOf(const char *s, size_t n, Theme &theme) {
    if (contains(s, n, "toyota")) {
        theme = ThemeSupra;
    } else if (contains(s, n, "mini")) {
        theme = ThemeMini;
    } else if (contains(s, n, "bmw")) {
        theme = ThemeBmw;
    } else {
        return false;
    }
    return true;
}

/* The value of `key=` when the line `s` (n bytes) is that key, else NULL. */
const char *valueOf(const char *s, size_t n, const char *key, size_t &length) {
    const size_t k = strlen(key);
    if (n <= k || memcmp(s, key, k) || s[k] != '=') {
        return NULL;
    }
    length = n - k - 1;
    return s + k + 1;
}

size_t readAll(int fd, char *text, size_t size) {
    size_t used = 0;
    while (used < size) {
        const ssize_t n = ::read(fd, text + used, size - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        used += static_cast<size_t>(n);
    }
    return used;
}

} // namespace

const char *themeCode(int theme) {
    return theme >= 0 && static_cast<unsigned>(theme) < kThemeCount ? kCodes[theme] : NULL;
}

const char *themeName(int theme) {
    return theme >= 0 && static_cast<unsigned>(theme) < kThemeCount ? kNames[theme] : NULL;
}

bool themeSettingParse(const char *text, int &choice) {
    if (!text) {
        return false;
    }
    if (!strcmp(text, "auto")) {
        choice = kThemeAuto;
        return true;
    }
    for (unsigned i = 0; i < kThemeCount; ++i) {
        if (!strcmp(text, kCodes[i])) {
            choice = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

const char *themeSettingCode(int choice) {
    const char *code = themeCode(choice);
    return code ? code : "auto";
}

bool themeFromCarConfig(const char *text, size_t size, Theme &theme) {
    static const char *const kKeys[] = {"OEMIconLabel", "OEMIcon", "Manufacturer"};
    const size_t kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);
    Theme found[sizeof(kKeys) / sizeof(kKeys[0])];
    bool has[sizeof(kKeys) / sizeof(kKeys[0])] = {false, false, false};
    bool general = false;
    if (!text) {
        return false;
    }
    for (size_t at = 0; at < size;) {
        const char *line = text + at;
        const char *end = static_cast<const char *>(memchr(line, '\n', size - at));
        size_t n = end ? static_cast<size_t>(end - line) : size - at;
        at += n + 1;
        if (n && line[n - 1] == '\r') {
            --n;
        }
        if (n && line[0] == '[') {
            general = n == 9 && !memcmp(line, "[General]", 9);
            continue;
        }
        for (size_t k = 0; general && k < kKeyCount; ++k) {
            size_t length = 0;
            const char *value = valueOf(line, n, kKeys[k], length);
            if (value && !has[k]) {
                has[k] = makeOf(value, length, found[k]);
            }
        }
    }
    for (size_t k = 0; k < kKeyCount; ++k) {
        if (has[k]) {
            theme = found[k];
            return true;
        }
    }
    return false;
}

Theme themeEffective(int choice, int car) {
    if (themeCode(choice)) {
        return static_cast<Theme>(choice);
    }
    return themeCode(car) ? static_cast<Theme>(car) : ThemeBmw;
}

int themeSettingLoad(nbt_fs *fs, int &choice) {
    choice = kThemeAuto;
    if (!fs) {
        errno = EINVAL;
        return -1;
    }
    core::UniqueFd fd(nbt_fs_open(fs, NBT_FS_MANAGER_THEME, "manager", NULL, NBT_FS_READ));
    if (!fd.valid()) {
        return errno == ENOENT ? 0 : -1;
    }
    char text[kThemeFileMax + 1];
    const size_t used = readAll(fd.get(), text, sizeof(text));
    // One line, newline-terminated; anything else leaves Automatic.
    if (used < 2 || used > kThemeFileMax || text[used - 1] != '\n' ||
        memchr(text, '\n', used - 1) || memchr(text, 0, used)) {
        return 0;
    }
    text[used - 1] = 0;
    int parsed = kThemeAuto;
    if (themeSettingParse(text, parsed)) {
        choice = parsed;
    }
    return 0;
}

int themeCarRead(const char *path) {
    char text[kCarMakeFileMax];
    Theme theme = ThemeBmw;
    if (!path) {
        return -1;
    }
    core::UniqueFd fd(open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK));
    if (!fd.valid()) {
        return -1;
    }
    const size_t used = readAll(fd.get(), text, sizeof(text));
    return themeFromCarConfig(text, used, theme) ? static_cast<int>(theme) : -1;
}

} // namespace model
} // namespace nbt
