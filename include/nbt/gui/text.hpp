/* The manager GUI's texts in its languages (src/gui/text/<code>.txt, generated
 * into text_id.hpp and text_tables.cpp by tools/gui_text.py), and UTF-8
 * helpers for the fixed-size buffers they are written into.
 *
 * The language shown is process state, set by the session (LanguageWatch)
 * and the Language screen; English until then. Texts take {1} and {2},
 * replaced by strings only, so a translation cannot change argument types. */
#ifndef NBT_GUI_TEXT_HPP
#define NBT_GUI_TEXT_HPP

#include "nbt/gui/text_id.hpp"
#include "nbt/model/language.hpp"

#include <stddef.h>

namespace nbt {
namespace gui {

model::Language currentLanguage();
void setCurrentLanguage(model::Language language);

/* `id` in the current language, or in `language`; "" for an unknown id. */
const char *text(TextId id);
const char *textIn(model::Language language, TextId id);

/* `id`'s text with {1} and {2} replaced by `a` and `b` (NULL: nothing),
 * cut to fit `size` at a whole character. */
void textf(char *out, size_t size, TextId id, const char *a = NULL, const char *b = NULL);

/* Drops an incomplete UTF-8 sequence that a cut left at the end of `s`. */
void utf8TrimEnd(char *s);
/* The bytes of `s` (at most `max`) that fit in `columns` display columns,
 * cut at a whole character: one column for most characters, two for CJK
 * and full-width ones. */
size_t utf8Fit(const char *s, size_t max, size_t columns);

} // namespace gui
} // namespace nbt

#endif
