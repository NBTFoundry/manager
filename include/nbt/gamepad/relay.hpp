// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GAMEPAD_RELAY_HPP
#define NBT_GAMEPAD_RELAY_HPP
#include "nbt/core/mutex.hpp"
#include "nbt/core/unique_fd.hpp"
#include "nbt/gamepad/shared_state.hpp"
#include <pthread.h>
namespace nbt {
namespace gamepad {
/* Capability `gamepad`, host side: the GUI's published pad state (one USB
 * owner, shared_state.hpp) sent on the application's NBT_GAMEPAD_FD as the
 * service sent it (24-byte wire states, on change and at least every 100 ms).
 * Disconnected unless active (a surface, not paused) and the application's
 * surface holds the panel. A second thread asks LayerManager which surface
 * holds the panel (a child process, up to 3 s): the sending thread never waits
 * for it, and an unanswered question keeps the last answer. The pad's family
 * reaches only applications built for bridge 1.5 or later (`family`); older
 * readers reject it. */
class Relay {
public:
    Relay();
    ~Relay();
    /* Takes `fd` (the host's end) also on failure; `path` NULL: the car's. */
    bool start(int fd, const char *path, bool family);
    void setActive(bool active);
    void stop();

private:
    Relay(const Relay &) = delete;
    Relay &operator=(const Relay &) = delete;
    static void *entry(void *self);
    static void *watchEntry(void *self);
    void run();
    void watch();
    core::Mutex mutex_;
    core::UniqueFd socket_;
    pthread_t thread_, watcher_;
    const char *path_;
    bool started_, stopping_, active_, focused_, family_;
    uint64_t sentAt_;
    uint32_t sequence_;
    nbt_gamepad_wire_state sent_;
    StateReader reader_;
};
} // namespace gamepad
} // namespace nbt
#endif
