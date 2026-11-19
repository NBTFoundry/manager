#ifndef NBT_MODEL_TARGET_HPP
#define NBT_MODEL_TARGET_HPP

#include "nbt/target.h"

namespace nbt {
namespace model {

inline bool isCompatibleTarget(const char *system, const char *release, bool armv7) {
    return nbt_target_compatible(system, release, armv7 ? 1 : 0) != 0;
}

} // namespace model
} // namespace nbt

#endif
