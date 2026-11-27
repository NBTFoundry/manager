// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/theme.hpp"

#include "nbt/log.h"

#include <errno.h>

namespace nbt {
namespace gui {
namespace {

/* Each follows photographs of that make's own screen (docs/head-unit-interfaces.md,
 * "The car's make and iDrive's look"). BMW: a black page, dark tiles, the
 * selected one filled deep red, hairlines between rows. MINI: a solid blue
 * bar, no lines. Supra: an orange-red glow under the selected row, fading to
 * the right, with a bright edge and underline, and a line under the title. */
const Palette kPalettes[model::kThemeCount] = {
    {0x080808, 0xffffff, 0xa9abae, 0x980c0c, 0x5c0808, 0x202020, 0xff8a80, 0xb81616, 0x161616,
     0xf01212, 0x3a3a3a, 0, 0, 0, 2},
    {0x05080c, 0xffffff, 0xa8b4c0, 0x0a7cc4, 0x0a7cc4, 0x1a2430, 0x7fd0ff, 0x1296e6, 0x0e1620,
     0x38d8f0, 0x24384c, 0, 0, 0, 0},
    {0x060606, 0xffffff, 0xb0aeac, 0x7a2606, 0x200a02, 0x242220, 0xffa07a, 0x9c3a0c, 0x151312,
     0xf85828, 0x7a3a22, 0, 4, 2, 1},
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
