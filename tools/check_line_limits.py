#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
WARN_LINES = 300
MAX_LINES = 330
EXEMPT = {ROOT / "src/gui/qr_code.cpp"}
PROD_DIRS = (ROOT / "src", ROOT / "apps", ROOT / "plugin")


def is_production_cpp(path: Path) -> bool:
    if path.suffix != ".cpp" or path in EXEMPT:
        return False
    return any(prod_dir in path.parents for prod_dir in PROD_DIRS)


def candidate_files(args):
    if args:
        for arg in args:
            path = (ROOT / arg).resolve() if not Path(arg).is_absolute() else Path(arg).resolve()
            if path.is_file() and is_production_cpp(path):
                yield path
    else:
        for prod_dir in PROD_DIRS:
            for path in sorted(prod_dir.rglob("*.cpp")):
                if is_production_cpp(path.resolve()):
                    yield path.resolve()


def main():
    failures = []
    checked = 0
    for path in candidate_files(sys.argv[1:]):
        checked += 1
        lines = len(path.read_text(encoding="utf-8").splitlines())
        rel = path.relative_to(ROOT)
        if lines > MAX_LINES:
            failures.append(f"{rel}: {lines} lines (limit {MAX_LINES})")
        elif lines > WARN_LINES:
            print(f"check-line-limits: warning: {rel}: {lines} lines (target {WARN_LINES})",
                  file=sys.stderr)
    if failures:
        for failure in failures:
            print(f"check-line-limits: {failure}", file=sys.stderr)
        return 1
    print(f"check-line-limits: OK ({checked} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
