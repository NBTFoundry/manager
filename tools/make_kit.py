#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""`make kit`: the whole kit procedure in one command.

1. Refuses a working tree with uncommitted changes (a kit comes from a commit).
2. In the QNX 6.5 environment: `make qnx-check stack-report`, then
   tools/build_windows_kit.py with the capture shim just built.
3. Copies the kit to the exchange folder NBT_KIT_DIR as the zip plus the
   unzipped folder, refusing to replace either, and checks every file there
   against SHA256SUMS.txt. Without NBT_KIT_DIR the kit is not copied.
4. Prints the kit's commit and the SHA-256 of the kit, release and shim.

Environment: QNX_ENV (the QNX 6.5 environment script), QNX_SCREEN_LIBDIR,
RHMI_LIBDIR, NBT_KIT_DIR (optional).
--no-copy builds and checks without copying.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
def setting(name):
    value = os.environ.get(name)
    if not value:
        raise SystemExit(f"make kit: set {name}")
    return value


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def clean_tree():
    status = subprocess.run(["git", "-C", ROOT, "status", "--porcelain"], check=True,
                            capture_output=True, text=True).stdout
    return not status.strip()


def build():
    env = dict(os.environ, QNX_SCREEN_LIBDIR=setting("QNX_SCREEN_LIBDIR"),
               RHMI_LIBDIR=setting("RHMI_LIBDIR"),
               MANAGER_CAPTURE_LIB=str(ROOT / "build/libnbt-capture.so"))
    jobs = os.cpu_count() or 1
    script = (f"source {shlex.quote(setting('QNX_ENV'))} >/dev/null && "
              f"nice make -j {jobs} qnx-check stack-report && "
              f"{shlex.quote(sys.executable)} tools/build_windows_kit.py")
    subprocess.run(["bash", "-c", script], cwd=ROOT, env=env, check=True)


def built_kit():
    sequence = (Path.home() / ".local/share/nbt-manager/keys/manager-sequence").read_text().strip()
    version = f"dev{int(sequence)}"
    archive = ROOT / f"dist/nbt-manager-windows-kit-{version}.zip"
    if not archive.is_file():
        raise SystemExit(f"make_kit: {archive} was not built")
    return version, archive


def check_sums(folder):
    for line in (folder / "SHA256SUMS.txt").read_text().splitlines():
        if not line.strip():
            continue
        digest, name = line.split("  ", 1)
        if sha256(folder / name) != digest:
            raise SystemExit(f"make_kit: {folder / name} does not match SHA256SUMS.txt")


def copy(version, archive, destination):
    folder = destination / f"nbt-manager-{version}"
    target = destination / archive.name
    if folder.exists() or target.exists():
        raise SystemExit(f"make_kit: {folder} or {target} already exists; nothing copied")
    with tempfile.TemporaryDirectory(dir=ROOT / "build") as staging:
        with zipfile.ZipFile(archive) as kit:
            kit.extractall(staging)
        unpacked = Path(staging) / f"nbt-manager-{version}"
        if not unpacked.is_dir():
            raise SystemExit(f"make_kit: the zip has no nbt-manager-{version} folder")
        shutil.copy2(archive, target)
        shutil.copytree(unpacked, folder)  # flattened: the folder itself, no nesting
    if sha256(target) != sha256(archive):
        raise SystemExit(f"make_kit: {target} differs from {archive}")
    check_sums(folder)
    return folder


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--no-copy", action="store_true", help="build and check only")
    args = parser.parse_args()
    if not clean_tree():
        raise SystemExit("make_kit: commit first: the working tree has uncommitted changes")
    build()
    version, archive = built_kit()
    if args.no_copy or not os.environ.get("NBT_KIT_DIR"):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as staging:
            with zipfile.ZipFile(archive) as kit:
                kit.extractall(staging)
            check_sums(Path(staging) / f"nbt-manager-{version}")
        where = "not copied (--no-copy or no NBT_KIT_DIR)"
    else:
        where = str(copy(version, archive, Path(setting("NBT_KIT_DIR"))))
    release = ROOT / "build/kit-release/manager.nbtr"
    commit = subprocess.run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"], check=True,
                            capture_output=True, text=True).stdout.strip()
    print(f"\nKit {version} from {commit}, every checksum verified; {where}")
    print(f"  kit       {archive.name}  {sha256(archive)}")
    print(f"  release   {sha256(release) if release.is_file() else 'not found'}")
    print(f"  shim      {sha256(ROOT / 'build/libnbt-capture.so')}")


if __name__ == "__main__":
    main()
