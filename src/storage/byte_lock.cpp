// SPDX-License-Identifier: MPL-2.0
#include "nbt/storage/byte_lock.hpp"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
namespace nbt {
namespace storage {
namespace {
int operate(int fd, int operation, short type, off_t byte, struct flock &lock) {
    if (byte < 0) {
        errno = EINVAL;
        return -1;
    }
    memset(&lock, 0, sizeof(lock));
    lock.l_type = type;
    lock.l_whence = SEEK_SET;
    lock.l_start = byte;
    lock.l_len = 1;
    int result;
    do {
        result = fcntl(fd, operation, &lock);
    } while (result && errno == EINTR);
    return result;
}
} // namespace
bool ByteLock::set(int fd, short type, off_t byte) {
    struct flock lock;
    return !operate(fd, F_SETLK, type, byte, lock);
}
bool ByteLock::occupied(int fd, off_t byte) {
    struct flock lock;
    return operate(fd, F_GETLK, F_WRLCK, byte, lock) || lock.l_type != F_UNLCK;
}
} // namespace storage
} // namespace nbt
