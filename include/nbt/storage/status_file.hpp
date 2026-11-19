#ifndef NBT_STORAGE_STATUS_FILE_HPP
#define NBT_STORAGE_STATUS_FILE_HPP

#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace storage {

class StatusFile {
public:
    static core::Result<core::UniqueFd> open(const ManagedRoot &root, const char *app, bool create);
    static core::Status write(int fd, core::Span<const char> line);
};

} // namespace storage
} // namespace nbt

#endif
