// SPDX-License-Identifier: MPL-2.0
#include "nbt/process/host_channels.hpp"
#include "nbt/gamepad/service.hpp"
#include "nbt/log.h"
#include "nbt/most_service.h"
#include "nbt/process/host_control.hpp"
#include <poll.h>
#include <stdlib.h>
#include <string.h>
namespace nbt {
namespace process {
HostChannels::HostChannels()
    : most_(), gamepad_(), controller_(), relay_(), mostPolled_(false), surface_(false) {
}
HostChannels::~HostChannels() {
    stop();
}
void HostChannels::prepare(const char *app, const nbt_bridge_requirement &bridge,
                           const char *served, char available[NBT_BRIDGE_CAPS_MAX + 1]) {
    int controller = -1;
    most_.reset(HostControl::prepareMostChannel(app, bridge, served, available, &controller));
    controller_.reset(controller);
    if (!nbt_bridge_cap_has(bridge.uses, "gamepad") &&
        !nbt_bridge_cap_has(bridge.optional, "gamepad")) {
        return;
    }
    int host = -1, child = -1;
    const char *shared = NBT_GAMEPAD_STATE_PATH;
#ifdef NBT_TESTING
    if (getenv("NBT_TEST_GAMEPAD_STATE")) {
        shared = getenv("NBT_TEST_GAMEPAD_STATE");
    }
#endif
    if (nbt_bridge_cap_has(served, "gamepad") && gamepad::Service::socket(host, child)) {
        gamepad_.reset(child);
        if (relay_.start(host, shared)) {
            return;
        }
        gamepad_.reset();
    }
    char filtered[NBT_BRIDGE_CAPS_MAX + 1];
    nbt_bridge_caps_without(available, "gamepad", filtered);
    memcpy(available, filtered, sizeof(filtered));
    nbt_logf("host %s: gamepad service unavailable", app);
}
int HostChannels::wakeFd() const {
    return mostPolled_ ? nbt_most_service_fd() : -1;
}
void HostChannels::spawned(bool surface) {
    mostPolled_ = most_.valid();
    surface_ = surface;
    relay_.setActive(surface);
    nbt_most_service_controller_shown(surface ? 1 : 0);
    releaseChildren();
}
void HostChannels::pause(bool paused) {
    nbt_most_service_pause(paused ? 1 : 0);
    relay_.setActive(!paused && surface_);
}
void HostChannels::releaseChildren() {
    most_.reset();
    gamepad_.reset();
    controller_.reset();
}
void HostChannels::poll(uint64_t now, short events) {
    if (mostPolled_ && ((events & (POLLERR | POLLHUP | POLLNVAL)) || nbt_most_service_poll(now))) {
        mostPolled_ = false;
        nbt_logf("host: MOST socket closed by the application");
    }
}
void HostChannels::stop() {
    relay_.stop();
    releaseChildren();
    nbt_most_service_stop();
    mostPolled_ = false;
}
} // namespace process
} // namespace nbt
