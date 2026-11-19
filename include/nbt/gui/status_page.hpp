/* Settings > Status: what the manager knows about its input, to check a car
 * run on the panel itself, and its gamepad test (live buttons, sticks and
 * triggers). The GUI session refreshes the facts every loop (gatherStatus);
 * the two screens only show them and act on nothing but their own keys. The
 * pad's own buttons are shown on the test, never acted on there: the iDrive
 * controller's BACK leaves it. */
#ifndef NBT_GUI_STATUS_PAGE_HPP
#define NBT_GUI_STATUS_PAGE_HPP

#include "nbt/gamepad/reports.hpp"
#include "nbt/gamepad_wire.h"
#include "nbt/gui/ui_view.hpp"

namespace nbt {
namespace gui {

class CaptureWatch;
class GamepadInput;
class UiController;

struct StatusFacts {
    char keys[kTextMax + 1];       // whether the capture shim keeps the keys from iDrive
    char controller[kTextMax + 1]; // the iDrive controller's MOST input
    char pad[kTextMax + 1];        // the open pad, or why none is
    char padDetail[kTextMax + 1];  // the step that failed, or ""
    char lastError[kTextMax + 1];  // the last error screen since the manager opened
    gamepad::Family padFamily;     // names the buttons on the test
    nbt_gamepad_wire_state padState;
};

/* Every loop: the facts, and the capture note for the manager's list.
 * `controllerInput`: the MOST controller input is still read. */
void gatherStatus(UiController &ui, const CaptureWatch &capture, const GamepadInput &gamepad,
                  bool controllerInput);
/* Keys for Settings' Status entry and the two screens; false: not theirs. */
bool statusPress(UiController &ui, Key key);
void statusView(const UiController &ui, UiView &view);
/* The test's text for one button set ("A LB Up"), Sony's names for Sony
 * pads; "nothing" when none is pressed. */
void buttonNames(gamepad::Family family, uint32_t buttons, char *out, size_t size);

} // namespace gui
} // namespace nbt

#endif
