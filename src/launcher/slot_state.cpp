#include "nbt/launcher/slot_state.hpp"

#include "nbt/util.h"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace launcher {
namespace {

bool hashNonzero(const unsigned char hash[32]) {
    unsigned char value = 0;
    for (unsigned i = 0; i < 32; ++i) {
        value |= hash[i];
    }
    return value != 0;
}

bool valid(const nbt_manager_state &state) {
    if (state.pending > 2 || state.active.slot > 1 || !state.active.sequence || !state.highwater ||
        !hashNonzero(state.active.release_hash) || state.restarts > 3 ||
        (!state.restarts) != (!state.restart_epoch)) {
        return false;
    }
    if (!state.pending) {
        return state.active.sequence <= state.highwater && state.previous.slot == 2 &&
               !state.previous.sequence && !hashNonzero(state.previous.release_hash);
    }
    return state.previous.slot <= 1 && state.previous.slot != state.active.slot &&
           state.previous.sequence && state.previous.sequence <= state.highwater &&
           hashNonzero(state.previous.release_hash) &&
           (state.active.sequence > state.highwater ||
            state.active.sequence < state.previous.sequence);
}

void clearPrevious(nbt_manager_state &state) {
    memset(&state.previous, 0, sizeof(state.previous));
    state.previous.slot = 2;
}

int readNumber(char **cursor, const char *name, uint64_t *value) {
    const char *text = nbt_field(cursor, name);
    return !text || strlen(text) != 20 || nbt_parse_u64(text, value, 1) ? -1 : 0;
}

int readSlot(char **cursor, const char *name, unsigned *slot) {
    const char *text = nbt_field(cursor, name);
    if (!text || text[1] || (text[0] != '0' && text[0] != '1' && text[0] != '-')) {
        return -1;
    }
    *slot = text[0] == '-' ? 2u : static_cast<unsigned>(text[0] - '0');
    return 0;
}

int readHash(char **cursor, const char *name, unsigned char out[32]) {
    const char *text = nbt_field(cursor, name);
    return (!text || nbt_hex_decode(text, out, 32)) ? -1 : 0;
}

core::Status invalid() {
    return core::Status::error(EINVAL, "invalid manager slot state");
}

template <typename Fn> int applyState(nbt_manager_state *state, bool ok, Fn fn) {
    if (!state || !ok) {
        return -1;
    }
    core::Result<SlotState> result = SlotState::fromLegacy(*state);
    if (!result.ok() || !fn(result.value()).ok()) {
        return -1;
    }
    *state = result.value().legacy();
    return 0;
}

} // namespace

SlotState::SlotState() : state_() {
    clearPrevious(state_);
}

core::Result<SlotState> SlotState::initial(const nbt_release &release) {
    SlotState state;
    if (!release.sequence || !hashNonzero(release.sha256)) {
        return core::Result<SlotState>::failure(invalid());
    }
    state.state_.active.slot = 0;
    state.state_.active.sequence = release.sequence;
    state.state_.highwater = release.sequence;
    memcpy(state.state_.active.release_hash, release.sha256, 32);
    return core::Result<SlotState>::success(std::move(state));
}

core::Result<SlotState> SlotState::fromLegacy(const nbt_manager_state &state) {
    if (!valid(state)) {
        return core::Result<SlotState>::failure(invalid());
    }
    SlotState result;
    result.state_ = state;
    return core::Result<SlotState>::success(std::move(result));
}

core::Status SlotState::begin(const nbt_release &release, bool recovery) {
    if (!valid(state_) || state_.pending || !release.sequence || !hashNonzero(release.sha256) ||
        (!recovery && release.sequence <= state_.highwater) ||
        (recovery && release.sequence >= state_.active.sequence)) {
        return invalid();
    }
    state_.previous = state_.active;
    state_.active.slot = 1u - state_.previous.slot;
    state_.active.sequence = release.sequence;
    memcpy(state_.active.release_hash, release.sha256, 32);
    state_.pending = 1;
    return core::Status();
}

core::Status SlotState::rollback() {
    if (!valid(state_) || !state_.pending) {
        return invalid();
    }
    state_.active = state_.previous;
    state_.pending = 0;
    clearPrevious(state_);
    clearRestarts();
    return core::Status();
}

core::Status SlotState::trial() {
    if (!valid(state_) || state_.pending != 1) {
        return invalid();
    }
    state_.pending = 2;
    return core::Status();
}

core::Status SlotState::confirm(const nbt_process &process, uint64_t nowMs) {
    if (!valid(state_) || state_.pending != 2 || !nbt_process_healthy(&process, nowMs)) {
        return invalid();
    }
    if (state_.active.sequence > state_.highwater) {
        state_.highwater = state_.active.sequence;
    }
    state_.pending = 0;
    clearPrevious(state_);
    clearRestarts();
    return core::Status();
}

