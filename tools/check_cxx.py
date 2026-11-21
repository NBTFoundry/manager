#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
PLUGIN_FILES = [ROOT / "plugin/plugin.cpp", ROOT / "src/gui/hmi_hold.cpp", ROOT / "src/log/log.cpp",
                ROOT / "plugin/car_language.cpp", ROOT / "src/gui/car_language.cpp",
                ROOT / "src/gui/app_hold.cpp", ROOT / "src/gui/car_data_relay.cpp", ROOT / "plugin/car_data.cpp",
                ROOT / "src/sdk/bridge_hold.cpp", ROOT / "src/model/cds_policy.cpp", ROOT / "src/model/net_policy.cpp",
                ROOT / "plugin/shortcut.cpp", ROOT / "src/gui/shortcut_record.cpp"]
STL = re.compile(r"\bstd::|#\s*include\s*<(?:string|vector|map|array|memory|algorithm|type_traits|utility)>")
FORBIDDEN = re.compile(r"#\s*include\s*<(?:thread|mutex|atomic)>|\bthread_local\b")
RAW_HEAP = re.compile(r"(?<![\w:])delete\s+|\bnew\s+(?!\()")
MEM_OBJECT = re.compile(r"\b(?:memset|memcpy|memmove)\s*\([^;]*(?:std::|this\s*[),])")


def sources():
    for directory in ("src", "apps", "plugin", "tests", "tools"):
        yield from (ROOT / directory).rglob("*.cpp")
        yield from (ROOT / directory).rglob("*.hpp")


STRING = re.compile(r'"(?:\\.|[^"\\\n])*"')


def without_comments(text):
    """Code only: no comments, and string literals emptied (a message such as
    "a new application" is not a new-expression)."""
    text = STRING.sub('""', text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def main():
    errors = []
    for path in sources():
        text = without_comments(path.read_text(encoding="utf-8"))
        relative = path.relative_to(ROOT)
        if FORBIDDEN.search(text):
            errors.append(str(relative) + ": forbidden thread facility")
        if RAW_HEAP.search(text):
            errors.append(str(relative) + ": raw heap ownership; use a smart pointer or placement storage")
        if MEM_OBJECT.search(text):
            errors.append(str(relative) + ": byte operation on a C++ object")
    for path in PLUGIN_FILES:
        text = without_comments(path.read_text(encoding="utf-8"))
        if STL.search(text):
            errors.append(str(path.relative_to(ROOT)) + ": STL is not permitted in plugin-linked code")
    for path in (ROOT / "include").rglob("*.hpp"):
        text = path.read_text(encoding="utf-8")
        for match in re.finditer(r"(?:class|struct)\s+\w*Secure\w*\s*\{(.*?)\n\};", text, re.DOTALL):
            if "std::string" in match.group(1):
                errors.append(str(path.relative_to(ROOT)) + ": secret type contains std::string")
    if errors:
        print("\n".join("check-cxx: " + error for error in errors), file=sys.stderr)
        return 1
    print("check-cxx: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
