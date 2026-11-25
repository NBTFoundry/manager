// SPDX-License-Identifier: MPL-2.0
#include "nbt/storage/developer_store.hpp"
#include "nbt/storage/path_open.hpp"
#include "nbt/storage/phone_store.hpp"
#include "nbt/util.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
namespace nbt {
namespace storage {
namespace {
const size_t kTextMax = 1400; // NBTDEV1, a name, 2 * kDerMax hex digits, a date
} // namespace
bool DeveloperStore::fromDer(const unsigned char *der, size_t size, Developer &output) {
    if (!der || !size || size > sizeof(output.der)) {
        return false;
    }
    core::Result<crypto::RsaPublicKey> key =
        crypto::RsaPublicKey::fromDer(core::Span<const uint8_t>(der, size));
    if (!key.ok() || !key.value().id(output.id).ok()) {
        return false;
    }
    memcpy(output.der, der, size);
    output.derSize = size;
    return true;
}
void DeveloperStore::fingerprint(const char *id, char output[20]) {
    snprintf(output, 20, "%.4s %.4s %.4s %.4s", id, id + 4, id + 8, id + 12);
}
bool DeveloperStore::initialize() {
    char path[NBT_PATH_MAX];
    if (!root_.path(NBT_FS_DEVELOPERS_DIR, "manager", NULL, core::Span<char>(path, sizeof(path)))
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
bool DeveloperStore::sync() {
    core::Result<core::UniqueFd> fd = root_.open(NBT_FS_SYNC, "manager", NULL, NBT_FS_OPEN_LOCK);
    if (!fd.ok() || fsync(fd.value().get())) {
        return false;
    }
#ifndef __QNX__
    const PathKind dirs[] = {NBT_FS_DEVELOPERS_DIR, NBT_FS_SETUP_TRUST_DIR, NBT_FS_STATE_DIR};
    if (SafePathOpen::syncDirKinds(root_.legacy(), "manager", NULL, dirs, 3)) {
        return false;
    }
#endif
    return true;
}
bool DeveloperStore::read(const char *id, Developer &developer) {
    core::Result<core::UniqueFd> fd = root_.open(NBT_FS_DEVELOPER, "manager", id, NBT_FS_READ);
    char text[kTextMax];
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
    if (strncmp(text, "NBTDEV1\n", 8)) {
        return false;
    }
    char *cursor = text + 8;
    char *name = nbt_field(&cursor, "name");
    char *key = nbt_field(&cursor, "key");
    char *added = nbt_field(&cursor, "added");
    unsigned char der[crypto::RsaPublicKey::kDerMax];
    const size_t hexSize = key ? strlen(key) : 0;
    if (!PhoneStore::nameValid(name) || !hexSize || hexSize % 2 || hexSize / 2 > sizeof(der) ||
        nbt_hex_decode(key, der, hexSize / 2) || !added || *cursor ||
        nbt_parse_u64(added, &developer.added, 0) || !developer.added ||
        !fromDer(der, hexSize / 2, developer) || strcmp(developer.id, id)) {
        return false;
    }
    strcpy(developer.name, name);
    return true;
}
int DeveloperStore::list(Developer output[kMax]) {
    char path[NBT_PATH_MAX];
    if (!root_.path(NBT_FS_DEVELOPERS_DIR, "manager", NULL, core::Span<char>(path, sizeof(path)))
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
        if (n != 69 || strcmp(entry->d_name + 64, ".conf") || count == kMax) {
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
bool DeveloperStore::find(const char *id, Developer &output) {
    unsigned char bytes[32];
    return id && strlen(id) == 64 && !nbt_hex_decode(id, bytes, 32) && read(id, output);
}
bool DeveloperStore::add(const Developer &developer) {
    core::UniqueFd lock(
        SafePathOpen::lockNonBlocking(root_.legacy(), NBT_FS_DEVELOPERS_LOCK, "manager"));
    Developer existing[kMax], checked;
    char hex[2 * crypto::RsaPublicKey::kDerMax + 1], text[kTextMax];
    const int count = lock.valid() ? list(existing) : -1;
    if (count < 0 || count >= kMax || !PhoneStore::nameValid(developer.name) || !developer.added ||
        !fromDer(developer.der, developer.derSize, checked) || strcmp(checked.id, developer.id) ||
        nbt_hex(developer.der, developer.derSize, hex, sizeof(hex))) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        if (!strcmp(existing[i].id, developer.id)) {
            return false;
        }
    }
    const int n = snprintf(text, sizeof(text), "NBTDEV1\nname=%s\nkey=%s\nadded=%llu\n",
                           developer.name, hex, static_cast<unsigned long long>(developer.added));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(text) ||
        (nbt_fs_unlink(root_.legacy(), NBT_FS_DEVELOPER_NEXT, "manager", developer.id) &&
         errno != ENOENT)) {
        return false;
    }
    core::Result<core::UniqueFd> fd =
        root_.open(NBT_FS_DEVELOPER_NEXT, "manager", developer.id, NBT_FS_CREATE);
    if (!fd.ok()) {
        return false;
    }
    const size_t size = static_cast<size_t>(n);
    size_t used = 0;
    while (used < size) {
        const ssize_t w = write(fd.value().get(), text + used, size - used);
        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w <= 0) {
            return false;
        }
        used += static_cast<size_t>(w);
    }
    return !fsync(fd.value().get()) && fd.value().close().ok() &&
           !nbt_fs_rename(root_.legacy(), NBT_FS_DEVELOPER_NEXT, NBT_FS_DEVELOPER, "manager",
                          developer.id) &&
           sync();
}
bool DeveloperStore::remove(const char *id) {
    core::UniqueFd lock(
        SafePathOpen::lockNonBlocking(root_.legacy(), NBT_FS_DEVELOPERS_LOCK, "manager"));
    Developer existing;
    return lock.valid() && find(id, existing) &&
           !nbt_fs_unlink(root_.legacy(), NBT_FS_DEVELOPER, "manager", id) && sync();
}
} // namespace storage
} // namespace nbt
