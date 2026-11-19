// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_HOST_CHANNELS_HPP
#define NBT_PROCESS_HOST_CHANNELS_HPP
#include "nbt/bridge.h"
#include "nbt/gamepad/relay.hpp"
namespace nbt {
namespace process {
class HostChannels {
public:
    HostChannels();
    ~HostChannels();
    void prepare(const char *app, const nbt_bridge_requirement &bridge, const char *served,
                 char available[NBT_BRIDGE_CAPS_MAX + 1]);
    int most() const {
        return most_.get();
    }
    int gamepad() const {
        return gamepad_.get();
    }
    int controller() const {
        return controller_.get();
    }
    int wakeFd() const;
    void spawned(bool surface);
    /* Paused: no MOST messages, controller states or gamepad input reach the
     * application. */
    void pause(bool paused);
    void poll(uint64_t now, short events);
    void stop();
    void releaseChildren();

private:
    core::UniqueFd most_, gamepad_, controller_;
    gamepad::Relay relay_; // the GUI owns the pads; this relays them
    bool mostPolled_;
    bool surface_;
};
} // namespace process
} // namespace nbt
#endif
