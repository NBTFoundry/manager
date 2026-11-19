#!/usr/bin/env python3
"""Build small, reproducible resource databases for the PluginApp."""
import argparse
import struct
import zipfile
import zlib
from pathlib import Path


LANGUAGES = {
    "de": "Deutsch", "en-UK": "English (UK)", "en-US": "English (US)",
    "es-ES": "Espanol", "es-US": "Espanol (US)", "fr-FR": "Francais",
    "fr-CA": "Francais (CA)", "it": "Italiano", "pt": "Portugues", "ru": "Russian",
}


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def icon(width, height):
    # A simple, original grid of four squares on a transparent background.
    rows = bytearray()
    side = min(width, height)
    left, top = (width - side) // 2, (height - side) // 2
    for y in range(height):
        rows.append(0)  # PNG filter: none
        for x in range(width):
            nx, ny = x - left, y - top
            square = ((side * 12 // 100 <= nx < side * 45 // 100 or
                       side * 55 // 100 <= nx < side * 88 // 100) and
                      (side * 12 // 100 <= ny < side * 45 // 100 or
                       side * 55 // 100 <= ny < side * 88 // 100))
            rows.extend((232, 238, 244, 255) if square else (0, 0, 0, 0))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
            chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))


def write_entry(archive, name, data):
    info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output / "NbtManager_ALL_Texts.zip", "w") as archive:
        for lang, name in LANGUAGES.items():
            write_entry(archive, lang + ".txt", ("100000=" + name + "\n1=App Manager\n").encode())
    with zipfile.ZipFile(args.output / "NbtManager_ALL_Images.zip", "w") as archive:
        write_entry(archive, "55001.png", icon(63, 54))
        write_entry(archive, "55002.png", icon(549, 484))


if __name__ == "__main__":
    main()
