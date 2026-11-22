// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/phone_crashes.hpp"

#include "nbt/log.h"
#include "nbt/log/crash_report.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace worker {
namespace {

/* An installed application, or the manager. */
bool knownApp(storage::ManagedRoot &root, const char *app) {
    return nbt_token_valid(app, 48, 0) &&
           (!strcmp(app, "manager") ||
            nbt_fs_exists(root.legacy(), NBT_FS_PROFILE, app, NULL) == 1);
}

/* "" (all: app empty), or "<app>\n". */
bool appOrAll(storage::ManagedRoot &root, const char *body, char (&app)[NBT_APP_NAME_MAX + 2]) {
    const size_t length = body ? strlen(body) : 0;
    app[0] = 0;
    if (!length) {
        return true;
    }
    if (length >= sizeof(app) || body[length - 1] != '\n') {
        return false;
    }
    memcpy(app, body, length - 1);
    app[length - 1] = 0;
    return knownApp(root, app);
}

/* Newer: the later time, then the higher report number. */
bool newer(const log::CrashEntry &a, const log::CrashEntry &b) {
    const int time = strcmp(a.time, b.time);
    return time ? time > 0 : a.number > b.number;
}

/* Keeps the newest `max` of the entries offered, newest first. */
struct Newest {
    Newest() : count(0) {
    }
    void offer(const log::CrashEntry &entry) {
        unsigned at = count;
        while (at > 0 && newer(entry, kept[at - 1])) {
            --at;
        }
        if (at >= kCrashListMax) {
            return;
        }
        const unsigned last = count < kCrashListMax ? count : kCrashListMax - 1;
        for (unsigned i = last; i > at; --i) {
            kept[i] = kept[i - 1];
        }
        kept[at] = entry;
        count = count < kCrashListMax ? count + 1 : kCrashListMax;
    }
    log::CrashEntry kept[kCrashListMax];
    unsigned count;
};

/* No tab or newline in a listed value. */
void clean(char *text) {
    for (; *text; ++text) {
        if (*text == '\t' || *text == '\n') {
            *text = ' ';
        }
    }
}

} // namespace

int phoneCrashList(storage::ManagedRoot &root, const char *body, core::Span<char> out) {
    char app[NBT_APP_NAME_MAX + 2];
    if (!appOrAll(root, body, app)) {
        return 400;
    }
    char names[NBT_APP_LIST_MAX + 1][NBT_APP_NAME_MAX + 1];
    int apps = 1;
    if (app[0]) {
        memcpy(names[0], app, strlen(app) + 1); // a valid identifier: at most 48 characters
    } else {
        apps = nbt_fs_list_apps(root.legacy(), names);
        if (apps < 0) {
            return 503;
        }
        snprintf(names[apps++], sizeof(names[0]), "manager");
    }
    Newest newest;
    for (int i = 0; i < apps; ++i) {
        log::CrashEntry entries[log::kCrashSlots];
        const int count = log::listCrashReports(root.legacy(), names[i], entries, log::kCrashSlots);
        if (count < 0) {
            nbt_logf("phone: %s crash reports unreadable (errno %d)", names[i], errno);
            return 503;
        }
        for (int j = 0; j < count; ++j) {
            newest.offer(entries[j]);
        }
    }
    int n = snprintf(out.data(), out.size(), "CRASHES\t%u\n", newest.count);
    if (n < 0 || static_cast<size_t>(n) >= out.size()) {
        return 500;
    }
    size_t used = static_cast<size_t>(n);
    for (unsigned i = 0; i < newest.count; ++i) {
        log::CrashEntry &entry = newest.kept[i];
        clean(entry.time);
        clean(entry.version);
        clean(entry.ending);
        n = snprintf(out.data() + used, out.size() - used, "%s\t%u\t%lu\t%s\t%s\t%s\n", entry.app,
                     entry.slot, entry.number, entry.time, entry.version, entry.ending);
        if (n < 0 || static_cast<size_t>(n) >= out.size() - used) {
            return 500;
        }
        used += static_cast<size_t>(n);
    }
    return 200;
}

int phoneCrashRead(storage::ManagedRoot &root, const char *body, LogReply &reply) {
    char text[NBT_APP_NAME_MAX + 4];
    const size_t length = body ? strlen(body) : 0;
    if (length < 4 || length >= sizeof(text) || body[length - 1] != '\n' ||
        body[length - 3] != '\t') {
        return 400;
    }
    memcpy(text, body, length - 3);
    text[length - 3] = 0;
    const char slot[2] = {body[length - 2], 0};
    if (slot[0] < '0' || slot[0] >= static_cast<char>('0' + log::kCrashSlots) ||
        !knownApp(root, text)) {
        return 400;
    }
    reply.reset();
    const int fd = nbt_fs_open(root.legacy(), NBT_FS_CRASH_REPORT, text, slot, NBT_FS_READ);
    if (fd < 0) {
        return errno == ENOENT ? 409 : 503;
    }
    reply.files[1].reset(fd);
    struct stat st;
    if (fstat(fd, &st) || st.st_size < 0) {
        reply.reset();
        return 503;
    }
    // A report is about 40 KiB; the bound only keeps a stray file from being streamed.
    static const uint64_t kReportMax = UINT64_C(1) << 20;
    const uint64_t size = static_cast<uint64_t>(st.st_size);
    reply.sizes[1] = size < kReportMax ? size : kReportMax;
    return 200;
}

int phoneCrashClear(storage::ManagedRoot &root, const char *body, core::Span<char> out) {
    char app[NBT_APP_NAME_MAX + 2];
    if (!appOrAll(root, body, app)) {
        return 400;
    }
    int failed = 0;
    if (app[0]) {
        failed = log::clearCrashReports(root.legacy(), app);
    } else {
        char names[NBT_APP_LIST_MAX][NBT_APP_NAME_MAX + 1];
        const int apps = nbt_fs_list_apps(root.legacy(), names);
        if (apps < 0) {
            return 503;
        }
        for (int i = 0; i < apps; ++i) {
            failed |= log::clearCrashReports(root.legacy(), names[i]);
        }
        failed |= log::clearCrashReports(root.legacy(), "manager");
    }
    if (failed) {
        return 503;
    }
    snprintf(out.data(), out.size(), "OK\n");
    return 200;
}

} // namespace worker
} // namespace nbt
