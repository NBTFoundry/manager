// SPDX-License-Identifier: MPL-2.0
#include "nbt/log/crash_report.hpp"

#include "nbt/log.h"
#include "nbt/manager_store.h"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace nbt {
namespace log {
namespace {

bool writeAll(int fd, const char *data, size_t size) {
    while (size) {
        const ssize_t n = ::write(fd, data, size);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        data += n;
        size -= static_cast<size_t>(n);
    }
    return true;
}

/* Formatted lines into a fixed buffer; what does not fit is cut. */
template <size_t N> struct Text {
    Text() : used(0) {
        data[0] = 0;
    }
    void add(const char *format, ...) __attribute__((format(printf, 2, 3))) {
        va_list args;
        va_start(args, format);
        const int n = vsnprintf(data + used, sizeof(data) - used, format, args);
        va_end(args);
        if (n > 0) {
            used += static_cast<size_t>(n) < sizeof(data) - used ? static_cast<size_t>(n)
                                                                 : sizeof(data) - used - 1;
        }
    }
    char data[N];
    size_t used;
};

void slotName(unsigned slot, char (&name)[2]) {
    name[0] = static_cast<char>('0' + slot);
    name[1] = 0;
}

/* The value of header line "<key>: " in `header`, printable ASCII, into `out`. */
bool headerValue(const char *header, const char *key, char *out, size_t size) {
    const size_t keyLength = strlen(key);
    for (const char *line = header; *line;) {
        const char *end = strchr(line, '\n');
        if (!end || end == line) {
            return false; // the header ends at its first empty line
        }
        if (!strncmp(line, key, keyLength) && line[keyLength] == ':' &&
            line[keyLength + 1] == ' ') {
            const char *value = line + keyLength + 2;
            size_t length = static_cast<size_t>(end - value);
            if (length >= size) {
                length = size - 1;
            }
            for (size_t i = 0; i < length; ++i) {
                const unsigned char c = static_cast<unsigned char>(value[i]);
                out[i] = c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '?';
            }
            out[length] = 0;
            return true;
        }
        line = end + 1;
    }
    return false;
}

/* Report `slot`'s header: 1 with `entry` filled, 0 when the slot holds no
 * report, -1 when it cannot be read. */
int readHeader(nbt_fs *fs, const char *app, unsigned slot, CrashEntry &entry) {
    char name[2];
    slotName(slot, name);
    const int fd = nbt_fs_open(fs, NBT_FS_CRASH_REPORT, app, name, NBT_FS_READ);
    if (fd < 0) {
        return errno == ENOENT ? 0 : -1;
    }
    char header[kCrashHeaderMax + 1];
    size_t used = 0;
    while (used < kCrashHeaderMax) {
        const ssize_t n = read(fd, header + used, kCrashHeaderMax - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            const int saved = errno;
            close(fd);
            errno = saved;
            return -1;
        }
        if (n == 0) {
            break;
        }
        used += static_cast<size_t>(n);
    }
    close(fd);
    header[used] = 0;
    memset(&entry, 0, sizeof(entry));
    const size_t title = sizeof(kCrashTitle) - 1;
    char number[24];
    if (strncmp(header, kCrashTitle, title) || header[title] != '\n' ||
        !headerValue(header + title + 1, "Report", number, sizeof(number))) {
        return 0;
    }
    char *end = NULL;
    errno = 0;
    entry.number = strtoul(number, &end, 10);
    if (errno || !end || *end || end == number) {
        return 0;
    }
    snprintf(entry.app, sizeof(entry.app), "%s", app);
    entry.slot = slot;
    const char *fields = header + title + 1;
    (void)headerValue(fields, "Time", entry.time, sizeof(entry.time));
    (void)headerValue(fields, "Version", entry.version, sizeof(entry.version));
    (void)headerValue(fields, "Ended", entry.ending, sizeof(entry.ending));
    return 1;
}

/* A new crash.next. dev74 in the car: the checked open of an application's
 * log failed in its host (errno 89) where plain opens worked
 * (src/log/app_log.cpp), so the same fallback, with the same checks. */
int createNext(nbt_fs *fs, const char *app) {
    if (nbt_fs_unlink(fs, NBT_FS_CRASH_REPORT_NEXT, app, NULL) && errno != ENOENT) {
        nbt_logf("crash report %s: stale crash.next not removed (errno %d)", app, errno);
    }
    int fd = nbt_fs_open(fs, NBT_FS_CRASH_REPORT_NEXT, app, NULL, NBT_FS_CREATE);
    if (fd >= 0) {
        return fd;
    }
    const int checked = errno;
    char path[NBT_PATH_MAX];
    if (nbt_fs_path(fs, NBT_FS_CRASH_REPORT_NEXT, app, NULL, path, sizeof(path))) {
        nbt_logf("crash report %s: no path (errno %d)", app, errno);
        return -1;
    }
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    struct stat st;
    if (fd >= 0 && (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != fs->owner ||
                    st.st_dev != fs->device || st.st_nlink != 1 || (st.st_mode & 0022))) {
        close(fd);
        fd = -1;
        errno = EPERM;
    }
    if (fd < 0) {
        nbt_logf("crash report %s: cannot create (errno %d, then %d)", app, checked, errno);
        return -1;
    }
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    nbt_logf("crash report %s: checked create failed (errno %d); created with a plain open", app,
             checked);
    return fd;
}

bool writeHeader(nbt_fs *fs, int fd, const CrashFacts &facts, unsigned long number) {
    Text<kCrashHeaderMax> text;
    const time_t now = time(NULL);
    struct tm local;
    char when[32] = "unknown";
    if (localtime_r(&now, &local)) {
        strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &local);
    }
    text.add("%s\n", kCrashTitle);
    text.add("Application: %s\n", facts.app);
    text.add("Version: %s\n", facts.version && facts.version[0] ? facts.version : "unknown");
    if (facts.sequence) {
        text.add("Sequence: %llu\n", static_cast<unsigned long long>(facts.sequence));
    }
    text.add("Report: %lu\n", number);
    text.add("Time: %s\n", when);
    if (facts.start) {
        text.add("Started: %s\n", facts.start);
    }
    if (facts.pid > 0) {
        text.add("Process: %ld\n", facts.pid);
    }
    if (facts.ranMs != UINT64_MAX) {
        text.add("Ran: %llu.%llu s\n", static_cast<unsigned long long>(facts.ranMs / 1000),
                 static_cast<unsigned long long>(facts.ranMs % 1000 / 100));
    }
    text.add("Ended: %s\n", facts.ending && facts.ending[0] ? facts.ending : "unknown");
    nbt_manager_state state;
    if (strcmp(facts.app, "manager") && !nbt_manager_store_inspect(fs, &state)) {
        char version[49];
        sequenceVersion(state.active.sequence, version, sizeof(version));
        text.add("NBT Manager: %s (sequence %llu)\n", version,
                 static_cast<unsigned long long>(state.active.sequence));
    }
    text.add("\n");
    return writeAll(fd, text.data, text.used);
}

bool writeSection(int fd, const CrashSection &section) {
    if (!writeAll(fd, section.title, strlen(section.title)) || !writeAll(fd, ":\n", 2)) {
        return false;
    }
    bool newline = true;
    for (unsigned i = 0; i < 2; ++i) {
        if (section.size[i]) {
            if (!writeAll(fd, section.data[i], section.size[i])) {
                return false;
            }
            newline = section.data[i][section.size[i] - 1] == '\n';
        }
    }
    if (!section.size[0] && !section.size[1]) {
        return writeAll(fd, "(nothing)\n\n", 11);
    }
    return writeAll(fd, newline ? "\n" : "\n\n", newline ? 1 : 2);
}

} // namespace

