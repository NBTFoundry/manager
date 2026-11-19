// SPDX-License-Identifier: MPL-2.0
/* Strictly validates and splices the two starter.cfg entries the capture shim
 * install may change: HmiMain3D's <EnvFile> (docs/hmi-restart.md) and, from
 * older installs, Environment 6 (System_HMI_PRELOAD). */
#ifndef NBT_STORAGE_STARTER_CONFIG_HPP
#define NBT_STORAGE_STARTER_CONFIG_HPP

#include <stddef.h>

namespace nbt {
namespace storage {

static const size_t kStarterConfigMax = 1024u * 1024u;

/* Stock: Environment 6 as BMW ships it and HmiMain3D's <EnvFile/> empty.
 * WithShim: <EnvFile> names our environment file (Environment 6 stock).
 * LegacyShim: Environment 6 lists the shim after BMW's preloads (dev91-dev99
 * installs; <EnvFile/> empty). Anything else, both forms included, is invalid. */
enum StarterConfigState {
    StarterInvalid = -1,
    StarterStock = 0,
    StarterWithShim = 1,
    StarterLegacyShim = 2
};

struct StarterConfigSpan {
    StarterConfigState state;
    size_t valueStart; /* Environment 6's <Value> text */
    size_t valueEnd;
    size_t envFileStart; /* HmiMain3D's whole <EnvFile/> or <EnvFile>...</EnvFile> */
    size_t envFileEnd;
};

class StarterConfig {
public:
    static const char *stockPreload();
    /* BMW's preloads then the shim: the LD_PRELOAD our environment file sets
     * (it replaces iDrive's list, so it repeats BMW's). */
    static const char *installedPreload();
    /* The environment file HmiMain3D's <EnvFile> names when installed. */
    static const char *envFilePath();
    static bool installed(StarterConfigState state) {
        return state == StarterWithShim || state == StarterLegacyShim;
    }

    static StarterConfigState inspect(const char *data, size_t size, StarterConfigSpan *span);
    /* Any valid input to Environment 6 stock and <EnvFile> ours (enable) or empty. */
    static bool buildModified(const char *src, size_t srcSize, bool enable, char *dst,
                              size_t dstCap, size_t *dstSize);
    /* True iff `current` is installed (either form), `orig` is stock, and both
     * match outside the two entries above. */
    static bool matchesOriginOutsidePreload(const char *current, size_t currentSize,
                                            const char *orig, size_t origSize);
};

} // namespace storage
} // namespace nbt
#endif
