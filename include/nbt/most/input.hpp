// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_MOST_INPUT_HPP
#define NBT_MOST_INPUT_HPP

#include "nbt/core/mutex.hpp"
#include "nbt/core/unique_fd.hpp"
#include "nbt/most/ccs.hpp"
#include "nbt/most/input_trace.hpp"
#include "nbt/most/volume.hpp"

namespace nbt {
namespace most {

enum StopStatus { StopTimedOut = -1, StopIdle = 0, StopReleased = 1 };

class MostInput {
public:
    MostInput();

    int start(const char *library, const nbt_ccs_config *config);
    StopStatus stop(unsigned timeoutMs);
    bool backHeld();
    bool backKeyHeld();
    int volumeFd() const;
    bool latestVolume(int &percent, uint32_t &sequence);
    void logTrace(const char *why);

    static MostInput *shared();
    static MostInput *sharedOrCreate();
    void onAttached();
    void onDetached();
    void onData(uint32_t id, const unsigned char *data, uint32_t length);

private:
    void pushInputs(const nbt_input *inputs, size_t count);
    size_t controlKeys(uint16_t *keys) const;
    bool releasing();
    void logRegistry(const unsigned char *data, uint32_t length);

    Ccs ccs_;
    VolumeLevel volume_;
    InputTrace trace_;
    core::UniqueFd writer_;
    core::UniqueFd volumeWriter_;
    core::UniqueFd volumeReader_;
    core::Mutex releaseLock_;
    bool registered_;
    bool started_;
    bool releaseWanted_;
    bool releaseDone_;
    bool backHeld_;
    bool backKeyHeld_;
    bool volumeSeen_;
    int volumePct_;
    uint32_t volumeSeq_;
    uint32_t registeredClient_;
    uint32_t registryHash_;
    bool registryLogged_;
};

} // namespace most
} // namespace nbt

#endif
