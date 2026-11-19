#include "nbt/gui/gamepad_input.hpp"
#include "nbt/gui/view_builder.hpp"
#include "nbt/log.h"
#include <stdlib.h>
#include <sys/select.h>
namespace nbt {
namespace gui {
GamepadInput::GamepadInput()
    : fs_(), input_(), service_(), reader_(), navigation_(), active_(false), selecting_(false) {
}
void GamepadInput::start(const char *root) {
    char error[128];
    int host = -1, child = -1;
    error[0] = 0;
    if (!root || nbt_fs_init(&fs_, root, error, sizeof(error))) {
        nbt_logf("gamepad: manager root unusable: %s", error[0] ? error : "none");
        return;
    }
    if (!gamepad::Service::socket(host, child)) {
        return; // logged by the service
    }
    core::UniqueFd hostFd(host);
    if (child >= FD_SETSIZE) {
        nbt_logf("gamepad: GUI socket %d is beyond select's limit", child);
        core::UniqueFd tooLarge(child);
        return;
    }
    input_.reset(child);
    const char *shared = NBT_GAMEPAD_STATE_PATH;
#ifdef NBT_TESTING
    shared = getenv("NBT_TEST_GAMEPAD_STATE"); // unset: nothing published
#endif
    if (!service_.start(&fs_, hostFd.release(), gamepad::makeUsbTransport(), shared)) {
        input_.reset();
    }
}
bool GamepadInput::poll(UiController &ui, uint64_t now, bool active) {
    pending_ = ui.takeHandover() || pending_;
    active = active && input_.valid() && !pending_;
    if (active != active_) {
        reader_.reset();
        navigation_.reset();
        active_ = active;
        if (selecting_) {
            ui.holding = 0;
        }
        selecting_ = false;
    }
    const bool wasConnected = reader_.state().connected != 0;
    Delivery delivery = {this, &ui, now, false};
    reader_.poll(input_.get(), now, active_ ? &receive : NULL, &delivery);
    if (!active_) {
        return false;
    }
    return apply(ui, reader_.state(), now) || delivery.changed ||
           wasConnected != (reader_.state().connected != 0);
}
void GamepadInput::receive(void *opaque, const nbt_gamepad_wire_state &state) {
    Delivery *delivery = static_cast<Delivery *>(opaque);
    delivery->changed =
        delivery->self->apply(*delivery->ui, state, delivery->now) || delivery->changed;
}
bool GamepadInput::apply(UiController &ui, const nbt_gamepad_wire_state &state, uint64_t now) {
    const gamepad::NavigationEvent e = navigation_.update(state, now);
    if (ui.screen == GamepadTest) { // shown there, not acted on (status_page.hpp)
        selecting_ = false;
        return e.pressed || e.released;
    }
    if (!state.connected && selecting_) {
        ui.holding = 0;
        selecting_ = false;
    }
    const uint32_t pressed = e.pressed | e.repeated;
    if (pressed & NBT_GAMEPAD_UP) {
        (void)ui.press(KeyUp);
    }
    if (pressed & NBT_GAMEPAD_DOWN) {
        (void)ui.press(KeyDown);
    }
    if (pressed & (NBT_GAMEPAD_EAST | NBT_GAMEPAD_VIEW | NBT_GAMEPAD_LEFT)) {
        (void)ui.press(KeyBack);
    }
    if (e.pressed & NBT_GAMEPAD_MENU) {
        (void)ui.home();
    }
    if (e.pressed & NBT_GAMEPAD_SOUTH) {
        (void)ui.press(KeySelect);
        selecting_ = true;
    }
    if ((e.released & NBT_GAMEPAD_SOUTH) && selecting_) {
        (void)ui.release(KeySelect);
        selecting_ = false;
    }
    return pressed || e.released;
}
namespace {
const char *familyName(gamepad::Family family) {
    switch (family) {
    case gamepad::Gip:
        return "Xbox";
    case gamepad::DualShock4:
        return "DualShock 4";
    case gamepad::DualSense:
        return "DualSense";
    default:
        return text(T_PAD_GENERIC);
    }
}

} // namespace
void GamepadInput::describe(const gamepad::Service::Report &r, char *pad, char *detail,
                            size_t size) {
    if (!size) {
        return;
    }
    detail[0] = 0;
    char number[16];
    if (!r.running) {
        textf(pad, size, T_PAD_UNAVAILABLE);
    } else if (r.family != gamepad::Unsupported) {
        ViewBuilder::formatText(number, sizeof(number), "%04x:%04x", r.vendor, r.product);
        textf(pad, size, T_PAD_FOUND, familyName(r.family), number);
    } else if (r.lockRefused) {
        textf(pad, size, T_PAD_HELD);
    } else if (!r.owner && r.problem.connectError) {
        ViewBuilder::formatText(number, sizeof(number), "%d", r.problem.connectError);
        textf(pad, size, T_PAD_NO_USB, number);
    } else if (!r.owner) {
        textf(pad, size, T_PAD_NOT_CONNECTED);
    } else if (r.problem.step) {
        ViewBuilder::formatText(number, sizeof(number), "%04x:%04x", r.problem.vendor,
                                r.problem.product);
        textf(pad, size, T_PAD_OPEN_FAILED, number);
        if (size > 2) { // indented under the pad's line
            detail[0] = detail[1] = ' ';
            textf(detail + 2, size - 2, T_PAD_RETRYING, r.problem.step);
        }
    } else {
        textf(pad, size, T_PAD_NONE);
    }
}
bool GamepadInput::takeHandover() {
    if (!pending_) {
        return false;
    }
    pending_ = false;
    return true;
}
void GamepadInput::stop() {
    service_.stop();
    input_.reset();
}
} // namespace gui
} // namespace nbt
