// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_STORAGE_LOCKS_HPP
#define NBT_STORAGE_LOCKS_HPP

#include "nbt/core/status.hpp"
#include "nbt/core/unique_fd.hpp"

#include <sys/types.h>

namespace nbt {
namespace storage {

class RecordLock {
public:
    static core::Result<RecordLock> acquire(core::UniqueFd fd, bool write = true);
    RecordLock(RecordLock &&other);
    RecordLock &operator=(RecordLock &&other);
    RecordLock(const RecordLock &) = delete;
    RecordLock &operator=(const RecordLock &) = delete;
    ~RecordLock();
    int fd() const {
        return fd_.get();
    }
    core::Status close();

private:
    RecordLock();
    core::UniqueFd fd_;
#ifdef NBT_TESTING
    dev_t device_;
    ino_t inode_;
    bool registered_;
#endif
};

class AppTxLock {
public:
    static core::Result<AppTxLock> acquire(core::UniqueFd fd);
    AppTxLock(AppTxLock &&) = default;
    AppTxLock &operator=(AppTxLock &&) = default;
    AppTxLock(const AppTxLock &) = delete;
    AppTxLock &operator=(const AppTxLock &) = delete;
    core::Status close() {
        return lock_.close();
    }

private:
    explicit AppTxLock(RecordLock &&lock) : lock_(std::move(lock)) {
    }
    RecordLock lock_;
};

class ManagerLock {
public:
    static core::Result<ManagerLock> acquire(core::UniqueFd fd);
    ManagerLock(ManagerLock &&) = default;
    ManagerLock &operator=(ManagerLock &&) = default;
    ManagerLock(const ManagerLock &) = delete;
    ManagerLock &operator=(const ManagerLock &) = delete;

private:
    explicit ManagerLock(RecordLock &&lock) : lock_(std::move(lock)) {
    }
    RecordLock lock_;
};

} // namespace storage
} // namespace nbt

#endif