core::Status SlotState::parse(const void *data, size_t size) {
    char text[NBT_MANAGER_STATE_MAX];
    SlotState parsed;
    if (!data || size < 12 || size >= sizeof(text) || memchr(data, 0, size)) {
        return invalid();
    }
    memcpy(text, data, size);
    text[size] = 0;
    const bool version2 = strncmp(text, "NBTMSTATE2\n", 11) == 0;
    if (!version2 && strncmp(text, "NBTMSTATE1\n", 11)) {
        return invalid();
    }
    char *cursor = text + 11;
    char *field = nbt_field(&cursor, "phase");
    if (!field || (strcmp(field, "stable") && strcmp(field, "pending") && strcmp(field, "trial"))) {
        return invalid();
    }
    parsed.state_.pending = !strcmp(field, "pending") ? 1u : (!strcmp(field, "trial") ? 2u : 0u);
    if (readSlot(&cursor, "slot", &parsed.state_.active.slot) ||
        readNumber(&cursor, "sequence", &parsed.state_.active.sequence) ||
        readHash(&cursor, "sha256", parsed.state_.active.release_hash) ||
        readNumber(&cursor, "highwater", &parsed.state_.highwater) ||
        readSlot(&cursor, "previous_slot", &parsed.state_.previous.slot) ||
        readNumber(&cursor, "previous_sequence", &parsed.state_.previous.sequence) ||
        readHash(&cursor, "previous_sha256", parsed.state_.previous.release_hash)) {
        return invalid();
    }
    if (version2) {
        uint64_t restarts = 0;
        if (readNumber(&cursor, "restarts", &restarts) || restarts > 3 ||
            readNumber(&cursor, "restart_epoch", &parsed.state_.restart_epoch)) {
            return invalid();
        }
        parsed.state_.restarts = static_cast<unsigned>(restarts);
    }
    if (*cursor || !valid(parsed.state_)) {
        return invalid();
    }
    state_ = parsed.state_;
    return core::Status();
}

core::Result<size_t> SlotState::format(char *buffer, size_t size) const {
    char activeHash[65], previousHash[65];
    if (!valid(state_) || !buffer || !size ||
        nbt_hex(state_.active.release_hash, 32, activeHash, sizeof(activeHash)) ||
        nbt_hex(state_.previous.release_hash, 32, previousHash, sizeof(previousHash))) {
        return core::Result<size_t>::failure(invalid());
    }
    const char previousSlot =
        state_.previous.slot == 2 ? '-' : static_cast<char>('0' + state_.previous.slot);
    const char *phase =
        state_.pending == 2 ? "trial" : (state_.pending == 1 ? "pending" : "stable");
    const int count = snprintf(
        buffer, size,
        "NBTMSTATE2\nphase=%s\nslot=%u\nsequence=%020llu\nsha256=%s\n"
        "highwater=%020llu\nprevious_slot=%c\nprevious_sequence=%020llu\nprevious_sha256=%s\n"
        "restarts=%020u\nrestart_epoch=%020llu\n",
        phase, state_.active.slot, static_cast<unsigned long long>(state_.active.sequence),
        activeHash, static_cast<unsigned long long>(state_.highwater), previousSlot,
        static_cast<unsigned long long>(state_.previous.sequence), previousHash, state_.restarts,
        static_cast<unsigned long long>(state_.restart_epoch));
    if (count < 0 || static_cast<size_t>(count) >= size) {
        return core::Result<size_t>::failure(
            core::Status::error(ENOSPC, "manager slot state is too large"));
    }
    return core::Result<size_t>::success(static_cast<size_t>(count));
}

RestartDecision SlotState::recordRestart(uint64_t epochSeconds, unsigned *delayMs) {
    static const unsigned delays[] = {500, 1000, 2000};
    if (!delayMs || !valid(state_)) {
        return RestartDecision::Exhausted;
    }
    // A clock behind the last restart means a reboot: the car's wall clock is
    // not set and starts again from 2000-01-01 at every boot.
    if (state_.restarts &&
        (epochSeconds < state_.restart_epoch || epochSeconds - state_.restart_epoch > 60)) {
        clearRestarts();
    }
    if (state_.restarts == 3) {
        return RestartDecision::Exhausted;
    }
    ++state_.restarts;
    state_.restart_epoch = epochSeconds;
    *delayMs = delays[state_.restarts - 1];
    return RestartDecision::Restart;
}

} // namespace launcher
} // namespace nbt

using nbt::launcher::SlotState;

int nbt_manager_state_initial(nbt_manager_state *state, const nbt_release *release) {
    if (!state || !release) {
        return -1;
    }
    nbt::core::Result<SlotState> result = SlotState::initial(*release);
    if (!result.ok()) {
        return -1;
    }
    *state = result.value().legacy();
    return 0;
}

int nbt_manager_state_begin(nbt_manager_state *state, const nbt_release *release, int recovery) {
    return nbt::launcher::applyState(
        state, release && (recovery == 0 || recovery == 1),
        [release, recovery](SlotState &s) { return s.begin(*release, recovery != 0); });
}

int nbt_manager_state_rollback(nbt_manager_state *state) {
    return nbt::launcher::applyState(state, true, [](SlotState &s) { return s.rollback(); });
}

int nbt_manager_state_trial(nbt_manager_state *state) {
    return nbt::launcher::applyState(state, true, [](SlotState &s) { return s.trial(); });
}

int nbt_manager_state_confirm(nbt_manager_state *state, const nbt_process *process,
                              uint64_t now_ms) {
    return nbt::launcher::applyState(state, process != NULL, [process, now_ms](SlotState &s) {
        return s.confirm(*process, now_ms);
    });
}

int nbt_manager_state_parse(const void *data, size_t size, nbt_manager_state *state) {
    SlotState result;
    if (!state || !result.parse(data, size).ok()) {
        return -1;
    }
    *state = result.legacy();
    return 0;
}

int nbt_manager_state_format(const nbt_manager_state *state, char *buffer, size_t size) {
    if (!state) {
        return -1;
    }
    nbt::core::Result<SlotState> value = SlotState::fromLegacy(*state);
    if (!value.ok()) {
        return -1;
    }
    nbt::core::Result<size_t> result = value.value().format(buffer, size);
    return result.ok() ? static_cast<int>(result.value()) : -1;
}
