// SPDX-License-Identifier: MPL-2.0
/* Transactional capture shim + starter.cfg install and recovery. */
#include "nbt/storage/capture_install.hpp"

#include "capture_io.hpp"
#include "capture_probe.hpp"
#include "nbt/log.h"
#include "nbt/storage/hmi_env.hpp"
#include "nbt/storage/path_open.hpp"
#include "nbt/storage/starter_config.hpp"
#include "nbt/util.h"

#include <dirent.h>
#include <errno.h>
#include <memory>
#include <new>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace {

using namespace nbt::storage::capture_io;
using nbt::storage::kStarterConfigMax;
using nbt::storage::SafePathOpen;
using nbt::storage::StarterConfig;
using nbt::storage::StarterConfigState;

int readShimFromRelease(int releaseFd, const nbt_release_component &comp,
                        std::unique_ptr<char[]> &shimBuf) {
    if (comp.size == 0 || comp.size > 2u * 1024u * 1024u) {
        return -1;
    }
    shimBuf.reset(new (std::nothrow) char[comp.size]);
    if (!shimBuf || lseek(releaseFd, static_cast<off_t>(comp.offset), SEEK_SET) < 0) {
        return -1;
    }
    size_t got = 0;
    while (got < static_cast<size_t>(comp.size)) {
        const ssize_t n =
            read(releaseFd, shimBuf.get() + got, static_cast<size_t>(comp.size) - got);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return -1;
        }
        got += static_cast<size_t>(n);
    }
    unsigned char sha[32];
    sha256Bytes(shimBuf.get(), got, sha);
    return nbt_constant_equal(sha, comp.sha256, 32) ? 0 : -1;
}

/* Installed and verified: a new shim (or one with no state yet) is armed for
 * iDrive's next start; the manager keeps it after 60 s of running
 * (docs/hmi-restart.md). A shim already known keeps its state. */
void armShim(nbt_fs *fs, const unsigned char sha[32], bool shimChanged) {
    nbt::storage::ShimRecord record;
    if (!shimChanged && !nbt::storage::shimRecordRead(fs, record) &&
        record.state != nbt::storage::ShimNone && !memcmp(record.sha, sha, 32)) {
        return;
    }
    if (nbt::storage::shimArm(fs, sha)) {
        nbt_logf("capture: the shim could not be armed for iDrive's next start");
    } else {
        nbt_logf("capture: shim armed for iDrive's next start");
    }
}

/* On /fs/sda0, remounted writable only for this: starter.cfg names iDrive's
 * environment file (verified by reading it back), then the folder the shim
 * used before dev105 is removed. */
int updateSystemFiles(const char *cfgPath, const char *cfgDir, const char *cfgNext,
                      const char *capDir, bool cfgChange, bool legacy, const char *cfg,
                      size_t cfgLen, const CfgMeta &meta, const unsigned char target[32],
                      dev_t cfgDev, nbt_remount_fn remount, char *error, unsigned errorSize) {
    if (remount && remount(1)) {
        return failMsg(error, errorSize, "cannot remount /fs/sda0 writable");
    }
    int rc = -1;
    struct statvfs vol;
    std::unique_ptr<char[]> check(new (std::nothrow) char[kStarterConfigMax]);
    size_t checkLen = 0;
    unsigned char checkSha[32];
    if (!check || statvfs(cfgDir, &vol) || (vol.f_flag & ST_RDONLY) ||
        !SafePathOpen::volumeHasSpace(vol, cfgLen + 65536u)) {
        failMsg(error, errorSize, "insufficient space or read-only /fs/sda0");
    } else if (checkSafeDir(cfgDir, geteuid(), cfgDev, false)) {
        failMsg(error, errorSize, "capture destination directories unsafe");
    } else if (cfgChange && ((void)unlink(cfgNext),
                             writeExactFile(cfgNext, cfgPath, cfgDir, cfg, cfgLen, meta.mode, &meta,
                                            "short-cfg-write", "sync-cfg", "before-cfg-rename"))) {
        failMsg(error, errorSize, "cannot update starter.cfg");
    } else if (readRegularChecked(cfgPath, geteuid(), 1, 2, check.get(), kStarterConfigMax,
                                  &checkLen, NULL) ||
               (sha256Bytes(check.get(), checkLen, checkSha),
                !nbt_constant_equal(checkSha, target, 32))) {
        failMsg(error, errorSize, "starter.cfg read-back verification failed");
    } else if (legacy && removeLegacyShimDir(capDir)) {
        failMsg(error, errorSize, "cannot remove the shim's old folder");
    } else {
        rc = 0;
    }
    if (remount && remount(0) && rc == 0) {
        rc = failMsg(error, errorSize, "cannot remount /fs/sda0 read-only");
    }
    return rc;
}

} // namespace

