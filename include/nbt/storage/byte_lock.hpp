// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_STORAGE_BYTE_LOCK_HPP
#define NBT_STORAGE_BYTE_LOCK_HPP
#include <sys/types.h>
namespace nbt {
namespace storage {
/* Nonblocking byte locks; the caller's UniqueFd owns their lifetime. */
class ByteLock {
public:
    static bool set(int fd, short type, off_t byte);
    static bool occupied(int fd, off_t byte); // errors fail closed
};
} // namespace storage
} // namespace nbt
#endif
