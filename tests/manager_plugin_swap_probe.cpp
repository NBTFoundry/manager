// SPDX-License-Identifier: MPL-2.0
#include "nbt/launcher/plugin_installer.hpp"
#include "nbt/manager_plugin.h"

#include <fcntl.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <unistd.h>

class SwappingVerifier : public nbt::launcher::ReleaseVerifier {
public:
    explicit SwappingVerifier(int alternate) : alternate_(alternate), calls_(0) {
    }

    nbt::core::Status verify(int releaseFd, int keyFd, const char *compat, nbt_release &release,
                             char *error, unsigned errorSize) override {
        if (++calls_ == 2) {
            releaseFd = alternate_;
        }
        if (nbt_release_verify(releaseFd, keyFd, compat, &release, error, errorSize)) {
            return nbt::core::Status::error(EINVAL, "release verification failed");
        }
        return nbt::core::Status();
    }

private:
    int alternate_;
    unsigned calls_;
};

int main(int argc, char **argv) {
    nbt_hmi_fs hmi;
    char error[160];
    ssize_t count;
    unsigned char digest[32], data[8192];
    SHA256_CTX sha;
    if (argc != 5 || nbt_hmi_init(&hmi, argv[1])) {
        return 2;
    }
    const int releaseFd = open(argv[2], O_RDONLY);
    const int alternate = open(argv[3], O_RDONLY);
    const int keyFd = open(argv[4], O_RDONLY);
    if (releaseFd < 0 || alternate < 0 || keyFd < 0) {
        return 2;
    }
    SHA256_Init(&sha);
    while ((count = read(releaseFd, data, sizeof(data))) > 0) {
        SHA256_Update(&sha, data, static_cast<size_t>(count));
    }
    if (count < 0) {
        return 2;
    }
    SHA256_Final(digest, &sha);
    SwappingVerifier verifier(alternate);
    nbt::launcher::PluginInstaller installer(verifier);
    const nbt::core::Status result =
        installer.install(hmi, releaseFd, keyFd, "demo-1", digest, error, sizeof(error));
    close(releaseFd);
    close(alternate);
    close(keyFd);
    return result.ok() ? 1 : 0;
}
