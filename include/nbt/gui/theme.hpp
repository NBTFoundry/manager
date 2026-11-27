// SPDX-License-Identifier: MPL-2.0
/* The colours of the manager's screen, one palette per theme
 * (nbt/model/theme.hpp). The theme shown is process state, as the language
 * (text.hpp): ThemeWatch loads the Theme setting and the car's make when
 * the GUI starts, then checks the setting once a second, so a change made
 * in Settings or from a phone shows at once. */
#ifndef NBT_GUI_THEME_HPP
#define NBT_GUI_THEME_HPP

#include "nbt/fs.h"
#include "nbt/model/theme.hpp"

#include <stdint.h>

namespace nbt {
namespace gui {

/* How a theme draws its page: dark tiles under a header strip (BMW), solid
 * bars over a gradient with rings (MINI), a glow line along a rail (Supra). */
enum Look { LookTiles, LookBars, LookGlow };

/* A theme's colours (0xRRGGBB each) and shapes. */
struct Palette {
    uint32_t background, foreground, muted;
    uint32_t highlight;       // the cursor: the focused row's fill, at its left
    uint32_t highlightFade;   // ...and at its right end
    uint32_t track;           // progress and scroll bars
    uint32_t action;          // the text of an action that is not focused
    uint32_t actionHighlight; // the cursor on an action
    uint32_t dialog;
    uint32_t accent;   // the cursor's bright edge and underline, the progress bar
    uint32_t rule;     // the line under a title and between rows, the dialog's border
    int32_t radius;    // rows, bars and panels; -1: a pill
    int32_t edge;      // the bright edge at the cursor's left, in pixels; 0: none
    int32_t underline; // the bright line under the cursor, in pixels; 0: none
    int ruled;         // 1: a line under titles; 2: lines between rows too; 0: none
    int look;          // a Look
    uint32_t panel;    // the header strip, and a tile that is not selected
    uint32_t deep;     // the page's colour at its bottom (background: at its top)
};

/* The palette of a model::Theme; BMW's for a value out of range. */
const Palette &palette(int theme);

void setCurrentTheme(model::Theme theme);
model::Theme currentTheme();

class ThemeWatch {
public:
    ThemeWatch() : fs_(), ready_(false), car_(-1), nextMs_(0) {
    }
    /* `root`: the manager root (manager/theme); `carPath`: the car's CarPlay
     * configuration, NULL for none. Sets the theme before the first frame. */
    void start(const char *root, const char *carPath);
    void poll(uint64_t nowMs);

private:
    void apply(bool log);

    nbt_fs fs_;
    bool ready_;
    int car_; // the car's make as a model::Theme; -1 unknown
    uint64_t nextMs_;
};

} // namespace gui
} // namespace nbt

#endif
