// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_SIGNERS_H
#define NBT_SIGNERS_H

/* Who signs an application's packages: the apps key bundled with the manager
 * (nbt/apps_key.h), signer "", or a developer key a paired phone added and the
 * driver approved (nbt/storage/developer_store.hpp), signer its 64-digit key
 * id. An application keeps the signer of its first package: its profile names
 * a developer key ("signer=" after "compat="), and only that key's packages
 * update it. A removed developer key leaves its applications installed and
 * running, but nothing updates them until the key is added again. */

#include "nbt/fs.h"

#define NBT_SIGNER_MAX 64u
/* The manager key, signer of the manager's own applications only
 * (nbt/bundled.h): never found by nbt_signer_find. */
#define NBT_SIGNER_MANAGER "manager"

/* A seekable PEM descriptor of the signer's key for nbt_package_verify, or -1
 * with errno ENOENT when the manager bundles no apps key or the developer key
 * is no longer on the car. */
int nbt_signer_key_fd(nbt_fs *fs, const char *signer);
/* The trusted key that signed the package, the apps key tried first: 0 with
 * `signer` set, or -1 when none did. Only the signature over the header and
 * manifest is checked; nbt_package_verify checks the rest. */
int nbt_signer_find(nbt_fs *fs, int package_fd, char signer[NBT_SIGNER_MAX + 1]);
/* The developer key's name, "" for the apps key; -1 when the key is unknown. */
int nbt_signer_name(nbt_fs *fs, const char *signer, char name[33]);
/* The signer the application's profile names ("" for the apps key), or -1
 * when the profile is missing or invalid. */
int nbt_app_signer(nbt_fs *fs, const char *app, char signer[NBT_SIGNER_MAX + 1]);

#endif
