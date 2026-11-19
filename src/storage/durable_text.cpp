#include "nbt/storage/durable_text.hpp"

#include "nbt/settings.h"
#include "nbt/storage/path_open.hpp"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace storage {
namespace {

void textFault(nbt_fs_path_kind kind, const char *step) {
#ifdef NBT_TESTING
    const char *fault = getenv("NBT_TEST_AUTOSTART_FAULT");
    if ((kind == NBT_FS_AUTOSTART || kind == NBT_FS_AUTOSTART_GUARD) && fault &&
        !strcmp(fault, step)) {
        _exit(91);
    }
#else
    (void)kind;
    (void)step;
#endif
}

} // namespace

core::Status DurableText::replace(const ManagedRoot &root, PathKind next, PathKind target,
                                  const char *app, core::Span<const char> text) {
    if (!root.valid() || (text.size() && !text.data())) {
        return core::Status::error(EINVAL, "invalid durable text input");
    }
    if (nbt_fs_replace_text(const_cast<nbt_fs *>(root.legacy()), next, target, app, text.data(),
                            text.size())) {
        return core::Status::fromErrno("durable text replacement failed");
    }
    return core::Status();
}

core::Status DurableText::rewriteSettings(const ManagedRoot &root, const char *app, int fd,
                                          core::Span<const char> text) {
    if (!root.valid() || (text.size() && !text.data())) {
        return core::Status::error(EINVAL, "invalid settings text input");
    }
    if (nbt_fs_settings_write(const_cast<nbt_fs *>(root.legacy()), app, fd, text.data(),
                              text.size())) {
        return core::Status::fromErrno("settings snapshot write failed");
    }
    return core::Status();
}

} // namespace storage
} // namespace nbt

int nbt_fs_settings_write(nbt_fs *fs, const char *app, int fd, const char *text, size_t length) {
    char path[NBT_PATH_MAX];
    struct stat st;
    ssize_t n = 0;
    size_t writeLength = length;
#ifdef NBT_TESTING
    const char *fault = getenv("NBT_TEST_SETTINGS_FAULT");
#endif
    if (!text || !length || length >= NBT_SETTINGS_VALUES_MAX ||
        nbt_fs_path(fs, NBT_FS_DATA_SETTINGS, app, NULL, path, sizeof(path)) ||
        nbt::storage::SafePathOpen::checkedFile(fs, path, fd) || fstat(fd, &st) ||
        (st.st_mode & 0077)) {
        errno = EPERM;
        return -1;
    }
    if (lseek(fd, 0, SEEK_SET) != 0) {
        return -1;
    }
#ifdef NBT_TESTING
    if (fault && !strcmp(fault, "write")) {
        writeLength = length / 2;
    }
#endif
    do {
        n = write(fd, text, writeLength);
    } while (n < 0 && errno == EINTR);
    if (n < 0) {
        return -1;
    }
    if ((size_t)n != length) {
        errno = EIO;
        return -1;
    }
#ifdef NBT_TESTING
    if (fault && !strcmp(fault, "truncate")) {
        errno = EIO;
        return -1;
    }
#endif
    if (ftruncate(fd, (off_t)length)) {
        return -1;
    }
#ifdef NBT_TESTING
    if (fault && !strcmp(fault, "sync")) {
        errno = EIO;
        return -1;
    }
#endif
    if (fsync(fd)) {
        return -1;
    }
#ifndef __QNX__
    if (nbt_fs_app_data(fs, app, path) || nbt::storage::SafePathOpen::syncDirPath(path)) {
        return -1;
    }
#endif
    return 0;
}

int nbt_fs_replace_text(nbt_fs *fs, nbt_fs_path_kind next, nbt_fs_path_kind kind, const char *app,
                        const char *text, size_t size) {
    size_t done = 0;
    int result = 0;
    if (!text || size > 4096 ||
        !((next == NBT_FS_SETTINGS_NEXT && kind == NBT_FS_SETTINGS) ||
          (next == NBT_FS_SETTINGS_SCHEMA_NEXT && kind == NBT_FS_SETTINGS_SCHEMA) ||
          (next == NBT_FS_AUTOSTART_NEXT && kind == NBT_FS_AUTOSTART) ||
          (next == NBT_FS_AUTOSTART_GUARD_NEXT && kind == NBT_FS_AUTOSTART_GUARD) ||
          (next == NBT_FS_FAULT_GUARD_NEXT && kind == NBT_FS_FAULT_GUARD))) {
        errno = EINVAL;
        return -1;
    }
    if (nbt_fs_exists(fs, kind, app, NULL) < 0) {
        return -1;
    }
    if (nbt_fs_unlink(fs, next, app, NULL) && errno != ENOENT) {
        return -1;
    }
    const int fd = nbt_fs_open(fs, next, app, NULL, NBT_FS_CREATE);
    if (fd < 0) {
        return -1;
    }
    nbt::storage::textFault(kind, "created");
    while (done < size && !result) {
        const ssize_t n = write(fd, text + done, size - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            result = -1;
        } else {
            done += (size_t)n;
        }
    }
    if (!result) {
        nbt::storage::textFault(kind, "written");
    }
    if (!result && fsync(fd)) {
        result = -1;
    }
    if (!result) {
        nbt::storage::textFault(kind, "synced");
    }
    if (close(fd)) {
        result = -1;
    }
    if (!result) {
        result = nbt_fs_rename(fs, next, kind, app, NULL);
    }
    if (!result) {
        nbt::storage::textFault(kind, "renamed");
    }
    if (!result) {
        result = nbt_fs_barrier(fs, app, NULL);
    }
    if (result) {
        const int saved = errno;
        (void)nbt_fs_unlink(fs, next, app, NULL);
        errno = saved;
        return -1;
    }
    nbt::storage::textFault(kind, "barrier");
    return 0;
}
