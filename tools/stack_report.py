#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / os.environ.get("BUILD_DIR", "build")


def main():
    rows = []
    for path in (BUILD / "obj").rglob("*.su"):
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            fields = line.rsplit("\t", 2)
            if len(fields) != 3:
                continue
            try:
                amount = int(fields[1])
            except ValueError:
                continue
            rows.append((amount, fields[0], fields[2], path.relative_to(ROOT)))
    rows.sort(reverse=True, key=lambda row: row[0])
    for amount, function, kind, path in rows[:40]:
        print("%8d %-8s %s (%s)" % (amount, kind, function, path))
    print("stack-report: %d functions" % len(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
