#ifndef NBT_STORAGE_PATH_POLICY_HPP
#define NBT_STORAGE_PATH_POLICY_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/fs.h"

namespace nbt {
namespace storage {

typedef nbt_fs_path_kind PathKind;
typedef nbt_fs_open_mode OpenMode;

class PathPolicy {
public:
    static core::Status relative(PathKind kind, const char *app, const char *tx,
                                 core::Span<char> output);
    static bool canCreate(PathKind kind);
    static bool canLock(PathKind kind);
    static bool canUnlink(PathKind kind);
    static bool canReplace(PathKind from, PathKind to);
    static bool canRename(PathKind from, PathKind to, const char *app);
};

} // namespace storage
} // namespace nbt

#endif
