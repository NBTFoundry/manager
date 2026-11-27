// SPDX-License-Identifier: MPL-2.0
/* The self test's report (docs/self-test.md): the worker's own checks, what
 * each bundled application wrote in data/<app>/selftest.txt (untrusted: read
 * bounded, line by line, anything malformed dropped) and the driver's
 * answers; kept in state/manager/selftest.txt and summed up for the GUI. */
#include "nbt/worker/self_test.hpp"

#include "nbt/bundled.h"
#include "nbt/core/unique_fd.hpp"
#include "nbt/log.h"
#include "nbt/process/audio_mixer.hpp"
#include "nbt/process/runner_path.hpp"
#include "nbt/storage/files_root.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

namespace nbt {
namespace worker {
namespace {

const size_t kEntries = 64, kDetailMax = 40, kAppReportMax = 4096, kAppLines = 24;
const size_t kReplyFailures = 16;
const uint64_t kMiB = 1024 * 1024;

struct Entry {
    char key[24]; // <group>.<check>
    char result;  // p, f or s
    char detail[kDetailMax + 1];
};

struct Report {
    Entry entries[kEntries];
    size_t count;
};

void add(Report &r, const char *group, const char *check, char result, const char *detail) {
    if (r.count == kEntries) {
        return;
    }
    Entry &e = r.entries[r.count++];
    snprintf(e.key, sizeof(e.key), "%s.%s", group, check);
    e.result = result;
    snprintf(e.detail, sizeof(e.detail), "%s", detail);
    for (char *c = e.detail; *c; ++c) {
        *c = (*c < 0x20 || *c > 0x7e || *c == '|') ? ' ' : *c;
    }
}

void managerChecks(nbt_fs *fs, const SelfTestPrepared &prepared, Report &r) {
    char text[64];
    add(r, "manager", "release", prepared.version[0] ? 'p' : 'f',
        prepared.version[0] ? prepared.version : prepared.reason);
    const char *runner = process::runnerPath();
    const bool runs = runner && !access(runner, X_OK);
    add(r, "manager", "runner", runs ? 'p' : 'f', runs ? "present" : "missing");
    storage::FilesRoot files;
    const core::Result<uint64_t> disk =
        files.open(storage::FilesRoot::location()).ok()
            ? files.freeBytes()
            : core::Result<uint64_t>::failure(core::Status::error(EIO, "no root"));
    if (disk.get()) {
        snprintf(text, sizeof(text), "%llu MB free",
                 static_cast<unsigned long long>(disk.value() / kMiB));
    }
    add(r, "manager", "files", disk.get() ? 'p' : 'f', disk.get() ? text : "no hard disk");
    struct statvfs vfs;
    if (statvfs(fs->root, &vfs)) {
        add(r, "manager", "space", 'f', "cannot read");
    } else {
        const uint64_t free = static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize / kMiB;
        snprintf(text, sizeof(text), "%llu MB free", static_cast<unsigned long long>(free));
        add(r, "manager", "space", free >= 32 ? 'p' : 'f', text);
    }
    // How the sound went while the applications played: the mixer's counts
    // since `prepared` (docs/audio-mixer.md). A gap, or a queue made longer,
    // is a queue that was too short.
    process::MixerState audio;
    if (!process::mixerState(&audio)) {
        add(r, "manager", "audio", 's', "no mixer");
    } else {
        const unsigned gaps =
            audio.gaps >= prepared.audioGaps ? audio.gaps - prepared.audioGaps : audio.gaps;
        const unsigned grown =
            audio.grown >= prepared.audioGrown ? audio.grown - prepared.audioGrown : audio.grown;
        snprintf(text, sizeof(text), "level %d, %u gaps, %u queues grown", audio.level,
                 gaps > 999 ? 999 : gaps, grown > 999 ? 999 : grown);
        add(r, "manager", "audio", gaps || grown ? 'f' : 'p', text);
    }
}

bool checkName(const char *s) {
    const size_t n = strlen(s);
    return n >= 1 && n <= 12 && strspn(s, "abcdefghijklmnopqrstuvwxyz") == n;
}

/* One application's report; false when it has none (or not this form's). */
bool appReport(nbt_fs *fs, const nbt_bundled_app &bundled, Report &r) {
    char text[kAppReportMax + 1];
    core::UniqueFd fd(nbt_fs_open(fs, NBT_FS_DATA_SELFTEST, bundled.app, NULL, NBT_FS_READ));
    const ssize_t n = fd.valid() ? read(fd.get(), text, kAppReportMax) : -1;
    if (n <= 0) {
        return false;
    }
    text[n] = 0;
    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    char head[32];
    snprintf(head, sizeof(head), "SELFTEST1\t%s", bundled.form);
    if (!line || strcmp(line, head)) {
        return false;
    }
    add(r, bundled.form, "start", 'p', "ran");
    for (size_t lines = 0; lines < kAppLines && (line = strtok_r(NULL, "\n", &save)); ++lines) {
        char *check = line, *result = strchr(line, '\t');
        char *detail = result ? strchr(result + 1, '\t') : NULL;
        if (!detail) {
            continue;
        }
        *result++ = 0;
        *detail++ = 0;
        const char code = !strcmp(result, "pass")   ? 'p'
                          : !strcmp(result, "fail") ? 'f'
                          : !strcmp(result, "skip") ? 's'
                                                    : 0;
        if (code && checkName(check) && strcmp(check, "start")) {
            add(r, bundled.form, check, code, detail);
        }
    }
    return true;
}

void answer(Report &r, const char *check, char said) {
    add(r, "you", check,
        said == 'y'   ? 'p'
        : said == 'n' ? 'f'
                      : 's',
        said == 'y'   ? "yes"
        : said == 'n' ? "no"
                      : "not asked");
}

int keep(nbt_fs *fs, const SelfTestPrepared &prepared, const Report &r) {
    char text[4096];
    int used =
        snprintf(text, sizeof(text), "SELFTEST1\t%lld\t%s\n", static_cast<long long>(time(NULL)),
                 prepared.version[0] ? prepared.version : "-");
    if (used < 0 || static_cast<size_t>(used) >= sizeof(text)) {
        errno = ENOSPC;
        return -1;
    }
    // Under 4 KiB, so it fits one phone reply (GET /v1/selftest): lines that
    // would not fit are left out.
    for (size_t i = 0; i < r.count; ++i) {
        const Entry &e = r.entries[i];
        char line[sizeof(e.key) + sizeof(e.detail) + 8];
        const int n = snprintf(line, sizeof(line), "%s\t%c\t%s\n", e.key, e.result, e.detail);
        if (n < 0 || static_cast<size_t>(used + n) >= sizeof(text)) {
            break;
        }
        memcpy(text + used, line, static_cast<size_t>(n));
        used += n;
    }
    return nbt_fs_replace_text(fs, NBT_FS_MANAGER_SELFTEST_NEXT, NBT_FS_MANAGER_SELFTEST, "manager",
                               text, static_cast<size_t>(used));
}

int reply(const Report &r, char *out, size_t size) {
    unsigned counts[3] = {0, 0, 0};
    for (size_t i = 0; i < r.count; ++i) {
        ++counts[r.entries[i].result == 'p' ? 0 : r.entries[i].result == 'f' ? 1 : 2];
    }
    int used = snprintf(out, size, "SELFTEST\t%u\t%u\t%u", counts[0], counts[1], counts[2]);
    size_t listed = 0;
    for (char wanted : {'f', 's'}) { // failures first
        for (size_t i = 0; i < r.count && listed < kReplyFailures; ++i) {
            const Entry &e = r.entries[i];
            if (e.result != wanted || used < 0 || static_cast<size_t>(used) >= size) {
                continue;
            }
            used += snprintf(out + used, size - static_cast<size_t>(used), "\t%s|%c|%s", e.key,
                             e.result, e.detail);
            ++listed;
        }
    }
    if (used < 0 || static_cast<size_t>(used) + 2 > size) {
        return -1;
    }
    out[used] = '\n';
    out[used + 1] = 0;
    return 0;
}

} // namespace

int selfTestReport(nbt_fs *fs, const SelfTestPrepared &prepared, char saw, char heard, char *out,
                   size_t size) {
    Report r;
    r.count = 0;
    managerChecks(fs, prepared, r);
    for (unsigned i = 0; i < NBT_BUNDLED_COUNT; ++i) {
        const nbt_bundled_app &bundled = nbt_bundled_apps[i];
        if (prepared.absent & (1u << i)) {
            add(r, bundled.form, "start", 's', "not in this release");
        } else if (prepared.failed & (1u << i)) {
            add(r, bundled.form, "start", 'f', "install failed");
        } else if (!(prepared.ready & (1u << i))) {
            add(r, bundled.form, "start", 's', "not run");
        } else if (!appReport(fs, bundled, r)) {
            add(r, bundled.form, "start", 'f', "no report");
        }
    }
    answer(r, "screen", saw);
    answer(r, "sound", heard);
    const int kept = keep(fs, prepared, r);
    if (kept) {
        nbt_logf("self test: report not kept (errno %d)", errno);
    }
    for (size_t i = 0; i < r.count; ++i) {
        const Entry &e = r.entries[i];
        nbt_logf("self test: %s %s %s", e.key,
                 e.result == 'p'   ? "pass"
                 : e.result == 'f' ? "FAIL"
                                   : "skip",
                 e.detail);
    }
    return reply(r, out, size) ? -1 : kept;
}

int selfTestPhone(nbt_fs *fs, char *out, size_t size) {
    core::UniqueFd fd(nbt_fs_open(fs, NBT_FS_MANAGER_SELFTEST, "manager", NULL, NBT_FS_READ));
    if (!fd.valid()) {
        return errno == ENOENT ? (snprintf(out, size, "SELFTEST1\tnone\n") > 0 ? 200 : 500) : 503;
    }
    const ssize_t n = size ? read(fd.get(), out, size - 1) : -1;
    if (n <= 0 || static_cast<size_t>(n) == size - 1) {
        return 503; // unreadable, or larger than any report kept
    }
    out[n] = 0;
    return strncmp(out, "SELFTEST1\t", 10) ? 503 : 200;
}

int selfTestLast(nbt_fs *fs, char *out, size_t size) {
    char text[4097];
    core::UniqueFd fd(nbt_fs_open(fs, NBT_FS_MANAGER_SELFTEST, "manager", NULL, NBT_FS_READ));
    const ssize_t n = fd.valid() ? read(fd.get(), text, sizeof(text) - 1) : -1;
    if (n <= 0) {
        return -1;
    }
    text[n] = 0;
    char *save = NULL;
    char *line = strtok_r(text, "\n", &save);
    if (!line || strncmp(line, "SELFTEST1\t", 10)) {
        return -1;
    }
    Report r;
    r.count = 0;
    while ((line = strtok_r(NULL, "\n", &save)) != NULL) {
        char *result = strchr(line, '\t');
        char *detail = result ? strchr(result + 1, '\t') : NULL;
        char *dot = strchr(line, '.');
        if (!detail || !dot || dot > result || result[2] != '\t' ||
            (result[1] != 'p' && result[1] != 'f' && result[1] != 's')) {
            continue;
        }
        *dot = *result = 0;
        add(r, line, dot + 1, result[1], detail + 1);
    }
    return reply(r, out, size);
}

} // namespace worker
} // namespace nbt
