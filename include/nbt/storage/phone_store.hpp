// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_STORAGE_PHONE_STORE_HPP
#define NBT_STORAGE_PHONE_STORE_HPP
#include "nbt/storage/managed_root.hpp"
namespace nbt {
namespace storage {
struct Phone {
    char id[65];
    char name[33];
    unsigned char key[65];
    uint64_t paired;
};
class PhoneStore {
public:
    explicit PhoneStore(ManagedRoot &root) : root_(root) {
    }
    bool initialize();
    int list(Phone output[4]);
    bool find(const char *id, Phone &output);
    bool add(const Phone &phone);
    bool remove(const char *id);
    bool openEnabled();
    bool setOpenEnabled(bool enabled);
    static bool nameValid(const char *name);

private:
    bool read(const char *id, Phone &output);
    bool sync();
    bool replace(PathKind next, PathKind target, const char *id, const char *text);
    ManagedRoot &root_;
};
} // namespace storage
} // namespace nbt
#endif
