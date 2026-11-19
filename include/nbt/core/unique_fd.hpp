// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_CORE_UNIQUE_FD_HPP
#define NBT_CORE_UNIQUE_FD_HPP

#include "nbt/core/config.hpp"
#include "nbt/core/status.hpp"

#include <errno.h>
#include <unistd.h>
#include <utility>

namespace nbt {
namespace core {

class UniqueFd {
public:
    UniqueFd() : fd_(-1) {
    }
    explicit UniqueFd(int fd) : fd_(fd) {
    }
    UniqueFd(const UniqueFd &) = delete;
    UniqueFd &operator=(const UniqueFd &) = delete;
    UniqueFd(UniqueFd &&other) : fd_(other.release()) {
    }
    UniqueFd &operator=(UniqueFd &&other) {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }
    ~UniqueFd() {
        const int saved = errno;
        closeIgnoringErrors();
        errno = saved;
    }
    int get() const {
        return fd_;
    }
    bool valid() const {
        return fd_ >= 0;
    }
    int release() {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }
    void reset(int fd = -1) {
        if (fd == fd_) {
            return;
        }
        const int saved = errno;
        closeIgnoringErrors();
        fd_ = fd;
        errno = saved;
    }
    NBT_WARN_UNUSED Status close() {
        if (fd_ < 0) {
            return Status();
        }
        const int fd = fd_;
        fd_ = -1;
        if (::close(fd)) {
            return Status::fromErrno("close failed");
        }
        return Status();
    }

private:
    void closeIgnoringErrors() {
        if (fd_ >= 0) {
            (void)::close(fd_);
        }
        fd_ = -1;
    }
    int fd_;
};

} // namespace core
} // namespace nbt

#endif