int listCrashReports(nbt_fs *fs, const char *app, CrashEntry *out, size_t max) {
    CrashEntry found[kCrashSlots];
    unsigned count = 0;
    for (unsigned slot = 0; slot < kCrashSlots; ++slot) {
        const int read = readHeader(fs, app, slot, found[count]);
        if (read < 0) {
            return -1;
        }
        count += read ? 1u : 0u;
    }
    // Newest first: insertion sort on the report number.
    for (unsigned i = 1; i < count; ++i) {
        CrashEntry entry = found[i];
        unsigned j = i;
        for (; j > 0 && found[j - 1].number < entry.number; --j) {
            found[j] = found[j - 1];
        }
        found[j] = entry;
    }
    const unsigned shown = count < max ? count : static_cast<unsigned>(max);
    for (unsigned i = 0; i < shown; ++i) {
        out[i] = found[i];
    }
    return static_cast<int>(shown);
}

int clearCrashReports(nbt_fs *fs, const char *app) {
    int result = 0;
    for (unsigned slot = 0; slot < kCrashSlots; ++slot) {
        char name[2];
        slotName(slot, name);
        if (nbt_fs_unlink(fs, NBT_FS_CRASH_REPORT, app, name) && errno != ENOENT) {
            result = -1;
        }
    }
    if (nbt_fs_unlink(fs, NBT_FS_CRASH_REPORT_NEXT, app, NULL) && errno != ENOENT) {
        result = -1;
    }
    return result;
}

int writeCrashReport(nbt_fs *fs, const CrashFacts &facts, const CrashSection *sections,
                     size_t count, int *keep) {
    if (keep) {
        *keep = -1;
    }
    if (!fs || !facts.app) {
        return -1;
    }
    // An empty slot, else the oldest report's; the new report's number is one more than any.
    unsigned slot = kCrashSlots, oldestSlot = kCrashSlots;
    unsigned long newest = 0, oldest = 0;
    for (unsigned i = 0; i < kCrashSlots; ++i) {
        CrashEntry entry;
        if (readHeader(fs, facts.app, i, entry) <= 0) {
            if (slot == kCrashSlots) {
                slot = i; // empty, or unreadable: replaced
            }
            continue;
        }
        newest = entry.number > newest ? entry.number : newest;
        if (oldestSlot == kCrashSlots || entry.number < oldest) {
            oldest = entry.number;
            oldestSlot = i;
        }
    }
    if (slot == kCrashSlots) {
        slot = oldestSlot;
    }
    const int fd = createNext(fs, facts.app);
    if (fd < 0) {
        return -1;
    }
    bool written = writeHeader(fs, fd, facts, newest + 1);
    for (size_t i = 0; written && i < count; ++i) {
        written = writeSection(fd, sections[i]);
    }
    char name[2];
    slotName(slot, name);
    if (!written || fsync(fd) ||
        nbt_fs_rename(fs, NBT_FS_CRASH_REPORT_NEXT, NBT_FS_CRASH_REPORT, facts.app, name)) {
        nbt_logf("crash report %s: not written (errno %d)", facts.app, errno);
        close(fd);
        (void)nbt_fs_unlink(fs, NBT_FS_CRASH_REPORT_NEXT, facts.app, NULL);
        return -1;
    }
    nbt_logf("crash report %s: %lu in slot %u (%s)", facts.app, newest + 1, slot,
             facts.ending ? facts.ending : "unknown");
    if (keep) {
        *keep = fd;
    } else {
        close(fd);
    }
    return static_cast<int>(slot);
}

} // namespace log
} // namespace nbt
