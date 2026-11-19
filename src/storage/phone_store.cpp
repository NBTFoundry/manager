#include "nbt/storage/phone_store.hpp"
#include "nbt/crypto/ec_public_key.hpp"
#include "nbt/storage/path_open.hpp"
#include "nbt/util.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
namespace nbt {
namespace storage {
bool PhoneStore::nameValid(const char *name) {
    if (!name || !*name || strlen(name) > 32 || *name == ' ' || name[strlen(name) - 1] == ' ') {
        return false;
    }
    for (size_t i = 0; name[i]; ++i) {
        if (name[i] < 32 || name[i] > 126 || name[i] == '|' || name[i] == '\t') {
            return false;
        }
    }
    return true;
}
bool PhoneStore::initialize() {
    char path[NBT_PATH_MAX];
    if (!root_.path(NBT_FS_PHONES_DIR, "manager", NULL, core::Span<char>(path, sizeof(path)))
             .ok()) {
        return false;
    }
    if (SafePathOpen::privateDir(root_.legacy(), path, false)) {
        if (errno != ENOENT || (mkdir(path, 0700) && errno != EEXIST) ||
            SafePathOpen::privateDir(root_.legacy(), path, false)) {
            return false;
        }
    }
    return sync();
}
bool PhoneStore::sync() {
    core::Result<core::UniqueFd> fd = root_.open(NBT_FS_SYNC, "manager", NULL, NBT_FS_OPEN_LOCK);
    if (!fd.ok() || fsync(fd.value().get())) {
        return false;
    }
#ifndef __QNX__
    const PathKind dirs[] = {NBT_FS_PHONES_DIR, NBT_FS_SETUP_TRUST_DIR, NBT_FS_STATE_DIR};
    if (SafePathOpen::syncDirKinds(root_.legacy(), "manager", NULL, dirs, 3)) {
        return false;
    }
#endif
    return true;
}
bool PhoneStore::read(const char *id, Phone &phone) {
    core::Result<core::UniqueFd> fd = root_.open(NBT_FS_PHONE, "manager", id, NBT_FS_READ);
    char text[320];
    if (!fd.ok()) {
        return false;
    }
    size_t used = 0;
    for (;;) {
        const ssize_t n = ::read(fd.value().get(), text + used, sizeof(text) - 1 - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            return false;
        }
        if (!n) {
            break;
        }
        used += static_cast<size_t>(n);
        if (used == sizeof(text) - 1) {
            return false;
        }
    }
    if (memchr(text, 0, used)) {
        return false;
    }
    text[used] = 0;
    if (strncmp(text, "NBTPHONE1\n", 10)) {
        return false;
    }
    char *cursor = text + 10;
    char *name = nbt_field(&cursor, "name");
    char *key = nbt_field(&cursor, "key");
    char *paired = nbt_field(&cursor, "paired");
    char actual[65];
    if (!nameValid(name) || !key || nbt_hex_decode(key, phone.key, 65) || !paired || *cursor ||
        nbt_parse_u64(paired, &phone.paired, 0) || !phone.paired ||
        !crypto::EcPublicKey::valid(phone.key) || !crypto::EcPublicKey::id(phone.key, actual) ||
        strcmp(actual, id)) {
        return false;
    }
    strcpy(phone.name, name);
    strcpy(phone.id, id);
    return true;
}
int PhoneStore::list(Phone output[4]) {
    char path[NBT_PATH_MAX];
    if (!root_.path(NBT_FS_PHONES_DIR, "manager", NULL, core::Span<char>(path, sizeof(path)))
             .ok() ||
        SafePathOpen::privateDir(root_.legacy(), path, false)) {
        return -1;
    }
    DIR *dir = opendir(path);
    if (!dir) {
        return -1;
    }
    int count = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) {
                count = -1;
            }
            break;
        }
        const size_t n = strlen(entry->d_name);
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            (n == 74 && !strcmp(entry->d_name + 64, ".conf.next"))) {
            continue;
        }
        if (n != 69 || strcmp(entry->d_name + 64, ".conf") || count == 4) {
            count = -1;
            break;
        }
        char id[65];
        memcpy(id, entry->d_name, 64);
        id[64] = 0;
        if (!read(id, output[count])) {
            count = -1;
            break;
        }
        ++count;
    }
    if (closedir(dir)) {
        return -1;
    }
    return count;
}
bool PhoneStore::find(const char *id, Phone &output) {
    return read(id, output);
}
bool PhoneStore::replace(PathKind next, PathKind target, const char *id, const char *text) {
    if (nbt_fs_unlink(root_.legacy(), next, "manager", id) && errno != ENOENT) {
        return false;
    }
    core::Result<core::UniqueFd> fd = root_.open(next, "manager", id, NBT_FS_CREATE);
    if (!fd.ok()) {
        return false;
    }
    const size_t size = strlen(text);
    size_t used = 0;
    while (used < size) {
        const ssize_t n = write(fd.value().get(), text + used, size - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        used += static_cast<size_t>(n);
    }
    return !fsync(fd.value().get()) && fd.value().close().ok() &&
           !nbt_fs_rename(root_.legacy(), next, target, "manager", id) && sync();
}
bool PhoneStore::add(const Phone &phone) {
    core::UniqueFd lock(
        SafePathOpen::lockNonBlocking(root_.legacy(), NBT_FS_PHONES_LOCK, "manager"));
    Phone existing[4];
    char id[65], hex[131], text[320];
    const int count = lock.valid() ? list(existing) : -1;
    if (count < 0 || count >= 4 || !nameValid(phone.name) || !phone.paired ||
        !crypto::EcPublicKey::valid(phone.key) || !crypto::EcPublicKey::id(phone.key, id) ||
        strcmp(id, phone.id) || nbt_hex(phone.key, 65, hex, sizeof(hex))) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        if (!strcmp(existing[i].id, id)) {
            return false;
        }
    }
    const int n = snprintf(text, sizeof(text), "NBTPHONE1\nname=%s\nkey=%s\npaired=%llu\n",
                           phone.name, hex, static_cast<unsigned long long>(phone.paired));
    return n > 0 && static_cast<size_t>(n) < sizeof(text) &&
           replace(NBT_FS_PHONE_NEXT, NBT_FS_PHONE, id, text);
}
bool PhoneStore::remove(const char *id) {
    core::UniqueFd lock(
        SafePathOpen::lockNonBlocking(root_.legacy(), NBT_FS_PHONES_LOCK, "manager"));
    return lock.valid() && !nbt_fs_unlink(root_.legacy(), NBT_FS_PHONE, "manager", id) && sync();
}
bool PhoneStore::openEnabled() {
    core::Result<core::UniqueFd> fd = root_.open(NBT_FS_PHONE_OPEN, "manager", NULL, NBT_FS_READ);
    char text[4] = {};
    return fd.ok() && ::read(fd.value().get(), text, sizeof(text)) == 3 && !memcmp(text, "on\n", 3);
}
bool PhoneStore::setOpenEnabled(bool enabled) {
    core::UniqueFd lock(
        SafePathOpen::lockNonBlocking(root_.legacy(), NBT_FS_PHONES_LOCK, "manager"));
    return lock.valid() &&
           replace(NBT_FS_PHONE_OPEN_NEXT, NBT_FS_PHONE_OPEN, NULL, enabled ? "on\n" : "off\n");
}
} // namespace storage
} // namespace nbt
