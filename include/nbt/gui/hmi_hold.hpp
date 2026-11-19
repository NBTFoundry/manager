// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_HMI_HOLD_HPP
#define NBT_GUI_HMI_HOLD_HPP

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

namespace nbt {
namespace gui {

static const char kHoldSignalPath[] = "/dev/shmem/nbt-manager-hmi";
static const char kHoldLivenessPath[] = "/dev/shmem/nbt-manager-gui-live";
static const unsigned kHoldPollMs = 250;
static const unsigned kHoldStartMs = 10000;
static const unsigned kHoldTries = 4;
static const unsigned kHoldLivenessMs = 1500;

enum HoldPhase { HoldIdle, HoldStarting, HoldOpen, HoldSteppedAside };

enum HoldState { HoldBlank, HoldParent };

enum HoldSignal { SignalAside = 1, SignalShow = 2 };

enum HoldReadResult { HoldReadError = -1, HoldReadOk = 0, HoldReadAbsent = 1 };

enum HoldAction {
    HoldSpawn = 1u,
    HoldFocus = 2u,
    HoldRelease = 4u,
    HoldAside = 8u,
    HoldShow = 16u,
    HoldPoll = 32u,
    HoldToParent = 64u // put iDrive on the parent state 15 first
};

class HmiHold {
public:
    HmiHold();
    void reset();
    unsigned entry(uint64_t nowMs, bool running);
    void focus(HoldState state, bool focused, uint64_t nowMs);
    unsigned poll(uint64_t nowMs, bool running);

    HoldPhase phase;
    uint64_t startedMs;
    uint64_t focusMs;
    uint64_t leftMs;
    uint64_t parentMs;
    unsigned tries;
    bool blank;
    bool parent;
    bool seen;
    bool shown; // HoldShow sent since the entry: the GUI may cover iDrive
};

int holdSignalParse(const char *text, size_t size, uint64_t &sequence, HoldSignal &signal);
HoldReadResult holdSignalRead(const char *path, uint64_t &sequence, HoldSignal &signal);
int holdSignalWrite(const char *path, uint64_t sequence, HoldSignal signal);
int holdLivenessWrite(const char *path, pid_t pid, uint64_t nowMs);
bool holdLivenessRead(const char *path, uint64_t nowMs);

} // namespace gui
} // namespace nbt

#endif
