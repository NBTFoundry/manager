// SPDX-License-Identifier: MPL-2.0
/* Internal shared I/O and verification helpers for capture install/removal. */
#ifndef NBT_STORAGE_CAPTURE_IO_HPP
#define NBT_STORAGE_CAPTURE_IO_HPP

#include "nbt/fs.h"
#include "nbt/release.h"
#include "nbt/storage/starter_config.hpp"

#include <memory>
#include <stdint.h>
#include <sys/types.h>

namespace nbt {
namespace storage {
namespace capture_io {

struct CfgMeta {
    mode_t mode;
    uid_t uid;
    gid_t gid;
};

struct TxRecord {
    char op[16];
    char phase[24];
    unsigned char orig_sha[32];
    unsigned char target_sha[32];
    unsigned char shim_sha[32];
};

int failMsg(char *err, unsigned sz, const char *msg);
bool faultMatch(const char *name);
void crashFault(const char *name);
void sha256Bytes(const char *data, size_t len, unsigned char out[32]);

int readRegularChecked(const char *path, uid_t owner, unsigned minLinks, unsigned maxLinks,
                       char *out, size_t cap, size_t *outLen, CfgMeta *meta);
int writeExactFile(const char *nextPath, const char *finalPath, const char *dirPath,
                   const void *data, size_t len, mode_t mode, const CfgMeta *ownerMeta,
                   const char *shortFault, const char *syncFault, const char *renameFault);
int buildCapturePaths(const char *hmiRoot, const char *cfgPath, char capDir[NBT_PATH_MAX],
                      char shim[NBT_PATH_MAX], char shimNext[NBT_PATH_MAX],
                      char cfgDir[NBT_PATH_MAX], char cfgNext[NBT_PATH_MAX]);
bool fileMatchesSha(const char *path, uid_t owner, uint64_t size, const unsigned char sha[32],
                    mode_t modeMask);
int readOrigBackup(nbt_fs *fs, std::unique_ptr<char[]> &origBuf, size_t *origLen);
/* The stock starter.cfg rebuilt from the current one when the backup is gone
 * (an older installer removed the manager folder): StarterConfig accepts only
 * the exact stock value or stock plus our one entry, so swapping that value
 * back is the original byte for byte. Fails for anything else. */
int rebuildOrigBackup(const char *cfg, size_t cfgLen, std::unique_ptr<char[]> &origBuf,
                      size_t *origLen);
int saveOrigBackup(nbt_fs *fs, const char *origData, size_t origLen);
/* Writes a manager state file whole: `nextKind`, synced, renamed to `finalKind`. */
int writeFsAtomically(nbt_fs *fs, nbt_fs_path_kind nextKind, nbt_fs_path_kind finalKind,
                      const void *data, size_t len);
int writeTxFile(nbt_fs *fs, const TxRecord &tx);
int checkSafeDir(const char *path, uid_t owner, dev_t dev, bool createIfMissing);

/* The shim in the manager's state folder, next to iDrive's environment file
 * (capture_shim_file.cpp): written whole (0700, synced, renamed), its SHA-256,
 * removed. */
int writeShimFile(nbt_fs *fs, const char *data, size_t len);
int shimFileSha(nbt_fs *fs, unsigned char sha[32], size_t *size);
bool shimFileMatches(nbt_fs *fs, uint64_t size, const unsigned char sha[32]);
int removeShimFile(nbt_fs *fs);
/* The folder dev91-dev103 kept the shim in (<hmi root>/NbtCapture): 1 present
 * with only our files, 0 absent, -1 anything else (left alone). */
int legacyShimDir(const char *capDir, bool allowTemp);
int removeLegacyShimDir(const char *capDir);

} // namespace capture_io
} // namespace storage
} // namespace nbt
#endif
