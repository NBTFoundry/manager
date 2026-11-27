// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/theme.hpp"

#include "nbt/log.h"

#include <errno.h>

namespace nbt {
namespace gui {
namespace {

/* BMW follows iDrive 6: a near-black page, square shapes, a dark orange
 * cursor with a bright edge and underline, thin rules under titles and
 * between rows. Supra: the same shapes in red. MINI: round shapes, one
 * colour, no rules. The MINI and Supra looks are ours, not taken from a car. */
const Palette kPalettes[model::kThemeCount] = {
    {0x0b0c0e, 0xffffff, 0xaab0b8, 0x6a1c02, 0x2a0d03, 0x24272b, 0xffa869, 0x8f3408, 0x15171a,
     0xf25a0a, 0x4a4f57, 0, 4, 2, 1},
    {0x0a0a0a, 0xffffff, 0xb0b0b0, 0x0d6b63, 0x0d6b63, 0x262626, 0x7fe3d6, 0x128a7f, 0x171717,
     0x2ed3c1, 0x3a3a3a, -1, 0, 0, 0},
    {0x0c0c0d, 0xffffff, 0xb3b3b6, 0x6e0c12, 0x2b0709, 0x29292c, 0xff8a8f, 0x96121b, 0x18181a,
     0xeb0a1e, 0x4d4d52, 0, 4, 2, 1},
};

int current; // a model::Theme

} // namespace

const Palette &palette(int theme) {
    return kPalettes[model::themeCode(theme) ? theme : model::ThemeBmw];
}

void setCurrentTheme(model::Theme theme) {
    current = model::themeCode(theme) ? theme : model::ThemeBmw;
}

model::Theme currentTheme() {
    return static_cast<model::Theme>(current);
}

void ThemeWatch::start(const char *root, const char *carPath) {
    char error[160];
    ready_ = root && !nbt_fs_init(&fs_, root, error, sizeof(error));
    car_ = model::themeCarRead(carPath);
    apply(true);
}

void ThemeWatch::poll(uint64_t nowMs) {
    if (ready_ && nowMs >= nextMs_) {
        nextMs_ = nowMs + 1000;
        apply(false);
    }
}

void ThemeWatch::apply(bool log) {
    int choice = model::kThemeAuto;
    if (ready_ && model::themeSettingLoad(&fs_, choice) && log) {
        nbt_logf("theme setting unreadable (errno %d); automatic", errno);
    }
    const model::Theme theme = model::themeEffective(choice, car_);
    if (log || theme != currentTheme()) {
        nbt_logf("theme %s, car %s, shown %s", model::themeSettingCode(choice),
                 model::themeCode(car_) ? model::themeCode(car_) : "unknown",
                 model::themeCode(theme));
    }
    setCurrentTheme(theme);
}

} // namespace gui
} // namespace nbt
