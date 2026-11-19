// SPDX-License-Identifier: MPL-2.0
#include "nbt/app_faults.h"
#include "nbt/lifecycle/digit_record.hpp"

#include "nbt/core/unique_fd.hpp"
#include "nbt/transaction.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace lifecycle {

bool readDigitRecord(nbt_fs *fs, const char *app, nbt_fs_path_kind kind, const char *prefix,
                     unsigned max, unsigned *out) {
    char text[64];
    size_t used = 0;
    const size_t length = strlen(prefix);
    *out = 0;
    core::UniqueFd fd(nbt_fs_open(fs, kind, app, NULL, NBT_FS_READ));
    if (!fd.valid()) {
        return errno == ENOENT;
    }
    while (used < sizeof(text)) {
        const ssize_t n = ::read(fd.get(), text + used, sizeof(text) - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            return false;
        }
        if (n == 0) {
            break;
        }
        used += static_cast<size_t>(n);
    }
    if (!fd.close().ok()) {
        return false;
    }
    if (used == length + 2 && !memcmp(text, prefix, length) && text[length] >= '0' &&
        text[length] <= static_cast<char>('0' + max) && text[length + 1] == '\n') {
        *out = static_cast<unsigned>(text[length] - '0');
        return true;
    }
    errno = EINVAL;
    return false;
}

int writeDigitRecord(nbt_fs *fs, nbt_fs_path_kind next, nbt_fs_path_kind target, const char *app,
                     const char *prefix, unsigned value) {
    char text[64];
    if (value > 9) {
        errno = EINVAL;
        return -1;
    }
    const int n = snprintf(text, sizeof(text), "%s%u\n", prefix, value);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(text)) {
        errno = EINVAL;
        return -1;
    }
    return nbt_fs_replace_text(fs, next, target, app, text, static_cast<size_t>(n));
}

} // namespace lifecycle
} // namespace nbt

namespace {

const char kFaultPrefix[] = "NBTFAULT1\nfaults=";

} // namespace

int nbt_app_fault_load(nbt_fs *fs, const char *app, unsigned *faults) {
    if (!faults) {
        errno = EINVAL;
        return -1;
    }
    return nbt::lifecycle::readDigitRecord(fs, app, NBT_FS_FAULT_GUARD, kFaultPrefix,
                                           NBT_APP_FAULT_LIMIT, faults)
               ? 0
               : -1;
}

int nbt_app_fault_quarantined(nbt_fs *fs, const char *app) {
    unsigned faults = 0;
    if (nbt_app_fault_load(fs, app, &faults)) {
        return 1; // malformed or unsafe record fails closed
    }
    return faults >= NBT_APP_FAULT_LIMIT ? 1 : 0;
}

int nbt_app_fault_record(nbt_fs *fs, const char *app) {
    unsigned faults = 0;
    nbt::core::UniqueFd lock(nbt_tx_lock(fs, app));
    if (!lock.valid()) {
        return -1;
    }
    const bool valid = !nbt_app_fault_load(fs, app, &faults);
    const unsigned next = valid ? (faults < NBT_APP_FAULT_LIMIT ? faults + 1u : NBT_APP_FAULT_LIMIT)
                                : NBT_APP_FAULT_LIMIT;
    int result = 0;
    if (!valid || next != faults) {
        result = nbt::lifecycle::writeDigitRecord(fs, NBT_FS_FAULT_GUARD_NEXT, NBT_FS_FAULT_GUARD,
                                                  app, kFaultPrefix, next);
    }
    if (!lock.close().ok()) {
        result = -1;
    }
    return result;
}

int nbt_app_fault_clear(nbt_fs *fs, const char *app) {
    unsigned faults = 0;
    nbt::core::UniqueFd lock(nbt_tx_lock(fs, app));
    if (!lock.valid()) {
        return -1;
    }
    int result = nbt_app_fault_load(fs, app, &faults);
    if (result || faults) {
        result = nbt::lifecycle::writeDigitRecord(fs, NBT_FS_FAULT_GUARD_NEXT, NBT_FS_FAULT_GUARD,
                                                  app, kFaultPrefix, 0);
    }
    if (!lock.close().ok()) {
        result = -1;
    }
    return result;
}
