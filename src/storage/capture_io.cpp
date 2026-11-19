/* Checked shared-storage primitives for capture install/removal transactions. */
#include "capture_io.hpp"

#include "nbt/core/unique_fd.hpp"
#include "nbt/storage/path_open.hpp"
#include "nbt/util.h"

#include <errno.h>
#include <fcntl.h>
#include <new>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace storage {
namespace capture_io {

int failMsg(char *err, unsigned sz, const char *msg) {
    if (err && sz) {
        snprintf(err, sz, "%s", msg);
    }
    return -1;
}

bool faultMatch(const char *name) {
#ifdef NBT_TESTING
    const char *f = getenv("NBT_TEST_CAPTURE_FAULT");
    return f && name && !strcmp(f, name);
#else
    (void)name;
    return false;
#endif
}

void crashFault(const char *name) {
    if (faultMatch(name)) {
        _exit(91);
    }
}

void sha256Bytes(const char *data, size_t len, unsigned char out[32]) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data, len);
    SHA256_Final(out, &ctx);
}

static int writeAllLoop(int fd, const void *data, size_t len, const char *shortFault) {
    const char *p = static_cast<const char *>(data);
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (shortFault && faultMatch(shortFault) && chunk > 64) {
            chunk = 64;
        }
        const ssize_t n = write(fd, p + off, chunk);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0 || (shortFault && faultMatch(shortFault))) {
            errno = EIO;
            return -1;
        }
        off += static_cast<size_t>(n);
    }
    return 0;
}

static int readAllExact(int fd, void *out, size_t want) {
    char *p = static_cast<char *>(out);
    size_t used = 0;
    while (used < want) {
        const ssize_t n = read(fd, p + used, want - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return -1;
        }
        used += static_cast<size_t>(n);
    }
    char extra = 0;
    if (read(fd, &extra, 1) != 0) {
        errno = EFBIG;
        return -1;
    }
    return 0;
}

int writeFsAtomically(nbt_fs *fs, nbt_fs_path_kind nextKind, nbt_fs_path_kind finalKind,
                      const void *data, size_t len) {
    (void)nbt_fs_unlink(fs, nextKind, "manager", NULL);
    core::UniqueFd fd(nbt_fs_open(fs, nextKind, "manager", NULL, NBT_FS_CREATE));
    if (!fd.valid() || writeAllLoop(fd.get(), data, len, NULL) || fsync(fd.get()) ||
        !fd.close().ok() || nbt_fs_rename(fs, nextKind, finalKind, "manager", NULL)) {
        (void)nbt_fs_unlink(fs, nextKind, "manager", NULL);
        return -1;
    }
    return 0;
}

int readRegularChecked(const char *path, uid_t owner, unsigned minLinks, unsigned maxLinks,
                       char *out, size_t cap, size_t *outLen, CfgMeta *meta) {
    struct stat lst, fst;
    int flags = O_RDONLY;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    if (lstat(path, &lst) || !S_ISREG(lst.st_mode) || lst.st_uid != owner ||
        !SafePathOpen::carModeSafe(&lst) || lst.st_nlink < minLinks || lst.st_nlink > maxLinks) {
        errno = EPERM;
        return -1;
    }
    core::UniqueFd fd(open(path, flags));
    if (!fd.valid() || fstat(fd.get(), &fst) || !S_ISREG(fst.st_mode) || fst.st_dev != lst.st_dev ||
        fst.st_ino != lst.st_ino || fst.st_size <= 0 || static_cast<size_t>(fst.st_size) > cap ||
        readAllExact(fd.get(), out, static_cast<size_t>(fst.st_size))) {
        return -1;
    }
    *outLen = static_cast<size_t>(fst.st_size);
    if (meta) {
        meta->mode = fst.st_mode & 0777;
        meta->uid = fst.st_uid;
        meta->gid = fst.st_gid;
    }
    return 0;
}

int writeExactFile(const char *nextPath, const char *finalPath, const char *dirPath,
                   const void *data, size_t len, mode_t mode, const CfgMeta *ownerMeta,
                   const char *shortFault, const char *syncFault, const char *renameFault) {
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    core::UniqueFd fd(open(nextPath, flags, mode));
    if (!fd.valid()) {
        return -1;
    }
    if (writeAllLoop(fd.get(), data, len, shortFault) || fchmod(fd.get(), mode) ||
        (ownerMeta && geteuid() == 0 && fchown(fd.get(), ownerMeta->uid, ownerMeta->gid)) ||
        (syncFault && faultMatch(syncFault)) || fsync(fd.get()) || !fd.close().ok()) {
        fd.reset();
        (void)unlink(nextPath);
        return -1;
    }
    if (renameFault) {
        crashFault(renameFault);
    }
    if (rename(nextPath, finalPath)) {
        (void)unlink(nextPath);
        return -1;
    }
#ifdef __QNX__
    (void)dirPath;
    core::UniqueFd syncFd(open(finalPath, O_WRONLY));
    return (!syncFd.valid() || SafePathOpen::syncWritableFd(syncFd.release())) ? -1 : 0;
#else
    return SafePathOpen::syncDirPath(dirPath);
#endif
}

