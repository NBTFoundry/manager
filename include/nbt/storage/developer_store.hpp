// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_STORAGE_DEVELOPER_STORE_HPP
#define NBT_STORAGE_DEVELOPER_STORE_HPP
#include "nbt/crypto/rsa_public_key.hpp"
#include "nbt/storage/managed_root.hpp"
namespace nbt {
namespace storage {
/* A developer key: an RSA-3072 public key a paired phone added and the driver
 * approved on the car. Packages signed with it install like the apps key's;
 * an application stays bound to the key that signed its first package. */
struct Developer {
    char id[65];   // SHA-256 of the DER key, lowercase hex
    char name[33]; // printable ASCII, as phone names (PhoneStore::nameValid)
    unsigned char der[crypto::RsaPublicKey::kDerMax];
    size_t derSize;
    uint64_t added; // seconds since the epoch
};
/* trust/developers/<id>.conf, NBTDEV1 records, at most kMax of them. */
class DeveloperStore {
public:
    static const int kMax = 8;
    explicit DeveloperStore(ManagedRoot &root) : root_(root) {
    }
    bool initialize();
    /* The keys in no particular order, or -1 when the folder is unreadable or
     * holds anything but valid records. */
    int list(Developer output[kMax]);
    bool find(const char *id, Developer &output);
    bool add(const Developer &developer);
    bool remove(const char *id);
    /* "abcd ef01 2345 6789": the first 16 digits of the key id, the
     * fingerprint the car and the phone show. */
    static void fingerprint(const char *id, char output[20]);
    /* Fills id and der from a DER key; false for anything but a canonical
     * RSA-3072 key with exponent 65537. */
    static bool fromDer(const unsigned char *der, size_t size, Developer &output);

private:
    bool read(const char *id, Developer &output);
    bool sync();
    ManagedRoot &root_;
};
} // namespace storage
} // namespace nbt
#endif
