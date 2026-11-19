// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_GAMEPAD_INPUT_HPP
#define NBT_GUI_GAMEPAD_INPUT_HPP
#include "nbt/gamepad/input.hpp"
#include "nbt/gamepad/service.hpp"
#include "nbt/gui/ui_controller.hpp"
namespace nbt {
namespace gui {
class GamepadInput {
public:
    GamepadInput();
    void start(const char *root);
    bool poll(UiController &ui, uint64_t now, bool active);
    /* The panel may go to the application at once: the GUI keeps the pads
     * and the application's host relays them (gamepad/relay.hpp). */
    bool takeHandover();
    void stop();
#ifdef NBT_TESTING
    void attachForTesting(int fd) {
        input_.reset(fd);
    }
#endif
    int fd() const {
        return input_.get();
    }
    /* The latest pad state, read whether or not the manager is shown. */
    const nbt_gamepad_wire_state &state() const {
        return reader_.state();
    }
    gamepad::Service::Report report() const {
        return service_.report();
    }
    /* The Status page's lines (each `size` bytes): the open pad, or why none
     * is; `detail` the step at which a plugged-in pad failed, or "". */
    static void describe(const gamepad::Service::Report &report, char *pad, char *detail,
                         size_t size);

private:
    struct Delivery {
        GamepadInput *self;
        UiController *ui;
        uint64_t now;
        bool changed;
    };
    static void receive(void *context, const nbt_gamepad_wire_state &state);
    bool apply(UiController &ui, const nbt_gamepad_wire_state &state, uint64_t now);
    nbt_fs fs_;
    core::UniqueFd input_;
    gamepad::Service service_;
    gamepad::Input reader_;
    gamepad::Navigation navigation_;
    bool active_, selecting_;
    bool pending_ = false;
};
} // namespace gui
} // namespace nbt
#endif
