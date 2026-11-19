// SPDX-License-Identifier: MPL-2.0
/* Host probe for starter.cfg parsing and transactional capture install/remove. */
#include "../src/storage/capture_probe.hpp"
#include "nbt/capture/diag.h"
#include "nbt/capture/protocol.h"
#include "nbt/core/unique_fd.hpp"
#include "nbt/fs.h"
#include "nbt/release.h"
#include "nbt/storage/capture_install.hpp"
#include "nbt/storage/starter_config.hpp"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>

namespace {

int remount(int writable) {
    printf("remount %s\n", writable ? "rw" : "ro");
    fflush(stdout);
    const char *fail = getenv("NBT_TEST_REMOUNT_FAIL");
    return fail && !strcmp(fail, writable ? "rw" : "ro") ? -1 : 0;
}

bool readFile(const char *path, std::vector<char> &out) {
    nbt::core::UniqueFd fd(open(path, O_RDONLY));
    if (!fd.valid()) {
        return false;
    }
    char buf[8192];
    for (;;) {
        const ssize_t n = read(fd.get(), buf, sizeof(buf));
        if (n < 0) {
            return false;
        }
        if (n == 0) {
            break;
        }
        out.insert(out.end(), buf, buf + n);
    }
    return true;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        return 2;
    }
    if (!strcmp(argv[1], "inspect") && argc == 3) {
        std::vector<char> data;
        if (!readFile(argv[2], data)) {
            return 1;
        }
        const nbt::storage::StarterConfigState st =
            nbt::storage::StarterConfig::inspect(data.empty() ? "" : &data[0], data.size(), NULL);
        printf("%s\n", st == nbt::storage::StarterStock        ? "stock"
                       : st == nbt::storage::StarterWithShim   ? "with-shim"
                       : st == nbt::storage::StarterLegacyShim ? "legacy"
                                                               : "invalid");
        return st == nbt::storage::StarterInvalid ? 1 : 0;
    }
    if (!strcmp(argv[1], "status") && argc == 3) {
        char buf[64];
        const int op = nbt_capture_operational_status(argv[2], buf, sizeof(buf));
        printf("op=%d status=%s\n", op, buf);
        return 0;
    }
    if (!strcmp(argv[1], "write-diag") && argc == 5) {
        nbt_capture_diag_shm shm;
        memset(&shm, 0, sizeof(shm));
        shm.magic = NBT_CAPTURE_DIAG_MAGIC;
        shm.version = NBT_CAPTURE_DIAG_VERSION;
        shm.observer_mode = static_cast<uint8_t>(atoi(argv[3]) ? 1 : 0);
        shm.pid = static_cast<int32_t>(atoi(argv[4]));
        nbt::core::UniqueFd fd(open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0600));
        if (!fd.valid()) {
            return 1;
        }
        return write(fd.get(), &shm, sizeof(shm)) == static_cast<ssize_t>(sizeof(shm)) ? 0 : 1;
    }
    nbt_fs fs;
    char error[160];
    if (!strcmp(argv[1], "install") && argc == 8) {
        if (nbt_fs_init(&fs, argv[2], error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        nbt::core::UniqueFd rfd(open(argv[5], O_RDONLY));
        nbt::core::UniqueFd kfd(open(argv[6], O_RDONLY));
        nbt_release rel;
        if (!rfd.valid() || !kfd.valid() ||
            nbt_release_verify(rfd.get(), kfd.get(), argv[7], &rel, error, sizeof(error))) {
            fprintf(stderr, "%s\n", *error ? error : "verify failed");
            return 1;
        }
        int changed = 0;
        if (nbt_capture_refresh_from_release(&fs, argv[3], argv[4], rfd.get(), &rel, remount,
                                             &changed, error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        printf("changed=%d\n", changed);
        return 0;
    }
    if (!strcmp(argv[1], "load-test") && (argc == 4 || argc == 5)) {
        // load-test <root> <library> [starter.cfg]
        std::vector<char> data, cfg;
        if (nbt_fs_init(&fs, argv[2], error, sizeof(error)) || !readFile(argv[3], data) ||
            data.empty() || (argc == 5 && (!readFile(argv[4], cfg) || cfg.empty()))) {
            return 1;
        }
        char detail[224];
        if (nbt::storage::capture_io::probeCaptureShim(&fs, cfg.empty() ? NULL : &cfg[0],
                                                       cfg.size(), &data[0], data.size(), detail,
                                                       sizeof(detail))) {
            fprintf(stderr, "%s\n", detail);
            return 1;
        }
        printf("loaded: %s\n", detail);
        return 0;
    }
    if (!strcmp(argv[1], "remove") && argc == 5) {
        if (nbt_fs_init(&fs, argv[2], error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        if (nbt_capture_remove_installed(&fs, argv[3], argv[4], remount, error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        puts("removed");
        return 0;
    }
    return 2;
}
