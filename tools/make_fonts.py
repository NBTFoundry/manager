#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Generates the manager GUI's fonts, src/gui/fonts/nbt_font_*.c, and their
manifest, src/gui/fonts/glyphs.json, in LVGL's own font format (fmt_txt, as
lv_font_conv writes it): glyphs drawn by FreeType (Pillow), advances, metrics
and kerning from the font files (fontTools).

    python3 tools/make_fonts.py --montserrat Montserrat-Medium.ttf \\
        --cjk /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc

Montserrat-Medium.ttf: https://github.com/JulietaUla/Montserrat (fonts/ttf),
the face of LVGL's built-in fonts. NotoSansCJK-Regular.ttc:
https://github.com/notofonts/noto-cjk (Sans/OTC), or a distribution's
fonts-noto-cjk. Both are under the SIL Open Font License (src/gui/fonts/).

Three sizes, as the renderer uses them (src/gui/ui_renderer.cpp): 14 for lines,
18 for items and 24 for titles; 4 bits per pixel, uncompressed.
- nbt_font_latin_<size>: Montserrat Medium for Latin-1, Latin Extended-A and
  common punctuation (English, French, German, Italian, Spanish, and accented
  phone and application names), with its kerning, plus the Chinese and
  Japanese names of those languages for the Language list.
- nbt_font_sc_<size> and nbt_font_jp_<size>: only the characters the Chinese
  and Japanese catalogs use (src/gui/text/zh.txt, ja.txt), from Noto Sans CJK
  SC and JP, which draw some shared characters differently. Their fallback is
  the Latin font of the same size, for Latin letters and digits, and they take
  its line height and baseline, so every language lays out alike.
Regenerate after changing a Chinese or Japanese text: tests/test_gui_text.py
fails until the fonts have every character the catalogs use.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

from fontTools.ttLib import TTCollection, TTFont
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gui_text  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "src" / "gui" / "fonts"
SIZES = (14, 18, 24)
LATIN_RANGES = [(0x20, 0x7E), (0xA0, 0x17F), (0x2013, 0x2014), (0x2018, 0x201E),
                (0x2022, 0x2022), (0x2026, 0x2026), (0x20AC, 0x20AC)]
# The language names the Language list shows in every language (nbt/model/language.hpp).
NAMES = "中文日本語"
FACES = {"sc": ("Noto Sans CJK SC", "zh"), "jp": ("Noto Sans CJK JP", "ja")}


def in_latin(char):
    return any(low <= ord(char) <= high for low, high in LATIN_RANGES)


def needed():
    """The characters each CJK font needs; checks the other catalogs stay Latin."""
    catalogs, _ = gui_text.load()
    chars = {}
    for code, entries in catalogs.items():
        outside = "".join(sorted(c for c in set("".join(entries.values())) if not in_latin(c)))
        if code in ("zh", "ja"):
            chars[code] = outside
        elif outside:
            raise SystemExit(f"make_fonts: {code}.txt uses {outside!r}, outside the Latin ranges")
    return {face: chars[code] for face, (_, code) in FACES.items()}


class Source:
    """One face: its tables (fontTools) and its outlines at a size (Pillow)."""

    def __init__(self, path, family=None):
        self.path = str(path)
        if family is None:
            self.index, self.font = 0, TTFont(self.path, lazy=True)
        else:
            fonts = TTCollection(self.path, lazy=True).fonts
            names = [f["name"].getDebugName(1) for f in fonts]
            if family not in names:
                raise SystemExit(f"make_fonts: {path} has no {family}")
            self.index = names.index(family)
            self.font = fonts[self.index]
        self.cmap = self.font.getBestCmap()
        self.upem = self.font["head"].unitsPerEm
        self.revision = round(self.font["head"].fontRevision, 3)

    def at(self, size):
        return ImageFont.truetype(self.path, size=size, index=self.index,
                                  layout_engine=ImageFont.Layout.BASIC)

    def advance(self, char, size):
        return self.font["hmtx"][self.cmap[ord(char)]][0] * size / self.upem


