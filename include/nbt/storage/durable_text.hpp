// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_STORAGE_DURABLE_TEXT_HPP
#define NBT_STORAGE_DURABLE_TEXT_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace storage {

class DurableText {
public:
    static core::Status replace(const ManagedRoot &root, PathKind next, PathKind target,
                                const char *app, core::Span<const char> text);
    static core::Status rewriteSettings(const ManagedRoot &root, const char *app, int fd,
                                        core::Span<const char> text);
};

} // namespace storage
} // namespace nbt

#endif
