#ifndef NBT_LAUNCHER_SLOT_VERIFIER_HPP
#define NBT_LAUNCHER_SLOT_VERIFIER_HPP

#include "nbt/fs.h"
#include "nbt/manager_stage.h"

namespace nbt {
namespace launcher {

class SlotVerifier {
public:
    static int readProfileCompat(nbt_fs *fs, char out[49]);
    static int verifySlotWithKey(nbt_fs *fs, const nbt_manager_slot *record, int keyFd,
                                 const char *compat);
    static int preflightInitialSlot(nbt_fs *fs, const char *slot, const nbt_release *release,
                                    uint64_t size);
    static int clearSlot(nbt_fs *fs, const char *slot, int initial);
    static int checkBridgeDependencies(nbt_fs *fs, const nbt_bridge_provider *provider,
                                       int recovery, int locks[NBT_APP_LIST_MAX], char *warning,
                                       unsigned warningSize, char *error, unsigned errorSize);
};

} // namespace launcher
} // namespace nbt

#endif
