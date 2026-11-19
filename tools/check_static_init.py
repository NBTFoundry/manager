#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / os.environ.get("BUILD_DIR", "build")
FORBIDDEN = {"_GLOBAL__sub_I", "__static_initialization_and_destruction"}


def main():
    failures = []
    objects = sorted((BUILD / "obj").rglob("*.o"))
    for path in objects:
        result = subprocess.run(["nm", "-C", str(path)], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        for line in result.stdout.splitlines():
            if any(name in line for name in FORBIDDEN):
                failures.append(str(path.relative_to(ROOT)) + ": " + line.strip())
    if failures:
        print("check-static-init: non-POD static initialization detected", file=sys.stderr)
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("check-static-init: OK (%d objects)" % len(objects))
    return 0


if __name__ == "__main__":
    sys.exit(main())
