/* iDrive's environment file and the shim trial record (hmi_env.hpp). */
#include "nbt/storage/hmi_env.hpp"

#include "capture_io.hpp"
#include "nbt/core/unique_fd.hpp"
#include "nbt/storage/path_open.hpp"
#include "nbt/storage/starter_config.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <memory>
#include <new>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace storage {

using namespace capture_io;

namespace {

/* On QNX an fsync commits the filesystem, so the manager's sync file serves;
 * elsewhere the state folder is synced too. */
int sync(nbt_fs *fs) {
    core::UniqueFd fd(nbt_fs_open(fs, NBT_FS_SYNC, "manager", NULL, NBT_FS_OPEN_LOCK));
    if (!fd.valid() || SafePathOpen::syncWritableFd(fd.get())) {
        return -1;
    }
#ifndef __QNX__
    const nbt_fs_path_kind dirs[] = {NBT_FS_STATE_DIR};
    return SafePathOpen::syncDirKinds(fs, "manager", NULL, dirs, 1);
#else
    return 0;
#endif
}

} // namespace

int hmiEnvEnable(nbt_fs *fs) {
    char text[256];
    const int n =
        snprintf(text, sizeof(text), "LD_PRELOAD=%s\n", StarterConfig::installedPreload());
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(text)) { // the process manager reads 512
        return -1;
    }
    return writeFsAtomically(fs, NBT_FS_HMI_ENV_NEXT, NBT_FS_HMI_ENV, text,
                             static_cast<size_t>(n)) ||
                   sync(fs)
               ? -1
               : 0;
}

int hmiEnvDisable(nbt_fs *fs) {
    (void)nbt_fs_unlink(fs, NBT_FS_HMI_ENV_NEXT, "manager", NULL);
    if (nbt_fs_unlink(fs, NBT_FS_HMI_ENV, "manager", NULL) && errno != ENOENT) {
        return -1;
    }
    return nbt_fs_exists(fs, NBT_FS_HMI_ENV, "manager", NULL) == 0 && !sync(fs) ? 0 : -1;
}

int hmiEnvPresent(nbt_fs *fs) {
    return nbt_fs_exists(fs, NBT_FS_HMI_ENV, "manager", NULL);
}

StarterConfigState starterConfigStateAt(const char *starterCfg) {
    std::unique_ptr<char[]> buf(new (std::nothrow) char[kStarterConfigMax]);
    size_t len = 0;
    if (!buf || !starterCfg ||
        readRegularChecked(starterCfg, geteuid(), 1, 2, buf.get(), kStarterConfigMax, &len, NULL)) {
        return StarterInvalid;
    }
    return StarterConfig::inspect(buf.get(), len, NULL);
}

int installedShimSha(nbt_fs *fs, unsigned char sha[32]) {
    return shimFileSha(fs, sha, NULL);
}

namespace {

const char *const kStateNames[] = {"none", "armed", "kept", "failed", "off"};

} // namespace

int shimRecordRead(nbt_fs *fs, ShimRecord &record) {
    memset(&record, 0, sizeof(record));
    const int present = nbt_fs_exists(fs, NBT_FS_SHIM_STATE, "manager", NULL);
    if (present <= 0) {
        return present; // none, or -1
    }
    core::UniqueFd fd(nbt_fs_open(fs, NBT_FS_SHIM_STATE, "manager", NULL, NBT_FS_READ));
    char text[160], state[16], hex[65];
    unsigned strikes = 0;
    const ssize_t n = fd.valid() ? read(fd.get(), text, sizeof(text) - 1) : -1;
    if (n <= 0) {
        return -1;
    }
    text[n] = 0;
    int used = 0;
    if (sscanf(text, "v2\nstate=%15[a-z]\nstrikes=%u\nshim=%64[0-9a-f]\n%n", state, &strikes, hex,
               &used) != 3 ||
        used != n || strikes > 9 || nbt_hex_decode(hex, record.sha, 32)) {
        return -1;
    }
    for (unsigned i = ShimArmed; i <= ShimOff; ++i) {
        if (!strcmp(state, kStateNames[i])) {
            record.state = static_cast<ShimState>(i);
            record.strikes = strikes;
            return 0;
        }
    }
    return -1;
}

int shimRecordWrite(nbt_fs *fs, const ShimRecord &record) {
    char hex[65], text[160];
    if (record.state < ShimArmed || record.state > ShimOff || record.strikes > 9 ||
        nbt_hex(record.sha, 32, hex, sizeof(hex))) {
        return -1;
    }
    const int n = snprintf(text, sizeof(text), "v2\nstate=%s\nstrikes=%u\nshim=%s\n",
                           kStateNames[record.state], record.strikes, hex);
    return n > 0 && static_cast<size_t>(n) < sizeof(text) &&
                   !writeFsAtomically(fs, NBT_FS_SHIM_STATE_NEXT, NBT_FS_SHIM_STATE, text,
                                      static_cast<size_t>(n))
               ? 0
               : -1;
}

int shimRecordClear(nbt_fs *fs) {
    (void)nbt_fs_unlink(fs, NBT_FS_SHIM_STATE_NEXT, "manager", NULL);
    if (nbt_fs_unlink(fs, NBT_FS_SHIM_STATE, "manager", NULL) && errno != ENOENT) {
        return -1;
    }
    return 0;
}

int shimArm(nbt_fs *fs, const unsigned char sha[32]) {
    ShimRecord record;
    memset(&record, 0, sizeof(record));
    record.state = ShimArmed;
    memcpy(record.sha, sha, 32);
    return shimRecordWrite(fs, record) || hmiEnvEnable(fs) ? -1 : 0;
}

} // namespace storage
} // namespace nbt
