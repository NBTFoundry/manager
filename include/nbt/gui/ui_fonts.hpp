/* The renderer's fonts in a language: the GUI's own (src/gui/fonts/,
 * tools/make_fonts.py). Chinese and Japanese draw their characters from
 * their own faces and fall back to the Latin font of the same size, whose
 * line they share, so every language lays out alike. */
#ifndef NBT_GUI_UI_FONTS_HPP
#define NBT_GUI_UI_FONTS_HPP

#include "lvgl/lvgl.h"
#include "nbt/model/language.hpp"

namespace nbt {
namespace gui {

struct UiFonts {
    const lv_font_t *title; // 24 px
    const lv_font_t *line;  // 14 px
    const lv_font_t *item;  // 18 px
};

UiFonts uiFonts(model::Language language);

} // namespace gui
} // namespace nbt

#endif
