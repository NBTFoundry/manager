// SPDX-License-Identifier: MPL-2.0
#include "nbt/storage/locks.hpp"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <utility>

#ifdef NBT_TESTING
#include "nbt/core/mutex.hpp"
#endif

namespace nbt {
namespace storage {
namespace {
#ifdef NBT_TESTING
struct LockEntry {
    dev_t device;
    ino_t inode;
    bool used;
};
core::StaticMutex registryMutex = NBT_STATIC_MUTEX_INITIALIZER;
LockEntry registry[64];

void registerInode(dev_t device, ino_t inode) {
    core::LockGuard guard(registryMutex);
    size_t free = sizeof(registry) / sizeof(registry[0]);
    for (size_t i = 0; i < sizeof(registry) / sizeof(registry[0]); ++i) {
        if (registry[i].used && registry[i].device == device && registry[i].inode == inode) {
            NBT_CHECK(!"locked inode opened again");
        }
        if (!registry[i].used && free == sizeof(registry) / sizeof(registry[0])) {
            free = i;
        }
    }
    NBT_CHECK(free < sizeof(registry) / sizeof(registry[0]));
    registry[free].device = device;
    registry[free].inode = inode;
    registry[free].used = true;
}

void unregisterInode(dev_t device, ino_t inode) {
    core::LockGuard guard(registryMutex);
    for (size_t i = 0; i < sizeof(registry) / sizeof(registry[0]); ++i) {
        if (registry[i].used && registry[i].device == device && registry[i].inode == inode) {
            registry[i].used = false;
            return;
        }
    }
    NBT_CHECK(!"lock registry entry missing");
}
#endif
} // namespace

RecordLock::RecordLock()
    : fd_()
#ifdef NBT_TESTING
      ,
      device_(0), inode_(0), registered_(false)
#endif
{
}

core::Result<RecordLock> RecordLock::acquire(core::UniqueFd fd, bool write) {
    if (!fd.valid()) {
        return core::Result<RecordLock>::failure(
            core::Status::error(EBADF, "invalid lock descriptor"));
    }
    struct stat metadata;
    if (fstat(fd.get(), &metadata)) {
        const core::Status status = core::Status::fromErrno("stat lock failed");
        return core::Result<RecordLock>::failure(status);
    }
    struct flock range;
    memset(&range, 0, sizeof(range));
    range.l_type = write ? F_WRLCK : F_RDLCK;
    range.l_whence = SEEK_SET;
    int result;
    do {
        result = fcntl(fd.get(), F_SETLKW, &range);
    } while (result && errno == EINTR);
    if (result) {
        return core::Result<RecordLock>::failure(core::Status::fromErrno("record lock failed"));
    }
    RecordLock lock;
    lock.fd_ = std::move(fd);
#ifdef NBT_TESTING
    registerInode(metadata.st_dev, metadata.st_ino);
    lock.device_ = metadata.st_dev;
    lock.inode_ = metadata.st_ino;
    lock.registered_ = true;
#endif
    return core::Result<RecordLock>::success(std::move(lock));
}

RecordLock::RecordLock(RecordLock &&other)
    : fd_(std::move(other.fd_))
#ifdef NBT_TESTING
      ,
      device_(other.device_), inode_(other.inode_), registered_(other.registered_)
#endif
{
#ifdef NBT_TESTING
    other.registered_ = false;
#endif
}

RecordLock &RecordLock::operator=(RecordLock &&other) {
    if (this == &other) {
        return *this;
    }
    close();
    fd_ = std::move(other.fd_);
#ifdef NBT_TESTING
    device_ = other.device_;
    inode_ = other.inode_;
    registered_ = other.registered_;
    other.registered_ = false;
#endif
    return *this;
}

RecordLock::~RecordLock() {
    (void)close();
}

core::Status RecordLock::close() {
#ifdef NBT_TESTING
    if (registered_) {
        unregisterInode(device_, inode_);
        registered_ = false;
    }
#endif
    return fd_.close();
}

core::Result<AppTxLock> AppTxLock::acquire(core::UniqueFd fd) {
    core::Result<RecordLock> lock = RecordLock::acquire(std::move(fd));
    if (!lock.get()) {
        return core::Result<AppTxLock>::failure(lock.status());
    }
    return core::Result<AppTxLock>::success(AppTxLock(std::move(lock.value())));
}

core::Result<ManagerLock> ManagerLock::acquire(core::UniqueFd fd) {
    core::Result<RecordLock> lock = RecordLock::acquire(std::move(fd));
    if (!lock.get()) {
        return core::Result<ManagerLock>::failure(lock.status());
    }
    return core::Result<ManagerLock>::success(ManagerLock(std::move(lock.value())));
}

} // namespace storage
} // namespace nbt
