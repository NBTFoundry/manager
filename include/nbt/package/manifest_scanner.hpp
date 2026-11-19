#ifndef NBT_PACKAGE_MANIFEST_SCANNER_HPP
#define NBT_PACKAGE_MANIFEST_SCANNER_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"
#include "nbt/crypto/sha256.hpp"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace package {

class PackageStream {
public:
    static uint32_t be32(const unsigned char *p);
    static uint64_t be64(const unsigned char *p);
    static core::Status readExact(int fd, core::Span<uint8_t> output);
    static bool validArmElf(core::Span<const uint8_t> bytes, bool sharedLibraryOnly);
    static core::Status
    verifySignature(int keyFd, bool managerKey, core::Span<const uint8_t> header,
                    core::Span<const uint8_t> manifest, core::Span<const uint8_t> signature,
                    crypto::Sha256 *packageSha, const char *digestErr, const char *sigErr);
    static core::Status hashStream(int fd, uint64_t size, int outputFd,
                                   core::Span<uint8_t> firstOut, size_t *firstSizeOut,
                                   crypto::Sha256 *packageSha, core::Span<uint8_t> digestOut,
                                   const char *readErr, const char *digestErr,
                                   const char *writeErr);
    static int legacyError(char *error, unsigned size, const char *message);
};

} // namespace package
} // namespace nbt

#endif
