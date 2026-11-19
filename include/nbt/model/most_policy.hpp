#ifndef NBT_MODEL_MOST_POLICY_HPP
#define NBT_MODEL_MOST_POLICY_HPP

#include "nbt/core/status.hpp"
#include "nbt/most_policy.h"

namespace nbt {
namespace model {

class MostPolicy {
public:
    MostPolicy() : value_() {
    }
    explicit MostPolicy(const nbt_most_policy &value) : value_(value) {
    }
    static core::Result<MostPolicy> parse(const char *text);
    bool receives(unsigned fblock, unsigned instance, unsigned function) const;
    bool sends(unsigned fblock, unsigned instance, unsigned function) const;
    size_t controls(uint16_t keys[NBT_MOST_POLICY_CONTROLS]) const;
    nbt_most_policy &legacy() {
        return value_;
    }
    const nbt_most_policy &legacy() const {
        return value_;
    }

private:
    nbt_most_policy value_;
};

} // namespace model
} // namespace nbt

#endif
