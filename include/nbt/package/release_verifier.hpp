// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PACKAGE_RELEASE_VERIFIER_HPP
#define NBT_PACKAGE_RELEASE_VERIFIER_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/release.h"

namespace nbt {
namespace package {

class ReleaseVerifier {
public:
    virtual ~ReleaseVerifier() {
    }
    virtual core::Result<nbt_release> verify(int fd, const char *compatibility) = 0;
};

class SignedReleaseVerifier : public ReleaseVerifier {
public:
    explicit SignedReleaseVerifier(int managerKeyFd) : managerKeyFd_(managerKeyFd) {
    }
    core::Result<nbt_release> verify(int fd, const char *compatibility) override;
    static core::Status copyComponent(int fd, const nbt_release &verified, unsigned index,
                                      int outputFd);
#ifdef NBT_TESTING
    /* format: 1, 2 or 3 (NBTRLS1-3). */
    static core::Status parseManifestForTesting(core::Span<const uint8_t> text,
                                                unsigned format = 1);
#endif

private:
    int managerKeyFd_;
};

int findComponent(const nbt_release &release, const char *name);

} // namespace package
} // namespace nbt

#endif
