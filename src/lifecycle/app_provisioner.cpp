#include "nbt/lifecycle/app_provisioner.hpp"

#include "nbt/app_provision.h"
#include "nbt/app_settings.h"
#include "nbt/apps_key.h"
#include "nbt/core/unique_fd.hpp"
#include "nbt/fs.h"
#include "nbt/manager_bootstrap.h"
#include "nbt/manager_stage.h"
#include "nbt/package.h"
#include "nbt/package/package_verifier.hpp"
#include "nbt/transaction.h"
#include "nbt/util.h"

#include <errno.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace lifecycle {
namespace {

const uint64_t kKeyMax = 16384u;
const uint64_t kPackageMax =
    (uint64_t)NBT_PAYLOAD_MAX + NBT_MANIFEST_MAX + NBT_SIGNATURE_SIZE + 24u;

int fail(char *error, unsigned size, const char *message) {
    if (error && size) {
        snprintf(error, size, "%s", message);
    }
    return -1;
}

int writeAll(int fd, const void *data, size_t n) {
    const unsigned char *p = static_cast<const unsigned char *>(data);
    while (n) {
        const ssize_t written = write(fd, p, n);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return -1;
        }
        p += written;
        n -= (size_t)written;
    }
    return 0;
}

int createText(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, const char *tx, const void *text,
               size_t size) {
    const int fd = nbt_fs_provision_create(fs, kind, app, tx);
    if (fd < 0) {
        return -1;
    }
    int result = writeAll(fd, text, size) || fsync(fd);
    if (close(fd)) {
        result = -1;
    }
    return result ? -1 : 0;
}

int createCopy(nbt_fs *fs, nbt_fs_path_kind kind, const char *app, const char *tx, int in,
               off_t offset, uint64_t size, const unsigned char *expected) {
    unsigned char buffer[8192];
    unsigned char digest[32];
    SHA256_CTX sha;
    uint64_t done = 0;
    int result = -1;
    if (lseek(in, offset, SEEK_SET) != offset) {
        return -1;
    }
    const int out = nbt_fs_provision_create(fs, kind, app, tx);
    if (out < 0) {
        return -1;
    }
    SHA256_Init(&sha);
    while (done < size) {
        const size_t want = size - done < sizeof(buffer) ? (size_t)(size - done) : sizeof(buffer);
        const ssize_t n = read(in, buffer, want);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0 || writeAll(out, buffer, (size_t)n)) {
            goto done;
        }
        SHA256_Update(&sha, buffer, (size_t)n);
        done += (uint64_t)n;
    }
    SHA256_Final(digest, &sha);
    if ((!expected || nbt_constant_equal(digest, expected, 32)) && !fsync(out)) {
        result = 0;
    }
done:
    if (close(out)) {
        result = -1;
    }
    return result;
}

/* Installs a verified package as a new application's baseline: executable,
 * installed record and settings declaration, then the profile, which makes
 * it an application. Nothing of it may exist; what it created goes again on
 * failure. Holds the manager operation lock (or its launcher's) throughout. */
int installBaseline(nbt_fs *fs, const char *app, const char *compat, int pkg,
                    const nbt_package &package, char *error, unsigned errorSize) {
    nbt_installed record;
    nbt_bridge_provider provider;
    char text[512 + NBT_BRIDGE_TEXT_MAX];
    char hash[65];
    int lock = -1;
    int created = 0;
    int result = -1;
    int n = 0;
    const int operation = nbt_tx_manager_lock(fs);
    if (operation < 0) {
        return fail(error, errorSize,
                    errno == EBUSY ? "manager update awaiting confirmation"
                                   : "manager operation is in progress");
    }
    memset(&provider, 0, sizeof(provider));
    if (package.bridge.major && nbt_manager_active_bridge(fs, &provider)) {
        fail(error, errorSize, "cannot verify active manager bridge");
        goto done;
    }
    if (nbt_bridge_check(&package.bridge, &provider, NULL, NULL, error, errorSize)) {
        goto done;
    }
    if (nbt_fs_provision_dirs(fs, app)) {
        fail(error, errorSize, "cannot create application directories");
        goto done;
    }
    created = 1;
    lock = nbt_tx_lock(fs, app);
    if (lock < 0 || nbt_hex(package.payload_hash, 32, hash, sizeof(hash))) {
        fail(error, errorSize, "cannot lock application");
        goto done;
    }
    memset(&record, 0, sizeof(record));
    record.sequence = record.highwater = (unsigned long long)package.sequence;
    memcpy(record.version, package.version, strlen(package.version) + 1);
    memcpy(record.sha256, hash, sizeof(hash));
    record.bridge = package.bridge;
    n = nbt_tx_record_format(&record, text, sizeof(text));
    if (n < 0 || (size_t)n >= sizeof(text) ||
        createCopy(fs, NBT_FS_ACTIVE, app, NULL, pkg, package.payload_offset, package.payload_size,
                   package.payload_hash) ||
        createText(fs, NBT_FS_RECORD, app, NULL, text, (size_t)n)) {
        fail(error, errorSize, "cannot write application files");
        goto done;
    }
    if (nbt_app_settings_save_schema(fs, app, &package.settings)) {
        fail(error, errorSize, "cannot write application settings");
        goto done;
    }
    n = snprintf(text, sizeof(text), "NBTAPP1\ncompat=%s\n", compat);
    if (n < 0 || (size_t)n >= sizeof(text) ||
        createText(fs, NBT_FS_PROFILE, app, NULL, text, (size_t)n) ||
        nbt_fs_provision_sync(fs, app)) {
        fail(error, errorSize, "cannot complete application provisioning");
        goto done;
    }
    result = 0;
done:
    if (lock >= 0) {
        close(lock);
    }
    if (result && created && nbt_fs_provision_discard(fs, app)) {
        fail(error, errorSize, "provisioning failed and cleanup was incomplete");
    }
    close(operation);
    nbt_clear(text, sizeof(text));
    return result;
}

