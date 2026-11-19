#ifndef NBT_WORKER_PHONE_AUTH_HPP
#define NBT_WORKER_PHONE_AUTH_HPP
#include "nbt/core/secure.hpp"
#include <stdint.h>
namespace nbt {
namespace worker {
class PhoneNonces {
public:
    PhoneNonces();
    bool issue(uint64_t now, char output[65]);
    bool take(const char *nonce, uint64_t now);
    bool signatureAllowed(uint64_t now);

private:
    struct Entry {
        core::SecureString<64> text;
        uint64_t expires = 0;
    };
    Entry entries_[32];
    uint64_t window_;
    unsigned checks_;
};
// Exact ASCII request text, including final LF; hash is the complete raw body.
bool phoneRequestDigest(const char *method, const char *path, const char *host, const char *nonce,
                        uint64_t length, const unsigned char bodyHash[32],
                        unsigned char output[32]);
bool phonePairingCode(const char *nonce, const unsigned char key[65], char output[7]);
} // namespace worker
} // namespace nbt
#endif
