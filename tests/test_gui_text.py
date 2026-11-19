# SPDX-License-Identifier: MPL-2.0
"""The GUI's text catalogs and fonts agree: the generated tables are up to date
(tools/gui_text.py) and the fonts have every character the catalogs use
(tools/make_fonts.py), read from the fonts' own character maps."""

import re
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import gui_text  # noqa: E402

FONTS = ROOT / "src" / "gui" / "fonts"


def code_points(path):
    """The characters a generated LVGL font maps."""
    text = path.read_text(encoding="utf-8")
    lists = {name: [int(v) for v in re.findall(r"\d+", body)]
             for name, body in re.findall(r"static const uint16_t (unicode_list_\d+)\[\] = \{(.*?)\};", text, re.S)}
    points = set()
    for start, name in re.findall(r"\.range_start = (\d+), .*?\.unicode_list = (unicode_list_\d+)", text):
        points |= {int(start) + offset for offset in lists[name]}
    return points


class GuiTextTests(unittest.TestCase):
    def test_generated_tables_are_up_to_date(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools" / "gui_text.py"), "--check"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_catalogs_match_english(self):
        catalogs, keys = gui_text.load()  # raises on a missing key or placeholder
        self.assertEqual(set(catalogs), set(gui_text.LANGUAGES))
        self.assertGreater(len(keys), 100)
        for code, entries in catalogs.items():
            self.assertEqual(list(entries) if code == "en" else sorted(entries), keys if code == "en" else sorted(keys))

    def test_fonts_have_every_character(self):
        catalogs, _ = gui_text.load()
        for size in (14, 18, 24):
            latin = code_points(FONTS / f"nbt_font_latin_{size}.c")
            faces = {"zh": code_points(FONTS / f"nbt_font_sc_{size}.c"),
                     "ja": code_points(FONTS / f"nbt_font_jp_{size}.c")}
            # The Language list names every language in every language.
            self.assertTrue({ord(c) for c in "EnglishFrançaisDeutschItalianoEspañol中文日本語"} <= latin)
            for code, entries in catalogs.items():
                have = latin | faces.get(code, set())
                missing = sorted({c for text in entries.values() for c in text if ord(c) not in have})
                self.assertEqual(missing, [], f"{code} at {size} px: run tools/make_fonts.py")

    def test_fonts_fall_back_to_latin(self):
        for size in (14, 18, 24):
            for face in ("sc", "jp"):
                text = (FONTS / f"nbt_font_{face}_{size}.c").read_text(encoding="utf-8")
                self.assertIn(f".fallback = &nbt_font_latin_{size},", text)
                latin = (FONTS / f"nbt_font_latin_{size}.c").read_text(encoding="utf-8")
                for metric in ("line_height", "base_line"):
                    self.assertEqual(re.search(rf"\.{metric} = (\d+),", text).group(1),
                                     re.search(rf"\.{metric} = (\d+),", latin).group(1))


if __name__ == "__main__":
    unittest.main()