int buildCapturePaths(const char *hmiRoot, const char *cfgPath, char capDir[NBT_PATH_MAX],
                      char shim[NBT_PATH_MAX], char shimNext[NBT_PATH_MAX],
                      char cfgDir[NBT_PATH_MAX], char cfgNext[NBT_PATH_MAX]) {
    if (!hmiRoot || !cfgPath ||
        snprintf(capDir, NBT_PATH_MAX, "%s/NbtCapture", hmiRoot) >= NBT_PATH_MAX ||
        snprintf(shim, NBT_PATH_MAX, "%s/libnbt-capture.so", capDir) >= NBT_PATH_MAX ||
        snprintf(shimNext, NBT_PATH_MAX, "%s/.libnbt-capture.so.next", capDir) >= NBT_PATH_MAX) {
        return -1;
    }
    const char *slash = strrchr(cfgPath, '/');
    if (!slash || slash == cfgPath) {
        return -1;
    }
    const size_t dirLen = static_cast<size_t>(slash - cfgPath);
    if (dirLen >= NBT_PATH_MAX) {
        return -1;
    }
    memcpy(cfgDir, cfgPath, dirLen);
    cfgDir[dirLen] = 0;
    if (snprintf(cfgNext, NBT_PATH_MAX, "%s/.%s.next", cfgDir, slash + 1) >= NBT_PATH_MAX) {
        return -1;
    }
    return 0;
}

bool fileMatchesSha(const char *path, uid_t owner, uint64_t size, const unsigned char sha[32],
                    mode_t modeMask) {
    struct stat st, lst;
    unsigned char buf[8192], digest[32];
    SHA256_CTX ctx;
    if (lstat(path, &lst) || !S_ISREG(lst.st_mode) || lst.st_uid != owner ||
        (lst.st_mode & 0777) != modeMask || lst.st_nlink != 1) {
        return false;
    }
    core::UniqueFd fd(open(path, O_RDONLY));
    if (!fd.valid() || fstat(fd.get(), &st) || st.st_dev != lst.st_dev || st.st_ino != lst.st_ino ||
        st.st_size < 0 || static_cast<uint64_t>(st.st_size) != size) {
        return false;
    }
    SHA256_Init(&ctx);
    uint64_t got = 0;
    while (got < size) {
        const ssize_t n = read(fd.get(), buf, sizeof(buf));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        SHA256_Update(&ctx, buf, static_cast<size_t>(n));
        got += static_cast<uint64_t>(n);
    }
    SHA256_Final(digest, &ctx);
    return got == size && nbt_constant_equal(digest, sha, 32);
}

int readOrigBackup(nbt_fs *fs, std::unique_ptr<char[]> &origBuf, size_t *origLen) {
    core::UniqueFd ofd(nbt_fs_open(fs, NBT_FS_CAPTURE_STARTER_ORIG, "manager", NULL, NBT_FS_READ));
    if (!ofd.valid()) {
        return -1;
    }
    struct stat ost;
    if (fstat(ofd.get(), &ost) || ost.st_size <= 0 ||
        static_cast<size_t>(ost.st_size) > kStarterConfigMax) {
        return -1;
    }
    *origLen = static_cast<size_t>(ost.st_size);
    origBuf.reset(new (std::nothrow) char[*origLen]);
    if (!origBuf) {
        errno = ENOMEM;
        return -1;
    }
    if (readAllExact(ofd.get(), origBuf.get(), *origLen)) {
        return -1;
    }
    return StarterConfig::inspect(origBuf.get(), *origLen, NULL) == StarterStock ? 0 : -1;
}

int rebuildOrigBackup(const char *cfg, size_t cfgLen, std::unique_ptr<char[]> &origBuf,
                      size_t *origLen) {
    if (!cfg || !origLen || StarterConfig::inspect(cfg, cfgLen, NULL) == StarterInvalid) {
        errno = EINVAL;
        return -1;
    }
    origBuf.reset(new (std::nothrow) char[kStarterConfigMax]);
    if (!origBuf) {
        errno = ENOMEM;
        return -1;
    }
    if (!StarterConfig::buildModified(cfg, cfgLen, false, origBuf.get(), kStarterConfigMax,
                                      origLen) ||
        StarterConfig::inspect(origBuf.get(), *origLen, NULL) != StarterStock) {
        origBuf.reset();
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int saveOrigBackup(nbt_fs *fs, const char *origData, size_t origLen) {
    const int exists = nbt_fs_exists(fs, NBT_FS_CAPTURE_STARTER_ORIG, "manager", NULL);
    if (exists != 0) {
        return exists > 0 ? 0 : -1;
    }
    return writeFsAtomically(fs, NBT_FS_CAPTURE_STARTER_ORIG_NEXT, NBT_FS_CAPTURE_STARTER_ORIG,
                             origData, origLen);
}

int writeTxFile(nbt_fs *fs, const TxRecord &tx) {
    char text[256], origHex[65], targetHex[65], shimHex[65];
    if (nbt_hex(tx.orig_sha, 32, origHex, sizeof(origHex)) ||
        nbt_hex(tx.target_sha, 32, targetHex, sizeof(targetHex)) ||
        nbt_hex(tx.shim_sha, 32, shimHex, sizeof(shimHex))) {
        return -1;
    }
    const int n = snprintf(text, sizeof(text), "v1\nop=%s\nphase=%s\norig=%s\ntarget=%s\nshim=%s\n",
                           tx.op, tx.phase, origHex, targetHex, shimHex);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(text)) {
        return -1;
    }
    return writeFsAtomically(fs, NBT_FS_CAPTURE_TX_NEXT, NBT_FS_CAPTURE_TX, text,
                             static_cast<size_t>(n));
}

int checkSafeDir(const char *path, uid_t owner, dev_t dev, bool createIfMissing) {
    struct stat st;
    if (lstat(path, &st)) {
        if (createIfMissing && errno == ENOENT) {
            if (mkdir(path, 0755) || lstat(path, &st)) {
                return -1;
            }
        } else {
            return -1;
        }
    }
    if (!S_ISDIR(st.st_mode) || st.st_uid != owner || st.st_dev != dev ||
        !SafePathOpen::carModeSafe(&st)) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

} // namespace capture_io
} // namespace storage
} // namespace nbt
