// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PACKAGE_PACKAGE_VERIFIER_HPP
#define NBT_PACKAGE_PACKAGE_VERIFIER_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/package.h"

namespace nbt {
namespace package {

class PackageVerifier {
public:
    static core::Result<nbt_package> peekApp(int fd);
    static core::Result<nbt_package> verify(int fd, int publicKeyFd, const char *appId,
                                            const char *compatibility);
    /* Whether the key's signature over the header and manifest is the
     * package's: no payload hashing, so cheap enough to try several keys.
     * verify() still checks the whole package. */
    static bool signedBy(int fd, int publicKeyFd);
#ifdef NBT_TESTING
    static core::Result<nbt_package> parseManifestForTesting(core::Span<const uint8_t> text,
                                                             uint64_t payloadSize);
#endif
};

} // namespace package
} // namespace nbt

#endif