def render(source, image_font, char, size):
    """(adv_w in 1/16 px, box_w, box_h, ofs_x, ofs_y, 4-bit pixels) of `char`."""
    x0, y0, x1, y1 = image_font.getbbox(char, anchor="ls")
    adv = round(source.advance(char, size) * 16)
    if x1 <= x0 or y1 <= y0:
        return adv, 0, 0, 0, 0, []
    image = Image.new("L", (x1 - x0, y1 - y0), 0)
    ImageDraw.Draw(image).text((-x0, -y0), char, font=image_font, fill=255, anchor="ls")
    box = image.getbbox()
    if not box:
        return adv, 0, 0, 0, 0, []
    image = image.crop(box)
    w, h = image.size
    left, top = x0 + box[0], y0 + box[1]  # top: y of the first row, baseline at 0, down positive
    pixels = [(v * 15 + 127) // 255 for v in image.tobytes()]
    return adv, w, h, left, -(top + h), pixels


def kerning(source, chars, size):
    """{(left, right): px} from the face's GPOS kern lookups, for `chars`."""
    if "GPOS" not in source.font:
        return {}
    gpos = source.font["GPOS"].table
    lookups = sorted({i for record in gpos.FeatureList.FeatureRecord if record.FeatureTag == "kern"
                      for i in record.Feature.LookupListIndex})
    names = {c: source.cmap[ord(c)] for c in chars if ord(c) in source.cmap}
    tables = []
    for index in lookups:
        lookup = gpos.LookupList.Lookup[index]
        subtables = []
        for table in lookup.SubTable:
            table = table.ExtSubTable if lookup.LookupType == 9 else table
            if getattr(table, "LookupType", 2) != 2:
                continue
            covered = set(table.Coverage.glyphs)
            if table.Format == 1:
                pairs = {}
                for first, pair_set in zip(table.Coverage.glyphs, table.PairSet):
                    pairs[first] = {r.SecondGlyph: getattr(r.Value1, "XAdvance", 0) or 0
                                    for r in pair_set.PairValueRecord}
                subtables.append((1, covered, pairs))
            elif table.Format == 2:
                values = [[getattr(c2.Value1, "XAdvance", 0) or 0 for c2 in c1.Class2Record]
                          for c1 in table.Class1Record]
                subtables.append((2, covered, (table.ClassDef1.classDefs, table.ClassDef2.classDefs, values)))
        tables.append(subtables)
    result = {}
    for left, lname in names.items():
        for right, rname in names.items():
            total = 0
            for subtables in tables:
                for kind, covered, data in subtables:
                    if lname not in covered:
                        continue
                    if kind == 1:
                        if rname in data[lname]:
                            total += data[lname][rname]
                            break
                        continue
                    first, second, values = data
                    total += values[first.get(lname, 0)][second.get(rname, 0)]
                    break
            if total:
                result[(left, right)] = total * size / source.upem
    return result


def classes(glyph_count, kerns):
    """LVGL kern classes: (left mapping, right mapping, values, left count, right count, scale)."""
    peak = max((abs(v) for v in kerns.values()), default=0)
    scale = max(16, math.ceil(peak * 256 / 127))
    values = {pair: round(px * 256 / scale) for pair, px in kerns.items()}
    values = {pair: v for pair, v in values.items() if v}
    ids = range(1, glyph_count + 1)
    rows = {g: tuple(values.get((g, r), 0) for r in ids) for g in ids}
    cols = {g: tuple(values.get((l, g), 0) for l in ids) for g in ids}
    left_ids, right_ids = {}, {}
    left = [0] + [0 if not any(rows[g]) else left_ids.setdefault(rows[g], len(left_ids) + 1) for g in ids]
    right = [0] + [0 if not any(cols[g]) else right_ids.setdefault(cols[g], len(right_ids) + 1) for g in ids]
    if len(left_ids) > 255 or len(right_ids) > 255:
        raise SystemExit("make_fonts: too many kerning classes")
    # Glyphs of one class kern alike, so any one of them gives the class's values.
    reps_l, reps_r = {}, {}
    for g in ids:
        reps_l.setdefault(left[g], g)
        reps_r.setdefault(right[g], g)
    matrix = [values.get((reps_l[lc], reps_r[rc]), 0)
              for lc in range(1, len(left_ids) + 1) for rc in range(1, len(right_ids) + 1)]
    return left, right, matrix, len(left_ids), len(right_ids), scale


def c_array(kind, name, values, per_line=16):
    lines = [f"static const {kind} {name}[] = {{"]
    for i in range(0, len(values), per_line):
        lines.append("    " + ", ".join(str(v) for v in values[i:i + per_line]) + ",")
    lines.append("};")
    return "\n".join(lines)


def write_font(name, size, glyphs, metrics, kerns, fallback, about):
    """`glyphs`: [(codepoint, adv, w, h, ofs_x, ofs_y, pixels)] sorted by code point."""
    bitmap, dscs = [], ["    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}"]
    for cp, adv, w, h, ox, oy, pixels in glyphs:
        if not (0 <= adv < 4096 and w < 256 and h < 256 and -128 <= ox < 128 and -128 <= oy < 128):
            raise SystemExit(f"make_fonts: U+{cp:04X} does not fit LVGL's glyph descriptor")
        index = len(bitmap)
        packed = pixels + [0] * (len(pixels) % 2)
        bitmap += [f"0x{a << 4 | b:02x}" for a, b in zip(packed[::2], packed[1::2])]
        dscs.append(f"    {{.bitmap_index = {index}, .adv_w = {adv}, .box_w = {w}, .box_h = {h}, "
                    f".ofs_x = {ox}, .ofs_y = {oy}}} /* U+{cp:04X} */")
    if len(bitmap) >= 1 << 20:
        raise SystemExit(f"make_fonts: {name} is larger than 1 MiB")
    # Sparse cmaps: each spans fewer than 65536 code points.
    cmaps, lists, start = [], [], 0
    codes = [g[0] for g in glyphs]
    while start < len(codes):
        end = start
        while end + 1 < len(codes) and codes[end + 1] - codes[start] < 65536:
            end += 1
        n = len(lists)
        lists.append(c_array("uint16_t", f"unicode_list_{n}", [c - codes[start] for c in codes[start:end + 1]]))
        cmaps.append(f"    {{.range_start = {codes[start]}, .range_length = {codes[end] - codes[start] + 1}, "
                     f".glyph_id_start = {start + 1}, .unicode_list = unicode_list_{n}, "
                     f".glyph_id_ofs_list = NULL, .list_length = {end - start + 1}, "
                     f".type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}}")
        start = end + 1
    parts = [
        f"/* Generated by tools/make_fonts.py: do not edit.\n * {name}: {about}, {size} px, 4 bpp. */",
        "#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n#include \"lvgl.h\"\n#else\n#include \"lvgl/lvgl.h\"\n#endif",
        c_array("LV_ATTRIBUTE_LARGE_CONST uint8_t", "glyph_bitmap", bitmap or ["0"]),
        "static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {\n" + ",\n".join(dscs) + "\n};",
        "\n".join(lists),
        "static const lv_font_fmt_txt_cmap_t cmaps[] = {\n" + ",\n".join(cmaps) + "\n};",
    ]
    kern_fields = ".kern_dsc = NULL, .kern_scale = 0, .kern_classes = 0,"
    if kerns:
        left, right, matrix, lcount, rcount, scale = classes(len(glyphs), kerns)
        parts += [c_array("uint8_t", "kern_left_class_mapping", left),
                  c_array("uint8_t", "kern_right_class_mapping", right),
                  c_array("int8_t", "kern_class_values", matrix),
                  "static const lv_font_fmt_txt_kern_classes_t kern_classes = {\n"
                  "    .class_pair_values = kern_class_values,\n"
                  "    .left_class_mapping = kern_left_class_mapping,\n"
                  "    .right_class_mapping = kern_right_class_mapping,\n"
                  f"    .left_class_cnt = {lcount},\n    .right_class_cnt = {rcount},\n}};"]
        kern_fields = f".kern_dsc = &kern_classes, .kern_scale = {scale}, .kern_classes = 1,"
    parts.append("static const lv_font_fmt_txt_dsc_t font_dsc = {\n"
                 "    .glyph_bitmap = glyph_bitmap, .glyph_dsc = glyph_dsc, .cmaps = cmaps,\n"
                 f"    {kern_fields}\n"
                 f"    .cmap_num = {len(cmaps)}, .bpp = 4, .bitmap_format = 0, .stride = 0,\n}};")
    if fallback:
        parts.append(f"extern const lv_font_t {fallback};")
    parts.append(
        f"const lv_font_t {name} = {{\n"
        "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n"
        "    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n"
        f"    .line_height = {metrics['line_height']},\n    .base_line = {metrics['base_line']},\n"
        f"    .cap_height = {metrics['cap_height']},\n    .x_height = {metrics['x_height']},\n"
        "    .subpx = LV_FONT_SUBPX_NONE,\n"
        f"    .underline_position = {metrics['underline_position']},\n"
        f"    .underline_thickness = {metrics['underline_thickness']},\n"
        "    .static_bitmap = 1,\n    .dsc = &font_dsc,\n"
        f"    .fallback = {'&' + fallback if fallback else 'NULL'},\n    .user_data = NULL,\n}};")
    (OUT / f"{name}.c").write_text("\n\n".join(p for p in parts if p) + "\n", encoding="utf-8")


def line_metrics(source, glyphs, size):
    """The line for `glyphs` (what the screen's own texts draw), with the Latin
    face's cap height, x-height and underline. A rarer glyph of the Latin font
    (a phone named "Åsa") may reach a pixel beyond it; LVGL's built-in fonts
    are measured the same way, over ASCII."""
    top = max((g[5] + g[3] for g in glyphs if g[3]), default=size)
    bottom = min((g[5] for g in glyphs if g[3]), default=0)
    os2, post = source.font["OS/2"], source.font["post"]
    return {"line_height": top - bottom, "base_line": -bottom,
            "cap_height": round(os2.sCapHeight * size / source.upem),
            "x_height": round(os2.sxHeight * size / source.upem),
            "underline_position": round(post.underlinePosition * size / source.upem),
            "underline_thickness": max(1, round(post.underlineThickness * size / source.upem))}


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--montserrat", type=Path, required=True, help="Montserrat-Medium.ttf")
    parser.add_argument("--cjk", type=Path, required=True, help="NotoSansCJK-Regular.ttc")
    args = parser.parse_args()
    needs = needed()
    catalogs, _ = gui_text.load()
    latin = Source(args.montserrat)
    faces = {face: Source(args.cjk, family) for face, (family, _) in FACES.items()}
    latin_chars = [chr(c) for low, high in LATIN_RANGES for c in range(low, high + 1) if c in latin.cmap]
    # What the screen's own texts draw with the Latin font: the line is measured over it.
    shown = {chr(c) for c in range(0x20, 0x7F)} | {c for text in catalogs.values() for v in text.values()
                                                   for c in v if in_latin(c)}
    OUT.mkdir(parents=True, exist_ok=True)
    for size in SIZES:
        drawn = latin.at(size)
        glyphs = [(ord(c),) + render(latin, drawn, c, size) for c in latin_chars]
        jp = faces["jp"].at(size)
        glyphs += [(ord(c),) + render(faces["jp"], jp, c, size) for c in NAMES]
        glyphs.sort()
        cjk = {face: [(ord(c),) + render(source, source.at(size), c, size) for c in sorted(needs[face])]
               for face, source in faces.items()}
        measured = [g for g in glyphs if chr(g[0]) in shown or chr(g[0]) in NAMES]
        metrics = line_metrics(latin, measured + cjk["sc"] + cjk["jp"], size)
        kerns = kerning(latin, latin_chars, size)
        index = {g[0]: i + 1 for i, g in enumerate(glyphs)}
        kerns = {(index[ord(a)], index[ord(b)]): v for (a, b), v in kerns.items()}
        write_font(f"nbt_font_latin_{size}", size, glyphs, metrics, kerns, None,
                   "Montserrat Medium (Latin), Noto Sans CJK JP (language names)")
        for face in faces:
            write_font(f"nbt_font_{face}_{size}", size, cjk[face], metrics, {}, f"nbt_font_latin_{size}",
                       f"{FACES[face][0]}, the characters of src/gui/text/{FACES[face][1]}.txt")
    manifest = {
        "generator": "tools/make_fonts.py",
        "sizes": list(SIZES),
        "latin": {"ranges": [[a, b] for a, b in LATIN_RANGES], "symbols": NAMES},
        "sc": "".join(sorted(needs["sc"])),
        "jp": "".join(sorted(needs["jp"])),
        "sources": {
            "Montserrat-Medium.ttf": {"sha256": sha256(args.montserrat), "revision": latin.revision},
            args.cjk.name: {"sha256": sha256(args.cjk),
                            "revision": {face: s.revision for face, s in faces.items()}},
        },
    }
    (OUT / "glyphs.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    print(f"make_fonts: {len(needs['sc'])} Chinese and {len(needs['jp'])} Japanese characters; "
          f"{len(SIZES) * 3} fonts in {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
