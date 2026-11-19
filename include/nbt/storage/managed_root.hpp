#ifndef NBT_STORAGE_MANAGED_ROOT_HPP
#define NBT_STORAGE_MANAGED_ROOT_HPP

#include "nbt/core/status.hpp"
#include "nbt/core/unique_fd.hpp"
#include "nbt/storage/path_policy.hpp"

namespace nbt {
namespace storage {

class ManagedRoot {
public:
    ManagedRoot();
    explicit ManagedRoot(const nbt_fs &root) : root_(root), valid_(true) {
    }
    core::Status initialize(const char *root);
    bool valid() const {
        return valid_;
    }
    nbt_fs *legacy() {
        return &root_;
    }
    const nbt_fs *legacy() const {
        return &root_;
    }
    core::Status path(PathKind kind, const char *app, const char *tx,
                      core::Span<char> output) const;
    core::Result<core::UniqueFd> open(PathKind kind, const char *app, const char *tx,
                                      OpenMode mode) const;
    core::Result<core::UniqueFd> lease(const char *app) const;
    core::Result<core::UniqueFd> managerOperationLock() const;

private:
    nbt_fs root_;
    bool valid_;
    ManagedRoot(const ManagedRoot &);
    ManagedRoot &operator=(const ManagedRoot &);
};

} // namespace storage
} // namespace nbt

#endif
