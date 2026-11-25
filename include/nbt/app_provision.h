// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_APP_PROVISION_H
#define NBT_APP_PROVISION_H

/* Provisions a new application from the local operator's SSH session (the
 * launcher's --provision-app): reads the application's public key (key_size
 * bytes) and its first signed package (package_size bytes) from `input`,
 * verifies the package against that key, the application id and `compat`
 * and the expected SHA-256, and installs it as the baseline: executable,
 * installed record, settings declaration and profile, the profile last.
 * Later updates are verified against the apps key bundled with the manager
 * (nbt/apps_key.h), so the operator sends the apps public key. Refuses when
 * any of the application's paths exists, and removes what it created when it
 * fails. Requires the manager layout (initial setup) to exist. */

#include "nbt/fs.h"

#include <stdint.h>

int nbt_app_provision(const char *root, int input, const char *app, const char *compat,
                      uint64_t key_size, uint64_t package_size, const unsigned char digest[32],
                      char *error, unsigned error_size);

/* A new application from a phone session, as approved in the GUI: verifies
 * the staged package (`package_fd`) against the signer's key (nbt/signers.h:
 * "" for the apps key bundled with the manager, or a developer key), its own
 * compatibility id (which the profile then fixes, with the signer) and the
 * approved SHA-256, then installs it as above. Nothing of `app` may exist. */
int nbt_app_install_new(nbt_fs *fs, const char *app, int package_fd,
                        const unsigned char approved[32], const char *signer, char *error,
                        unsigned error_size);

#endif
