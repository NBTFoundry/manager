// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/shortcut_record.hpp"
#include "nbt/gui/phone_wake.hpp"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
namespace nbt {
namespace gui {
namespace {
const size_t kLineMax = 80;
bool appValid(const char *app, size_t n) {
    if (!n || n > NBT_HOLD_APP_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        const char c = app[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}
bool matches(const char *text, size_t at, size_t size, const char *word) {
    const size_t n = strlen(word);
    return size - at >= n && !memcmp(text + at, word, n);
}
} // namespace
const char *shortcutRecordPath() {
#ifdef NBT_TESTING
    const char *path = getenv("NBT_TEST_SHORTCUT_RECORD");
    if (path) {
        return path;
    }
#endif
    return kShortcutRecordPath;
}
bool shortcutRecordParse(const char *text, size_t size, ShortcutRecord &record) {
    if (!text || size < 2 || size > kLineMax || text[size - 1] != '\n' || memchr(text, 0, size)) {
        return false;
    }
    size_t at = 0, digits = 0;
    uint64_t ms = 0;
    while (at < size && text[at] >= '0' && text[at] <= '9') {
        const unsigned d = static_cast<unsigned>(text[at] - '0');
        if (digits == 20 || ms > (UINT64_MAX - d) / 10u) {
            return false;
        }
        ms = ms * 10u + d;
        ++at;
        ++digits;
    }
    if (!digits || (digits > 1 && text[0] == '0') || text[at] != ' ') {
        return false;
    }
    ++at;
    ShortcutRecord parsed;
    memset(&parsed, 0, sizeof(parsed));
    parsed.ms = ms;
    if (matches(text, at, size, "open\n") && at + 5 == size) {
        parsed.word = ShortcutOpen;
    } else if (matches(text, at, size, "taken\n") && at + 6 == size) {
        parsed.word = ShortcutTaken;
    } else if (matches(text, at, size, "start ")) {
        at += 6;
        const size_t n = size - 1 - at;
        if (!appValid(text + at, n)) {
            return false;
        }
        parsed.word = ShortcutStart;
        memcpy(parsed.app, text + at, n);
        parsed.app[n] = 0;
    } else {
        return false;
    }
    record = parsed;
    return true;
}
int shortcutRecordFormat(const ShortcutRecord &record, char *out, size_t size) {
    int n = -1;
    size_t length = 0; // QNX 6.5's libc has no strnlen
    while (length <= NBT_HOLD_APP_MAX && record.app[length]) {
        ++length;
    }
    if (record.word == ShortcutStart && appValid(record.app, length)) {
        n = snprintf(out, size, "%llu start %s\n", static_cast<unsigned long long>(record.ms),
                     record.app);
    } else if (record.word == ShortcutOpen || record.word == ShortcutTaken) {
        n = snprintf(out, size, "%llu %s\n", static_cast<unsigned long long>(record.ms),
                     record.word == ShortcutOpen ? "open" : "taken");
    }
    return n < 0 || static_cast<size_t>(n) >= size ? -1 : n;
}
bool shortcutRecordWrite(const char *path, const ShortcutRecord &record) {
    char line[kLineMax + 1];
    const int n = shortcutRecordFormat(record, line, sizeof(line));
    if (n < 0) {
        return false;
    }
    const int fd = privateRecordOpen(path, true);
    if (fd < 0) {
        return false;
    }
    ssize_t written;
    do {
        written = pwrite(fd, line, static_cast<size_t>(n), 0);
    } while (written < 0 && errno == EINTR);
    const bool ok = written == n && !ftruncate(fd, n);
    return close(fd) == 0 && ok;
}
bool shortcutRecordRead(const char *path, ShortcutRecord &record) {
    const int fd = privateRecordOpen(path, false);
    if (fd < 0) {
        return false;
    }
    char line[kLineMax + 1];
    ssize_t n;
    do {
        n = pread(fd, line, sizeof(line), 0);
    } while (n < 0 && errno == EINTR);
    close(fd);
    return n > 0 && shortcutRecordParse(line, static_cast<size_t>(n), record);
}
bool shortcutRecordDue(const ShortcutRecord &record, uint64_t since, uint64_t now) {
    return record.word != ShortcutTaken && record.ms >= since && now >= record.ms &&
           now - record.ms <= kShortcutFreshMs;
}
} // namespace gui
} // namespace nbt
