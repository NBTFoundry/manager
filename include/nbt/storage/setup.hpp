#ifndef NBT_STORAGE_SETUP_HPP
#define NBT_STORAGE_SETUP_HPP

#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace storage {

class SetupLayout {
public:
    static core::Status createRoot(ManagedRoot *root, const char *path, uint64_t required);
    static core::Status create(const ManagedRoot &root);
    static core::Result<core::UniqueFd> createFile(const ManagedRoot &root, PathKind next);
    static core::Status publish(const ManagedRoot &root, PathKind next);
    static core::Status discard(const ManagedRoot &root, PathKind next);
    static core::Status sync(const ManagedRoot &root);
};

class AppProvisioningDirs {
public:
    static core::Status create(const ManagedRoot &root, const char *app);
    static core::Result<core::UniqueFd> createFile(const ManagedRoot &root, PathKind kind,
                                                   const char *app, const char *tx);
    static core::Status discard(const ManagedRoot &root, const char *app);
    static core::Status sync(const ManagedRoot &root, const char *app);
};

} // namespace storage
} // namespace nbt

#endif
