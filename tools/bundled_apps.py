#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The manager's bundled applications (docs/self-test.md) as packages signed
with the manager key, for a release's named components: prints one
--component NAME=PATH argument per line for tools/package_release.py."""

import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
# (component, form, program in the build folder), as nbt/bundled.h lists them.
BUNDLED = (("selftest_native", "native", "selftest-native"),
           ("selftest_guest", "guest", "selftest-guest.guest"),
           ("selftest", "module", "selftest-module.so"))


def package(programs, version, sequence, key, output):
    """Signs each bundled application into `output`; their component arguments."""
    arguments = []
    output.mkdir(parents=True, exist_ok=True)
    for name, form, binary in BUNDLED:
        program = programs / binary
        if not program.is_file():
            raise ValueError(f"missing {program}: make selftest-apps")
        signed = output / f"{name}.nbt"
        signed.unlink(missing_ok=True)
        subprocess.run([sys.executable, str(ROOT / "tools/package.py"),
                        f"@{ROOT / 'apps/selftest/common.args'}",
                        f"@{ROOT / 'apps/selftest' / (form + '.args')}",
                        "--version", version, "--sequence", str(sequence), "--key", str(key),
                        "--output", str(signed), str(program)],
                       check=True, stdout=subprocess.DEVNULL)
        arguments.append(f"{name}={signed}")
    return arguments


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--programs", required=True, type=Path, help="build/selftest")
    parser.add_argument("--version", required=True)
    parser.add_argument("--sequence", required=True, type=int)
    parser.add_argument("--key", required=True, type=Path, help="the manager signing key")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        for argument in package(args.programs, args.version, args.sequence, args.key, args.output):
            print(f"--component={argument}")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"bundled applications: {error}\n")


if __name__ == "__main__":
    main()
