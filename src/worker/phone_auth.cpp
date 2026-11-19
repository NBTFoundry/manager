#include "nbt/worker/phone_auth.hpp"
#include "nbt/core/system.hpp"
#include "nbt/core/text.hpp"
#include <openssl/sha.h>
#include <stdio.h>
#include <string.h>
namespace nbt {
namespace worker {
PhoneNonces::PhoneNonces() : window_(0), checks_(0) {
}
bool PhoneNonces::issue(uint64_t now, char output[65]) {
    if (!output || now > UINT64_MAX - 60000) {
        return false;
    }
    for (size_t i = 0; i < 32; ++i) {
        if (entries_[i].expires <= now) {
            core::SecureBytes<32> bytes;
            if (!core::randomBytes(core::Span<uint8_t>(bytes.data(), 32)).ok() ||
                !core::hexEncode(core::Span<const uint8_t>(bytes.data(), 32),
                                 core::Span<char>(output, 65))
                     .ok()) {
                return false;
            }
            if (!entries_[i].text.set(output)) {
                return false;
            }
            entries_[i].expires = now + 60000;
            return true;
        }
    }
    return false;
}
bool PhoneNonces::take(const char *nonce, uint64_t now) {
    if (!nonce || strlen(nonce) != 64) {
        return false;
    }
    for (size_t i = 0; i < 32; ++i) {
        if (entries_[i].expires > now && entries_[i].text.size() == 64 &&
            core::constantEqual(
                core::Span<const uint8_t>(reinterpret_cast<const uint8_t *>(nonce), 64),
                core::Span<const uint8_t>(
                    reinterpret_cast<const uint8_t *>(entries_[i].text.data()), 64))) {
            entries_[i].text.wipe();
            entries_[i].expires = 0;
            return true;
        }
    }
    return false;
}
bool PhoneNonces::signatureAllowed(uint64_t now) {
    if (now < window_ || now - window_ >= 60000) {
        window_ = now;
        checks_ = 0;
    }
    if (checks_ >= 120) {
        return false;
    }
    ++checks_;
    return true;
}
bool phoneRequestDigest(const char *method, const char *path, const char *host, const char *nonce,
                        uint64_t length, const unsigned char bodyHash[32],
                        unsigned char output[32]) {
    unsigned char decoded[32];
    char hash[65], text[512];
    if (!method || (strcmp(method, "GET") && strcmp(method, "POST")) || !path ||
        strncmp(path, "/v1/", 4) || strlen(path) > 32 || strpbrk(path, "\r\n ") || !host ||
        !host[0] || strlen(host) > 127 || strpbrk(host, "\r\n ") || !nonce ||
        !core::hexDecode(core::StringRef(nonce), core::Span<uint8_t>(decoded, 32)).ok() ||
        !bodyHash || !output ||
        !core::hexEncode(core::Span<const uint8_t>(bodyHash, 32), core::Span<char>(hash, 65))
             .ok()) {
        return false;
    }
    const int n = snprintf(text, sizeof(text), "NBT-REQ-1\n%s\n%s\n%s\n%s\n%llu\n%s\n", method,
                           path, host, nonce, static_cast<unsigned long long>(length), hash);
    return n > 0 && static_cast<size_t>(n) < sizeof(text) &&
           SHA256(reinterpret_cast<const unsigned char *>(text), static_cast<size_t>(n), output);
}
bool phonePairingCode(const char *nonce, const unsigned char key[65], char output[7]) {
    unsigned char input[97], digest[32];
    if (!nonce || !key || !output ||
        !core::hexDecode(core::StringRef(nonce), core::Span<uint8_t>(input, 32)).ok()) {
        return false;
    }
    memcpy(input + 32, key, 65);
    if (!SHA256(input, sizeof(input), digest)) {
        return false;
    }
    const uint32_t number = (static_cast<uint32_t>(digest[0]) << 24) |
                            (static_cast<uint32_t>(digest[1]) << 16) |
                            (static_cast<uint32_t>(digest[2]) << 8) | digest[3];
    return snprintf(output, 7, "%06u", static_cast<unsigned>(number % 1000000)) == 6;
}
} // namespace worker
} // namespace nbt
