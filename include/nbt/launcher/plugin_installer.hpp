#ifndef NBT_LAUNCHER_PLUGIN_INSTALLER_HPP
#define NBT_LAUNCHER_PLUGIN_INSTALLER_HPP

#include "nbt/core/status.hpp"
#include "nbt/fs.h"
#include "nbt/release.h"

namespace nbt {
namespace launcher {

class ReleaseVerifier {
public:
    virtual ~ReleaseVerifier() {
    }
    virtual core::Status verify(int releaseFd, int keyFd, const char *compat, nbt_release &release,
                                char *error, unsigned errorSize) = 0;
};

class RealReleaseVerifier : public ReleaseVerifier {
public:
    core::Status verify(int releaseFd, int keyFd, const char *compat, nbt_release &release,
                        char *error, unsigned errorSize) override;
};

class PluginInstaller {
public:
    explicit PluginInstaller(ReleaseVerifier &verifier) : verifier_(verifier) {
    }

    core::Status preflight(nbt_hmi_fs &hmi, int releaseFd, int keyFd, const char *compat,
                           const unsigned char digest[32], char *error, unsigned errorSize);
    core::Status install(nbt_hmi_fs &hmi, int releaseFd, int keyFd, const char *compat,
                         const unsigned char digest[32], char *error, unsigned errorSize);
    /* Brings the live plugin to this release: nothing when it matches, a
     * folder swap when it differs, a first registration when it is missing. */
    core::Status refresh(nbt_hmi_fs &hmi, int releaseFd, int keyFd, const char *compat,
                         const unsigned char digest[32], bool &changed, char *error,
                         unsigned errorSize);

private:
    core::Status writeTemporary(nbt_hmi_fs &hmi, int releaseFd, const nbt_release &release,
                                char *error, unsigned errorSize);

    ReleaseVerifier &verifier_;
};

} // namespace launcher
} // namespace nbt

#endif