int nbt_capture_refresh_from_release(nbt_fs *fs, const char *hmi_root, const char *starter_cfg_path,
                                     int release_fd, const nbt_release *release,
                                     nbt_remount_fn remount, int *changed, char *error,
                                     unsigned error_size) {
    if (error && error_size) {
        *error = 0;
    }
    if (changed) {
        *changed = 0;
    }
    if (!fs || !hmi_root || !starter_cfg_path || !release || release_fd < 0) {
        return failMsg(error, error_size, "invalid capture refresh arguments");
    }
    char capDir[NBT_PATH_MAX], shimPath[NBT_PATH_MAX], shimNext[NBT_PATH_MAX];
    char cfgDir[NBT_PATH_MAX], cfgNext[NBT_PATH_MAX];
    if (buildCapturePaths(hmi_root, starter_cfg_path, capDir, shimPath, shimNext, cfgDir,
                          cfgNext)) {
        return failMsg(error, error_size, "capture paths invalid");
    }
    const uid_t owner = geteuid();
    const int captureIndex = nbt_release_find(release, NBT_RELEASE_CAPTURE_NAME);
    const bool wantInstall = captureIndex >= 0;
    const int txPresent = nbt_fs_exists(fs, NBT_FS_CAPTURE_TX, "manager", NULL);
    const int origPresent = nbt_fs_exists(fs, NBT_FS_CAPTURE_STARTER_ORIG, "manager", NULL);
    if (txPresent < 0 || origPresent < 0) {
        return failMsg(error, error_size, "cannot inspect capture state");
    }

    std::unique_ptr<char[]> cfgBuf(new (std::nothrow) char[kStarterConfigMax]);
    std::unique_ptr<char[]> newCfgBuf(new (std::nothrow) char[kStarterConfigMax]);
    std::unique_ptr<char[]> origBuf;
    size_t cfgLen = 0, newCfgLen = 0, origLen = 0;
    CfgMeta cfgMeta;
    if (!cfgBuf || !newCfgBuf ||
        readRegularChecked(starter_cfg_path, owner, 1, 2, cfgBuf.get(), kStarterConfigMax, &cfgLen,
                           &cfgMeta)) {
        return failMsg(error, error_size, "starter.cfg missing or unsafe");
    }
    const StarterConfigState cfgState = StarterConfig::inspect(cfgBuf.get(), cfgLen, NULL);
    if (cfgState == nbt::storage::StarterInvalid) {
        return failMsg(error, error_size, "starter.cfg capture entries ambiguous or unsupported");
    }
    if (!wantInstall) {
        struct stat st;
        if (cfgState == nbt::storage::StarterStock && lstat(capDir, &st) && errno == ENOENT &&
            !txPresent && !origPresent &&
            nbt_fs_exists(fs, NBT_FS_CAPTURE_SHIM, "manager", NULL) == 0) {
            return 0;
        }
        return nbt_capture_remove_installed(fs, hmi_root, starter_cfg_path, remount, error,
                                            error_size);
    }
    const nbt_release_component &comp = release->components[captureIndex];
    const int legacy = legacyShimDir(capDir, txPresent > 0);
    if (txPresent == 0 && origPresent == 1 && cfgState == nbt::storage::StarterWithShim &&
        legacy == 0 && shimFileMatches(fs, comp.size, comp.sha256)) {
        armShim(fs, comp.sha256, false);
        return 0;
    }

    struct stat cfgDirSt, junk;
    if (legacy < 0 || lstat(cfgDir, &cfgDirSt) || !S_ISDIR(cfgDirSt.st_mode) ||
        (txPresent == 0 && (!lstat(cfgNext, &junk) || errno != ENOENT))) {
        return failMsg(error, error_size, "capture layout or leftover temporaries unsafe");
    }
    if (origPresent > 0) {
        if (readOrigBackup(fs, origBuf, &origLen)) {
            return failMsg(error, error_size, "invalid starter.cfg backup");
        }
        if (StarterConfig::installed(cfgState) &&
            !StarterConfig::matchesOriginOutsidePreload(cfgBuf.get(), cfgLen, origBuf.get(),
                                                        origLen)) {
            return failMsg(error, error_size, "starter.cfg has unrelated edits");
        }
    } else if (rebuildOrigBackup(cfgBuf.get(), cfgLen, origBuf, &origLen)) {
        return failMsg(error, error_size, "cannot rebuild starter.cfg backup");
    }
    if (!StarterConfig::buildModified(origBuf.get(), origLen, true, newCfgBuf.get(),
                                      kStarterConfigMax, &newCfgLen)) {
        return failMsg(error, error_size, "cannot format updated starter.cfg");
    }

    std::unique_ptr<char[]> shimBuf;
    if (readShimFromRelease(release_fd, comp, shimBuf)) {
        return failMsg(error, error_size, "cannot stage capture release data or backup");
    }
    // Before anything changes: a shim QNX's loader refuses would stop iDrive
    // before main at the start it is armed for.
    char probeDetail[224], probeError[288];
    if (probeCaptureShim(fs, cfgBuf.get(), cfgLen, shimBuf.get(), static_cast<size_t>(comp.size),
                         probeDetail, sizeof(probeDetail))) {
        snprintf(probeError, sizeof(probeError), "capture shim refused, nothing changed: %s",
                 probeDetail);
        nbt_logf("capture: %s", probeError);
        (void)failMsg(error, error_size, probeError);
        return NBT_CAPTURE_REFUSED;
    }
    if (probeDetail[0]) {
        nbt_logf("capture: load test passed: %s", probeDetail);
    }
    // Off while the shim file changes; the new one is armed once verified.
    const bool shimChanged = !shimFileMatches(fs, comp.size, comp.sha256);
    if (shimChanged && (nbt::storage::hmiEnvDisable(fs) || nbt::storage::shimRecordClear(fs))) {
        return failMsg(error, error_size, "cannot turn the previous shim off");
    }
    struct statvfs vol;
    if (statvfs(fs->root, &vol) || !SafePathOpen::volumeHasSpace(vol, comp.size + 65536u) ||
        faultMatch("enospc")) {
        return failMsg(error, error_size, "insufficient space for the capture shim");
    }
    if (saveOrigBackup(fs, origBuf.get(), origLen)) {
        return failMsg(error, error_size, "cannot stage capture release data or backup");
    }
    TxRecord tx;
    memset(&tx, 0, sizeof(tx));
    snprintf(tx.op, sizeof(tx.op), "install");
    snprintf(tx.phase, sizeof(tx.phase), "shim");
    sha256Bytes(origBuf.get(), origLen, tx.orig_sha);
    sha256Bytes(newCfgBuf.get(), newCfgLen, tx.target_sha);
    memcpy(tx.shim_sha, comp.sha256, 32);
    if (writeTxFile(fs, tx)) {
        return failMsg(error, error_size, "cannot record capture transaction");
    }
    // The shim next to iDrive's environment file: /var/opt, no remount.
    if (shimChanged && writeShimFile(fs, shimBuf.get(), static_cast<size_t>(comp.size))) {
        return failMsg(error, error_size, "cannot install capture shim");
    }
    if (!shimFileMatches(fs, comp.size, comp.sha256)) {
        return failMsg(error, error_size, "capture shim verification failed");
    }
    crashFault("after-shim-rename");
    const bool cfgChange = cfgState != nbt::storage::StarterWithShim;
    const int rc =
        cfgChange || legacy
            ? updateSystemFiles(starter_cfg_path, cfgDir, cfgNext, capDir, cfgChange, legacy > 0,
                                newCfgBuf.get(), newCfgLen, cfgMeta, tx.target_sha, cfgDirSt.st_dev,
                                remount, error, error_size)
            : 0;
    if (rc == 0) {
        (void)nbt_fs_unlink(fs, NBT_FS_CAPTURE_TX, "manager", NULL);
        if (changed) {
            *changed = shimChanged || cfgChange || legacy ? 1 : 0;
        }
        armShim(fs, comp.sha256, shimChanged);
    }
    return rc;
}
