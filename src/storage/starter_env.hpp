#ifndef NBT_STORAGE_STARTER_ENV_HPP
#define NBT_STORAGE_STARTER_ENV_HPP

#include <stddef.h>

namespace nbt {
namespace storage {

static const unsigned kHmiEnvMax = 8;

struct HmiEnvEntry {
    char text[1024]; /* NAME=value */
    size_t nameLength;
};

struct HmiEnv {
    unsigned count;
    HmiEnvEntry entries[kHmiEnvMax];
};

/* The Environment entries starter.cfg's HmiMain3D Process block requires, in
 * its order. False (count 0) when the block, a number or an entry is missing,
 * repeated or malformed. */
bool hmiStarterEnvironment(const char *data, size_t size, HmiEnv &env);

} // namespace storage
} // namespace nbt

#endif
