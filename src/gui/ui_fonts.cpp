// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/ui_fonts.hpp"

/* Defined in C (src/gui/fonts/). */
extern "C" {
LV_FONT_DECLARE(nbt_font_latin_14)
LV_FONT_DECLARE(nbt_font_latin_18)
LV_FONT_DECLARE(nbt_font_latin_24)
LV_FONT_DECLARE(nbt_font_sc_14)
LV_FONT_DECLARE(nbt_font_sc_18)
LV_FONT_DECLARE(nbt_font_sc_24)
LV_FONT_DECLARE(nbt_font_jp_14)
LV_FONT_DECLARE(nbt_font_jp_18)
LV_FONT_DECLARE(nbt_font_jp_24)
}

namespace nbt {
namespace gui {

UiFonts uiFonts(model::Language language) {
    const UiFonts sc = {&nbt_font_sc_24, &nbt_font_sc_14, &nbt_font_sc_18};
    const UiFonts jp = {&nbt_font_jp_24, &nbt_font_jp_14, &nbt_font_jp_18};
    const UiFonts latin = {&nbt_font_latin_24, &nbt_font_latin_14, &nbt_font_latin_18};
    return language == model::LangZh ? sc : language == model::LangJa ? jp : latin;
}

} // namespace gui
} // namespace nbt
