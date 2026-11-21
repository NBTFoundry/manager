#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Build small, reproducible resource databases for the PluginApp."""
import argparse
import zipfile
from pathlib import Path

ASSETS = Path(__file__).resolve().parent / "assets"


LANGUAGES = {
    "de": "Deutsch", "en-UK": "English (UK)", "en-US": "English (US)",
    "es-ES": "Espanol", "es-US": "Espanol (US)", "fr-FR": "Francais",
    "fr-CA": "Francais (CA)", "it": "Italiano", "pt": "Portugues", "ru": "Russian",
}

# Texts 2-4, the App shortcut entries (NbtManager.xml), as the manager's
# settings page names them (src/gui/text, app_shortcut).
SHORTCUT = {
    "de": "App-Kurzbefehl", "en-UK": "App shortcut", "en-US": "App shortcut",
    "es-ES": "Acceso directo", "es-US": "Acceso directo", "fr-FR": "Raccourci appli",
    "fr-CA": "Raccourci appli", "it": "Scorciatoia app", "pt": "Atalho de app",
    "ru": "App shortcut",  # as NBT-AAD's Russian entries: English (Cyrillic not car-tested)
}


def icon(name):
    """The menu icon: the NBTFoundry mark in one colour on transparent, 8-bit
    RGBA (rendered from the brand kit; plugin/assets/55001.png is 63x54,
    55002.png 549x484)."""
    return (ASSETS / name).read_bytes()


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
            shortcuts = "".join(f"{1 + n}={SHORTCUT[lang]} {n}\n" for n in (1, 2, 3))
            write_entry(archive, lang + ".txt",
                        ("100000=" + name + "\n1=App Manager\n" + shortcuts).encode())
    with zipfile.ZipFile(args.output / "NbtManager_ALL_Images.zip", "w") as archive:
        write_entry(archive, "55001.png", icon("55001.png"))
        write_entry(archive, "55002.png", icon("55002.png"))


if __name__ == "__main__":
    main()
