/* Safe capture removal (starter.cfg before shim) and operational status check. */
#include "nbt/storage/capture_install.hpp"

#include "capture_io.hpp"
#include "nbt/capture/diag.h"
#include "nbt/capture/protocol.h"
#include "nbt/storage/hmi_env.hpp"
#include "nbt/storage/path_open.hpp"
#include "nbt/storage/starter_config.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <memory>
#include <new>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

using namespace nbt::storage::capture_io;
using nbt::storage::kStarterConfigMax;
using nbt::storage::StarterConfig;
using nbt::storage::StarterConfigState;

} // namespace

int nbt_capture_remove_installed(nbt_fs *fs, const char *hmi_root, const char *starter_cfg_path,
                                 nbt_remount_fn remount, char *error, unsigned error_size) {
    if (error && error_size) {
        *error = 0;
    }
    if (!fs || !hmi_root || !starter_cfg_path) {
        return failMsg(error, error_size, "invalid capture removal arguments");
    }
    char capDir[NBT_PATH_MAX], shimPath[NBT_PATH_MAX], shimNext[NBT_PATH_MAX];
    char cfgDir[NBT_PATH_MAX], cfgNext[NBT_PATH_MAX];
    if (buildCapturePaths(hmi_root, starter_cfg_path, capDir, shimPath, shimNext, cfgDir,
                          cfgNext)) {
        return failMsg(error, error_size, "capture paths invalid");
    }
    const uid_t owner = geteuid();
    std::unique_ptr<char[]> cfgBuf(new (std::nothrow) char[kStarterConfigMax]);
    std::unique_ptr<char[]> origBuf;
    size_t cfgLen = 0, origLen = 0;
    CfgMeta cfgMeta;
    if (!cfgBuf || readRegularChecked(starter_cfg_path, owner, 1, 2, cfgBuf.get(),
                                      kStarterConfigMax, &cfgLen, &cfgMeta)) {
        return failMsg(error, error_size, "starter.cfg missing or unsafe");
    }
    const StarterConfigState st = StarterConfig::inspect(cfgBuf.get(), cfgLen, NULL);
    if (st == nbt::storage::StarterInvalid) {
        return failMsg(error, error_size,
                       "starter.cfg has unexpected modifications; retaining shim");
    }
    const int origPresent = nbt_fs_exists(fs, NBT_FS_CAPTURE_STARTER_ORIG, "manager", NULL);
    if (origPresent < 0) {
        return failMsg(error, error_size, "cannot check starter.cfg backup");
    }
    // First, so that iDrive's next start is stock whatever happens below.
    if (nbt::storage::hmiEnvDisable(fs) || nbt::storage::shimRecordClear(fs)) {
        return failMsg(error, error_size, "cannot remove iDrive's environment file");
    }
    bool needCfgRestore = StarterConfig::installed(st);
    unsigned char origSha[32];
    if (origPresent > 0) {
        if (readOrigBackup(fs, origBuf, &origLen)) {
            return failMsg(error, error_size, "starter.cfg backup is corrupt; retaining files");
        }
        sha256Bytes(origBuf.get(), origLen, origSha);
        if (needCfgRestore && !StarterConfig::matchesOriginOutsidePreload(cfgBuf.get(), cfgLen,
                                                                          origBuf.get(), origLen)) {
            return failMsg(error, error_size,
                           "starter.cfg changed outside capture preload; manual review required");
        }
    } else if (needCfgRestore) {
        /* An older installer removed the manager folder, backup included. */
        if (rebuildOrigBackup(cfgBuf.get(), cfgLen, origBuf, &origLen)) {
            return failMsg(error, error_size, "cannot rebuild starter.cfg backup; retaining shim");
        }
        sha256Bytes(origBuf.get(), origLen, origSha);
    }

    const int legacy = legacyShimDir(capDir, true);
    const int shimPresent = nbt_fs_exists(fs, NBT_FS_CAPTURE_SHIM, "manager", NULL);
    if (legacy < 0 || shimPresent < 0) {
        return failMsg(error, error_size, "the shim's old folder is unsafe; retaining files");
    }
    if (!needCfgRestore && !legacy && !shimPresent && !origPresent &&
        nbt_fs_exists(fs, NBT_FS_CAPTURE_TX, "manager", NULL) == 0) {
        return 0;
    }

    int rc = 0;
    if (needCfgRestore || legacy) { // on /fs/sda0
        if (remount) {
            if (remount(1)) {
                return failMsg(error, error_size, "cannot remount /fs/sda0 writable");
            }
        } else {
            struct statvfs vol;
            if (!statvfs(cfgDir, &vol) && (vol.f_flag & ST_RDONLY)) {
                return failMsg(error, error_size, "insufficient space or read-only /fs/sda0");
            }
        }
        rc = -1;
        if (needCfgRestore) {
            (void)unlink(cfgNext);
            size_t verifyLen = 0;
            unsigned char verifySha[32];
            if (writeExactFile(cfgNext, starter_cfg_path, cfgDir, origBuf.get(), origLen,
                               cfgMeta.mode, &cfgMeta, "short-restore-cfg", "sync-restore-cfg",
                               "before-restore-rename")) {
                failMsg(error, error_size, "cannot restore starter.cfg; retaining shim");
            } else if (readRegularChecked(starter_cfg_path, owner, 1, 2, cfgBuf.get(),
                                          kStarterConfigMax, &verifyLen, NULL) ||
                       StarterConfig::inspect(cfgBuf.get(), verifyLen, NULL) !=
                           nbt::storage::StarterStock ||
                       (sha256Bytes(cfgBuf.get(), verifyLen, verifySha),
                        !nbt_constant_equal(verifySha, origSha, 32))) {
                failMsg(error, error_size, "starter.cfg restoration verification failed");
            } else {
                rc = 0;
            }
        } else {
            rc = 0;
        }
        if (rc == 0) {
            crashFault("before-remove-shim");
            if (legacy && removeLegacyShimDir(capDir)) {
                rc = failMsg(error, error_size, "cannot delete the shim's old folder");
            }
        }
        if (remount && remount(0) && rc == 0) {
            rc = failMsg(error, error_size, "cannot remount /fs/sda0 read-only");
        }
    } else {
        crashFault("before-remove-shim");
    }
    if (rc == 0) {
        (void)nbt_fs_unlink(fs, NBT_FS_CAPTURE_STARTER_ORIG, "manager", NULL);
        (void)nbt_fs_unlink(fs, NBT_FS_CAPTURE_TX, "manager", NULL);
        if (removeShimFile(fs)) {
            rc = failMsg(error, error_size, "cannot delete capture shim");
        }
    }
    return rc;
}

int nbt_capture_operational_status(const char *diag_path, char *out, unsigned out_size) {
    nbt_capture_diag_shm snap;
    if (!diag_path || nbt_capture_diag_read(diag_path, &snap) != 0) {
        if (out && out_size) {
            snprintf(out, out_size, "pending-restart");
        }
        return 0;
    }
    if (out && out_size) {
        snprintf(out, out_size, "%s pid=%d", snap.observer_mode ? "observer" : "active",
                 static_cast<int>(snap.pid));
    }
    return 1;
}
