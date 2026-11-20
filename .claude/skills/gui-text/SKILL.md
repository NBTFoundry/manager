---
name: gui-text
description: Add or change a text on NBT Manager's car screen or in the phone app, in all seven languages (en, fr, de, it, es, zh, ja) - catalogs, generated C++, fonts for Chinese and Japanese, panel length limits. Use whenever user-facing wording changes.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Texts in seven languages

English, French, German, Italian, Spanish, Chinese (Simplified), Japanese.
English is the fallback. Write every translation yourself: short, natural,
the same placeholders.

## Car screen (manager)

1. `src/gui/text/en.txt` defines the keys and their order (`key = text`;
   `{1}` and `{2}` are filled at run time). Every other catalog has exactly
   the same keys. The panel shows about 40 characters in a title and 70 in a
   line; it cuts the rest.
2. `python3 tools/gui_text.py` regenerates `include/nbt/gui/text_id.hpp` and
   `src/gui/text_tables.cpp`; never edit those by hand.
3. Chinese or Japanese text changed: regenerate the fonts (needs Pillow and fontTools):
   ```sh
   python3 tools/make_fonts.py --montserrat <Montserrat-Medium.ttf> \
       --cjk /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc
   ```
   Use the same font files `src/gui/fonts/glyphs.json` records under
   `sources` (SHA-256 and revision: Montserrat 9.000 from
   github.com/JulietaUla/Montserrat `fonts/ttf`, Noto Sans CJK 2.004). Then
   `git diff --stat src/gui/fonts`: only the `sc`/`jp` fonts and
   `glyphs.json` change, unless Latin characters were added.
4. `tests/test_gui_text.py` (part of `make test`) fails while the generated
   files or the fonts are stale.

## Phone app (mobile-app)

`src/i18n/<lang>.ts` for the same seven languages; `tests/i18n.test.ts` checks
that every language has every key.
