// SPDX-License-Identifier: MPL-2.0
#include "nbt/launcher/manager_crash.hpp"

#include "nbt/log/crash_report.hpp"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace launcher {
namespace {

/* Up to `size` bytes of `kind`: its first, or with `newest` its last. */
size_t readManagerFile(const storage::ManagedRoot &root, nbt_fs_path_kind kind, char *out,
                       size_t size, bool newest) {
    core::Result<core::UniqueFd> opened = root.open(kind, "manager", NULL, NBT_FS_READ);
    if (!opened.ok()) {
        return 0;
    }
    core::UniqueFd fd(opened.value().release());
    struct stat st;
    if (newest && !fstat(fd.get(), &st) && st.st_size > static_cast<off_t>(size) &&
        lseek(fd.get(), st.st_size - static_cast<off_t>(size), SEEK_SET) < 0) {
        return 0;
    }
    size_t used = 0;
    while (used < size) {
        const ssize_t count = read(fd.get(), out + used, size - used);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        used += static_cast<size_t>(count);
    }
    return used;
}

void logCrashMarker(const char *record, size_t used) {
    if (!used) {
        return;
    }
    fprintf(stderr, "nbt-manager-launcher: %.*s", static_cast<int>(used), record);
    if (record[used - 1] != '\n') {
        fputc('\n', stderr);
    }
}

} // namespace

void reportManagerCrash(storage::ManagedRoot &root, const nbt_manager_state &state,
                        const char *reason) {
    char record[512];
    const size_t recordSize =
        readManagerFile(root, NBT_FS_MANAGER_CRASH, record, sizeof(record), false);
    logCrashMarker(record, recordSize);
    static const size_t kDebugTail = 16 * 1024;
    char *debug = static_cast<char *>(malloc(kDebugTail));
    const size_t debugSize =
        debug ? readManagerFile(root, NBT_FS_MANAGER_LOG, debug, kDebugTail, true) : 0;
    char version[49];
    log::sequenceVersion(state.active.sequence, version, sizeof(version));
    const log::CrashFacts facts = {"manager",  version, state.active.sequence, NULL, 0,
                                   UINT64_MAX, reason};
    log::CrashSection sections[2];
    size_t count = 0;
    if (recordSize) {
        const log::CrashSection marker = {
            "The GUI's crash record", {record, NULL}, {recordSize, 0}};
        sections[count++] = marker;
    }
    if (debugSize) {
        const log::CrashSection tail = {
            "NBT Manager's debug log, its newest 16 KiB", {debug, NULL}, {debugSize, 0}};
        sections[count++] = tail;
    }
    (void)log::writeCrashReport(root.legacy(), facts, sections, count, NULL);
    free(debug);
}

} // namespace launcher
} // namespace nbt
