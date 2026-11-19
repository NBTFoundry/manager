// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_MODEL_BRIDGE_HPP
#define NBT_MODEL_BRIDGE_HPP

#include "nbt/bridge.h"
#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"

namespace nbt {
namespace model {

class BridgeRequirement {
public:
    BridgeRequirement() : value_() {
    }
    static core::Result<BridgeRequirement> parse(char **text, const char *prefix);
    core::Result<size_t> format(const char *prefix, core::Span<char> output) const;
    const nbt_bridge_requirement &legacy() const {
        return value_;
    }

private:
    explicit BridgeRequirement(const nbt_bridge_requirement &value) : value_(value) {
    }
    nbt_bridge_requirement value_;
};

class Provider {
public:
    Provider() : value_() {
    }
    static Provider current();
    core::Status add(core::StringRef declaration);
    core::Result<nbt_bridge_version> check(const BridgeRequirement &need, const char *available,
                                           core::Span<char> error) const;
    const nbt_bridge_provider &legacy() const {
        return value_;
    }

private:
    nbt_bridge_provider value_;
};

core::Result<uint64_t> parseStatus(core::Span<const char> line, core::Span<char> text);
core::Result<size_t> formatStatus(uint64_t sequence, core::StringRef text, core::Span<char> output);

} // namespace model
} // namespace nbt

#endif
