// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_MOST_CONTROLLER_FEED_HPP
#define NBT_MOST_CONTROLLER_FEED_HPP

#include "nbt/ccs.h"
#include "nbt/controller_wire.h"
#include "nbt/core/mutex.hpp"
#include "nbt/most.h"

namespace nbt {
namespace most {

/* Bridge capability `controller`, host side: the iDrive controller (FBlock
 * 0xE5) and the centre stack's favourite keys (0x15.0C, function 0xC02),
 * decoded into one state (include/nbt/controller_wire.h) and sent to the
 * application's socket when it changes. Messages arrive on the DSI thread;
 * pause comes from the host's main loop. Layouts: docs/controller-input.md. */
class ControllerFeed {
public:
    ControllerFeed();
    /* `fd` is the host's non-blocking end, kept; NULL config: defaults. */
    void start(int fd, const nbt_ccs_config *config);
    bool active() const {
        return fd_ >= 0;
    }
    /* Appends the device controller keys (fblock << 8 | inst) the feed needs
     * to `keys[0..count)`, skipping ones present, up to `max`; returns the
     * new count. */
    size_t controls(uint16_t *keys, size_t count, size_t max) const;
    void message(const nbt_most_message &message);
    void link(bool up);
    /* States are sent only to an application that has a surface (`shown`,
     * from its start) and is not paused; either change to sending sends the
     * current state. */
    void pause(bool paused);
    void show(bool shown);
    /* For tests: the state as last decoded. */
    nbt_controller_wire_state state();

private:
    bool decode(const nbt_most_message &m);
    bool knob(unsigned fkt, const unsigned char *d, size_t n);
    bool ccs(unsigned fkt, const unsigned char *d, size_t n);
    bool rotate(int *last, int position);
    uint32_t slotKey(unsigned slot, bool ccs) const;
    bool setKeys(uint32_t *field, uint32_t keys);
    bool sending() const {
        return fd_ >= 0 && !paused_ && shown_;
    }
    void changed(bool wasSending);
    void sendLocked();
    void emit(bool gone);

    core::Mutex lock_;
    int fd_;
    nbt_ccs_config config_;
    nbt_controller_wire_state state_;
    int position_, ccsPosition_; /* -1 until known */
    uint32_t knobMotion_, knobKeys_, ccsMotion_, ccsKeys_, favourites_;
    bool paused_, shown_;
};

} // namespace most
} // namespace nbt

#endif
