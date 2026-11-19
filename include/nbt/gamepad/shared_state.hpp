#ifndef NBT_GAMEPAD_SHARED_STATE_HPP
#define NBT_GAMEPAD_SHARED_STATE_HPP
#include "nbt/core/unique_fd.hpp"
#include "nbt/gamepad_wire.h"
#include <stdint.h>
/* The pads' state as the manager GUI, their one USB owner, reads it, for the
 * application hosts to relay: one 48-byte record (magic, count, monotonic ms,
 * the 24-byte wire state, count again) rewritten at each delivery, at least
 * every 100 ms. Same owner and mode checks as the capture control file. */
#define NBT_GAMEPAD_STATE_PATH "/dev/shmem/nbt-manager-gamepad"
namespace nbt {
namespace gamepad {
const size_t kStateRecordSize = 48;
const uint64_t kStateStaleMs = 500;
/* Pure: the record for `state` at `now`, and the state a record holds. */
void encodeRecord(const nbt_gamepad_wire_state &state, uint32_t count, uint64_t now,
                  uint8_t out[kStateRecordSize]);
/* The state, or disconnected when the record is torn, malformed or older than
 * kStateStaleMs at `now`. */
nbt_gamepad_wire_state decodeRecord(const uint8_t *record, size_t size, uint64_t now);
class StateWriter {
public:
    StateWriter();
    bool open(const char *path);
    void publish(const nbt_gamepad_wire_state &state, uint64_t now);

private:
    core::UniqueFd fd_;
    uint32_t count_;
};
class StateReader {
public:
    StateReader();
    /* Opens `path` when needed (at most once a second while it fails). */
    nbt_gamepad_wire_state read(const char *path, uint64_t now);

private:
    core::UniqueFd fd_;
    uint64_t retryAt_;
};
} // namespace gamepad
} // namespace nbt
#endif
