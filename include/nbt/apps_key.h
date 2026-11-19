// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APPS_KEY_H
#define NBT_APPS_KEY_H

/* The apps key: the one public key every application package is signed with,
 * new applications and updates alike. It is bundled with the manager:
 * compiled into its binaries (tools/embed_key.py), so the manager release's
 * signature covers it and a manager update can replace it. Returns a
 * seekable, read-only descriptor of its PEM (an unlinked temporary file) for
 * nbt_package_verify, or -1 with errno ENOENT when the build bundled none.
 * NBT_TESTING builds read the file NBT_TEST_APPS_KEY names instead. */
int nbt_apps_key_fd(void);

#endif
