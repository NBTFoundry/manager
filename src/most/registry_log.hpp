#ifndef NBT_MOST_REGISTRY_LOG_HPP
#define NBT_MOST_REGISTRY_LOG_HPP

#include "nbt/log.h"
#include "nbt/most/codec.hpp"

#include <stdio.h>

namespace nbt {
namespace most {

/* Logs unique MOST registry snapshots when debug logging is active. */
inline void logMostRegistry(const unsigned char *bytes, uint32_t length, bool &logged,
                            uint32_t &lastHash) {
    static const size_t kShownMax = 128;
    static const size_t kPerLine = 16;
    nbt_most_registry_entry entries[kShownMax];
    char line[NBT_LOG_LINE];
    uint32_t hash = 2166136261u;
    if (!nbt_log_enabled()) {
        return;
    }
    for (uint32_t i = 0; i < length; ++i) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    if (logged && hash == lastHash) {
        return;
    }
    logged = true;
    lastHash = hash;
    const long count = Codec::registry(bytes, length, entries, kShownMax);
    if (count < 0) {
        nbt_logf("MOST registry malformed (%lu bytes)", (unsigned long)length);
        return;
    }
    const size_t shown = (size_t)count < kShownMax ? (size_t)count : kShownMax;
    nbt_logf("MOST registry: %ld entries, FBlock key/first word in hex%s", count,
             shown < (size_t)count ? "; the first 128 shown" : "");
    for (size_t i = 0; i < shown; i += kPerLine) {
        size_t used = 0;
        size_t j;
        for (j = i; j < shown && j < i + kPerLine; ++j) {
            used += (size_t)snprintf(line + used, sizeof(line) - used, " %04X/%04X",
                                     (unsigned)entries[j].key, (unsigned)entries[j].first);
        }
        nbt_logf("MOST registry %lu-%lu:%s", (unsigned long)i + 1, (unsigned long)j, line);
    }
}

} // namespace most
} // namespace nbt

#endif