int absent(nbt_fs *fs, const char *app, char *error, unsigned errorSize) {
    const int exists = nbt_fs_app_present(fs, app);
    return exists ? fail(error, errorSize,
                         exists > 0 ? "application already provisioned"
                                    : "manager layout missing; install the manager first")
                  : 0;
}

int provisionApp(const char *root, int input, const char *app, const char *compat, uint64_t keySize,
                 uint64_t packageSize, const unsigned char digest[32], char *error,
                 unsigned errorSize) {
    nbt_fs fs;
    nbt_package package;
    char text[160];
    int result = -1;
    if (!root || !digest || !nbt_token_valid(app, NBT_ID_MAX, 0) || !strcmp(app, "manager") ||
        !nbt_token_valid(compat, NBT_ID_MAX, NBT_TOKEN_DOT) || !keySize || keySize > kKeyMax ||
        !packageSize || packageSize > kPackageMax) {
        return fail(error, errorSize, "invalid provisioning request");
    }
    if (nbt_fs_init(&fs, root, error, errorSize) || absent(&fs, app, error, errorSize)) {
        return -1;
    }
    FILE *key = nbt_fs_bootstrap_temp();
    FILE *pkg = nbt_fs_bootstrap_temp();
    if (!key || !pkg ||
        nbt_manager_receive_pair(input, fileno(key), keySize, fileno(pkg), packageSize)) {
        fail(error, errorSize, "provisioning transfer failed");
    } else if (nbt_package_verify(fileno(pkg), fileno(key), app, compat, &package, text,
                                  sizeof(text)) ||
               !nbt_constant_equal(package.package_hash, digest, 32)) {
        fail(error, errorSize, "package signature, application, compatibility or digest rejected");
    } else {
        result = installBaseline(&fs, app, compat, fileno(pkg), package, error, errorSize);
    }
    if (key) {
        fclose(key);
    }
    if (pkg) {
        fclose(pkg);
    }
    return result;
}

} // namespace

core::Status AppProvisioner::provision(const char *root, int input, const char *app,
                                       const char *compat, uint64_t keySize, uint64_t packageSize,
                                       const unsigned char digest[32], core::Span<char> errorOut) {
    char localError[256];
    char *target = errorOut.data() ? errorOut.data() : localError;
    const unsigned targetSize =
        errorOut.data() ? static_cast<unsigned>(errorOut.size()) : sizeof(localError);
    if (provisionApp(root, input, app, compat, keySize, packageSize, digest, target, targetSize)) {
        return core::Status::error(errno ? errno : EIO, "application provisioning failed");
    }
    return core::Status();
}

} // namespace lifecycle
} // namespace nbt

int nbt_app_provision(const char *root, int input, const char *app, const char *compat,
                      uint64_t key_size, uint64_t package_size, const unsigned char digest[32],
                      char *error, unsigned error_size) {
    return nbt::lifecycle::AppProvisioner::provision(
               root, input, app, compat, key_size, package_size, digest,
               nbt::core::Span<char>(error, error ? error_size : 0))
                   .ok()
               ? 0
               : -1;
}

int nbt_app_install_new(nbt_fs *fs, const char *app, int package_fd,
                        const unsigned char approved[32], char *error, unsigned error_size) {
    nbt_package package;
    char text[160];
    if (!fs || !approved || !nbt_token_valid(app, NBT_ID_MAX, 0) || !strcmp(app, "manager")) {
        return nbt::lifecycle::fail(error, error_size, "invalid installation request");
    }
    nbt::core::Result<nbt_package> named = nbt::package::PackageVerifier::peekApp(package_fd);
    if (!named.get() || strcmp(named.value().app, app)) {
        return nbt::lifecycle::fail(error, error_size, "package names another application");
    }
    if (nbt::lifecycle::absent(fs, app, error, error_size)) {
        return -1;
    }
    nbt::core::UniqueFd key(nbt_apps_key_fd());
    if (!key.valid()) {
        return nbt::lifecycle::fail(error, error_size, "no apps key in this manager");
    }
    // The application's compatibility id is its first package's.
    if (nbt_package_verify(package_fd, key.get(), app, named.value().compatibility, &package, text,
                           sizeof(text)) ||
        !nbt_constant_equal(package.package_hash, approved, 32)) {
        return nbt::lifecycle::fail(error, error_size, "approved package changed or not signed");
    }
    return nbt::lifecycle::installBaseline(fs, app, package.compatibility, package_fd, package,
                                           error, error_size);
}
