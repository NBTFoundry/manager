// SPDX-License-Identifier: MPL-2.0
#include "nbt/signers.h"

#include "nbt/apps_key.h"
#include "nbt/core/unique_fd.hpp"
#include "nbt/model/installed_record.hpp"
#include "nbt/package/package_verifier.hpp"
#include "nbt/storage/developer_store.hpp"

#include <errno.h>
#include <string.h>

namespace {

bool developerId(const char *signer) {
    if (!signer || strlen(signer) != NBT_SIGNER_MAX) {
        return false;
    }
    for (size_t i = 0; i < NBT_SIGNER_MAX; ++i) {
        if (!((signer[i] >= '0' && signer[i] <= '9') || (signer[i] >= 'a' && signer[i] <= 'f'))) {
            return false;
        }
    }
    return true;
}

int developerKeyFd(const nbt::storage::Developer &developer) {
    nbt::core::Result<nbt::crypto::RsaPublicKey> key = nbt::crypto::RsaPublicKey::fromDer(
        nbt::core::Span<const uint8_t>(developer.der, developer.derSize));
    if (!key.ok()) {
        errno = EINVAL;
        return -1;
    }
    nbt::core::Result<nbt::core::UniqueFd> fd = key.value().pemFd();
    return fd.ok() ? fd.value().release() : -1;
}

} // namespace

int nbt_signer_key_fd(nbt_fs *fs, const char *signer) {
    if (!fs || !signer) {
        errno = EINVAL;
        return -1;
    }
    if (!signer[0]) {
        return nbt_apps_key_fd();
    }
    if (!strcmp(signer, NBT_SIGNER_MANAGER)) {
        return nbt_fs_open(fs, NBT_FS_MANAGER_KEY, "manager", NULL, NBT_FS_READ);
    }
    nbt::storage::ManagedRoot root(*fs);
    nbt::storage::DeveloperStore developers(root);
    nbt::storage::Developer developer;
    if (!developerId(signer) || !developers.find(signer, developer)) {
        errno = ENOENT;
        return -1;
    }
    return developerKeyFd(developer);
}

int nbt_signer_find(nbt_fs *fs, int package_fd, char signer[NBT_SIGNER_MAX + 1]) {
    if (!fs || package_fd < 0 || !signer) {
        errno = EINVAL;
        return -1;
    }
    nbt::core::UniqueFd apps(nbt_apps_key_fd());
    if (apps.valid() && nbt::package::PackageVerifier::signedBy(package_fd, apps.get())) {
        signer[0] = 0;
        return 0;
    }
    nbt::storage::ManagedRoot root(*fs);
    nbt::storage::DeveloperStore developers(root);
    nbt::storage::Developer list[nbt::storage::DeveloperStore::kMax];
    const int count = developers.list(list);
    for (int i = 0; i < count; ++i) {
        nbt::core::UniqueFd key(developerKeyFd(list[i]));
        if (key.valid() && nbt::package::PackageVerifier::signedBy(package_fd, key.get())) {
            memcpy(signer, list[i].id, NBT_SIGNER_MAX + 1);
            return 0;
        }
    }
    errno = EACCES;
    return -1;
}

int nbt_signer_name(nbt_fs *fs, const char *signer, char name[33]) {
    if (!fs || !signer || !name) {
        errno = EINVAL;
        return -1;
    }
    if (!signer[0] || !strcmp(signer, NBT_SIGNER_MANAGER)) {
        name[0] = 0;
        return 0;
    }
    nbt::storage::ManagedRoot root(*fs);
    nbt::storage::DeveloperStore developers(root);
    nbt::storage::Developer developer;
    if (!developerId(signer) || !developers.find(signer, developer)) {
        errno = ENOENT;
        return -1;
    }
    memcpy(name, developer.name, sizeof(developer.name));
    return 0;
}

int nbt_app_signer(nbt_fs *fs, const char *app, char signer[NBT_SIGNER_MAX + 1]) {
    char compat[NBT_ID_MAX + 1];
    return fs && app && signer ? nbt::model::InstalledRecord::profileRead(fs, app, compat, signer)
                               : -1;
}
