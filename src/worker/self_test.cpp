// SPDX-License-Identifier: MPL-2.0
/* The self test's preparation (docs/self-test.md), on the worker's job: the
 * running slot's release, verified again against the manager key, carries
 * the bundled applications as named components; each is copied out,
 * verified as a package against the manager key and installed with signer
 * "manager", unless the same one is installed already. */
#include "nbt/worker/self_test.hpp"

#include "nbt/app_faults.h"
#include "nbt/app_provision.h"
#include "nbt/app_remove.h"
#include "nbt/bundled.h"
#include "nbt/core/unique_fd.hpp"
#include "nbt/launcher/slot_verifier.hpp"
#include "nbt/log.h"
#include "nbt/manager_store.h"
#include "nbt/package.h"
#include "nbt/package/package_verifier.hpp"
#include "nbt/process/audio_mixer.hpp"
#include "nbt/release.h"
#include "nbt/signers.h"
#include "nbt/transaction.h"
#include "nbt/util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace nbt {
namespace worker {
namespace {

using core::UniqueFd;

int refuse(SelfTestPrepared &out, const char *reason) {
    snprintf(out.reason, sizeof(out.reason), "%s", reason);
    nbt_logf("self test: %s", reason);
    return -1;
}

/* The running slot's release, verified; its descriptor stays open in `fd`. */
int openRelease(nbt_fs *fs, UniqueFd &fd, nbt_release &release, SelfTestPrepared &out) {
    nbt_manager_state state;
    char compat[49], slot[2];
    if (nbt_manager_store_inspect(fs, &state) || state.active.slot > 1) {
        return refuse(out, "no manager state");
    }
    if (launcher::SlotVerifier::readProfileCompat(fs, compat)) {
        return refuse(out, "invalid manager profile");
    }
    snprintf(slot, sizeof(slot), "%u", state.active.slot);
    fd.reset(nbt_fs_open(fs, NBT_FS_MANAGER_SLOT_RELEASE, "manager", slot, NBT_FS_READ));
    UniqueFd key(nbt_fs_open(fs, NBT_FS_MANAGER_KEY, "manager", NULL, NBT_FS_READ));
    if (!fd.valid() || !key.valid()) {
        return refuse(out, "release unreadable");
    }
    char error[128] = "";
    if (nbt_release_verify(fd.get(), key.get(), compat, &release, error, sizeof(error))) {
        nbt_logf("self test: release: %s", error);
        return refuse(out, "release not verified");
    }
    if (!nbt_constant_equal(release.sha256, state.active.release_hash, 32)) {
        return refuse(out, "release is not the active one");
    }
    snprintf(out.version, sizeof(out.version), "%s", release.version);
    return 0;
}

/* The component as a package in state/manager/bundled.nbt, verified against
 * the manager key: its descriptor, or -1. */
int copyPackage(nbt_fs *fs, int releaseFd, const nbt_release &release, unsigned index,
                const char *app, nbt_package &package) {
    char error[128] = "";
    if (nbt_fs_unlink(fs, NBT_FS_MANAGER_BUNDLED, "manager", NULL) && errno != ENOENT) {
        return -1;
    }
    {
        UniqueFd out(nbt_fs_open(fs, NBT_FS_MANAGER_BUNDLED, "manager", NULL, NBT_FS_CREATE));
        if (!out.valid() || nbt_release_copy_component(releaseFd, &release, index, out.get(), error,
                                                       sizeof(error))) {
            nbt_logf("self test: %s: copy: %s", app, error);
            return -1;
        }
    }
    UniqueFd in(nbt_fs_open(fs, NBT_FS_MANAGER_BUNDLED, "manager", NULL, NBT_FS_READ));
    UniqueFd key(nbt_signer_key_fd(fs, NBT_SIGNER_MANAGER));
    if (!in.valid() || !key.valid()) {
        return -1;
    }
    // Its own compatibility id, as for any first install (nbt_app_install_new).
    core::Result<nbt_package> named = package::PackageVerifier::peekApp(in.get());
    if (!named.get() || nbt_package_verify(in.get(), key.get(), app, named.value().compatibility,
                                           &package, error, sizeof(error))) {
        nbt_logf("self test: %s: package: %s", app, error);
        return -1;
    }
    return in.release();
}

/* Installs bundled application `i`, or keeps the same one. */
bool install(nbt_fs *fs, int releaseFd, const nbt_release &release, unsigned index,
             const char *app) {
    nbt_package package;
    UniqueFd fd(copyPackage(fs, releaseFd, release, index, app, package));
    if (!fd.valid()) {
        return false;
    }
    char hash[65];
    nbt_installed record;
    const int present = nbt_fs_exists(fs, NBT_FS_PROFILE, app, NULL);
    if (present == 1 && !nbt_tx_installed(fs, app, &record) &&
        !nbt_hex(package.payload_hash, 32, hash, sizeof(hash)) && !strcmp(record.sha256, hash) &&
        record.sequence == package.sequence) {
        return true; // the same one
    }
    if (present != 0 || nbt_fs_app_present(fs, app) > 0) {
        const int removed = nbt_app_remove(fs, app);
        if (removed != NBT_APP_REMOVED && removed != NBT_APP_REMOVE_MISSING) {
            nbt_logf("self test: %s: cannot replace it (%d, errno %d)", app, removed, errno);
            return false;
        }
    }
    char error[160] = "";
    if (nbt_app_install_new(fs, app, fd.get(), package.package_hash, NBT_SIGNER_MANAGER, error,
                            sizeof(error))) {
        nbt_logf("self test: %s: install: %s", app, error);
        return false;
    }
    nbt_logf("self test: %s %s installed", app, package.version);
    return true;
}

} // namespace

int selfTestPrepare(nbt_fs *fs, SelfTestPrepared &out) {
    memset(&out, 0, sizeof(out));
    UniqueFd fd;
    nbt_release release;
    if (openRelease(fs, fd, release, out)) {
        return -1;
    }
    for (unsigned i = 0; i < NBT_BUNDLED_COUNT; ++i) {
        const nbt_bundled_app &bundled = nbt_bundled_apps[i];
        const int index = nbt_release_find(&release, bundled.component);
        if (index < 0) {
            out.absent |= 1u << i;
            continue;
        }
        if (!install(fs, fd.get(), release, static_cast<unsigned>(index), bundled.app) ||
            nbt_app_fault_clear(fs, bundled.app) ||
            (nbt_fs_unlink(fs, NBT_FS_DATA_SELFTEST, bundled.app, NULL) && errno != ENOENT)) {
            out.failed |= 1u << i;
            continue;
        }
        out.ready |= 1u << i;
    }
    if (nbt_fs_unlink(fs, NBT_FS_MANAGER_BUNDLED, "manager", NULL) && errno != ENOENT) {
        nbt_logf("self test: cannot remove the copied package (errno %d)", errno);
    }
    process::MixerState audio;
    if (process::mixerState(&audio)) { // counted on from here by the report
        out.audioGaps = audio.gaps;
        out.audioGrown = audio.grown;
    }
    return 0;
}

int selfTestReadyReply(const SelfTestPrepared &prepared, char *reply, size_t size) {
    int used = snprintf(reply, size, "SELFTEST\tready\t");
    for (unsigned i = 0; i < NBT_BUNDLED_COUNT && used > 0 && static_cast<size_t>(used) < size;
         ++i) {
        if (prepared.ready & (1u << i)) {
            used += snprintf(reply + used, size - static_cast<size_t>(used), "%s%s",
                             reply[used - 1] == '\t' ? "" : ",", nbt_bundled_apps[i].app);
        }
    }
    if (used < 0 || static_cast<size_t>(used) + 2 > size) {
        return -1;
    }
    reply[used] = '\n';
    reply[used + 1] = 0;
    return 0;
}

} // namespace worker
} // namespace nbt
