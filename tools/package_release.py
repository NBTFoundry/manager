#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Create a signed manager release; the private key stays on this machine."""

import argparse
import bridge
from contextlib import ExitStack
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

MAX_COMPONENT = 64 * 1024 * 1024
MAX_TOTAL = 128 * 1024 * 1024
TOKEN = re.compile(r"[a-z0-9_.-]{1,48}\Z")
VERSION = re.compile(r"[A-Za-z0-9_.-]{1,48}\Z")
COMPONENTS_V1 = ("launcher", "gui", "worker", "plugin_lib", "plugin_manifest",
                 "plugin_xml", "plugin_text", "plugin_images")
COMPONENTS_V2 = COMPONENTS_V1 + ("capture_lib",)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("launcher", "gui", "worker", "plugin-dir", "key", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--capture-lib", type=Path, help="optional ARM input-capture shim (produces NBTRLS2)")
    parser.add_argument("--version", required=True)
    parser.add_argument("--sequence", required=True, type=int)
    parser.add_argument("--compat", required=True)
    parser.add_argument("--bridge", action="append", help="served major.minor|capabilities (repeat in major order)")
    parser.add_argument("--no-bridge", action="store_true", help="legacy release serving no bridge")
    args = parser.parse_args()
    try:
        if args.no_bridge and args.bridge:
            raise ValueError("--bridge and --no-bridge conflict")
        bridge_text = bridge.provider([] if args.no_bridge else args.bridge if args.bridge is not None else bridge.CURRENT)
    except ValueError as error:
        parser.error(str(error))
    if not VERSION.fullmatch(args.version) or not TOKEN.fullmatch(args.compat) or not 0 < args.sequence < 2**64:
        parser.error("invalid version, compatibility or sequence")
    components = COMPONENTS_V2 if args.capture_lib else COMPONENTS_V1
    magic = b"NBTRLS2\n" if args.capture_lib else b"NBTRLS1\n"
    paths = [args.launcher, args.gui, args.worker, args.plugin_dir / "libNbtManager.so",
             args.plugin_dir / "NbtManager.manifest", args.plugin_dir / "NbtManager.xml",
             args.plugin_dir / "NbtManager_ALL_Texts.zip", args.plugin_dir / "NbtManager_ALL_Images.zip"]
    if args.capture_lib:
        paths.append(args.capture_lib)
    try:
        with ExitStack() as stack:
            snapshots, details, total = [], [], 0
            for index, path in enumerate(paths):
                source = stack.enter_context(path.open("rb"))
                snapshot = stack.enter_context(tempfile.TemporaryFile())
                digest, size, first = hashlib.sha256(), 0, b""
                while True:
                    block = source.read(65536)
                    if not block:
                        break
                    size += len(block)
                    if size > MAX_COMPONENT or total + size > MAX_TOTAL:
                        raise ValueError("release exceeds size limit")
                    first = (first + block)[:52]
                    digest.update(block)
                    snapshot.write(block)
                if not size:
                    raise ValueError(f"empty component: {components[index]}")
                if index < 4 or index == 8:
                    expected_types = (b"\x03\x00",) if index in (3, 8) else (b"\x02\x00", b"\x03\x00")
                    if len(first) < 52 or first[:7] != b"\x7fELF\x01\x01\x01" or first[18:20] != b"\x28\x00" or first[16:18] not in expected_types:
                        raise ValueError(f"invalid ARM ELF: {components[index]}")
                total += size
                snapshots.append(snapshot)
                details.append((size, digest.hexdigest()))
            manifest = (f"version={args.version}\nsequence={args.sequence}\n"
                        f"platform=qnx6.5-armv7le\ncompat={args.compat}\n" + bridge_text)
            for name, (size, digest) in zip(components, details):
                manifest += f"{name}_size={size}\n{name}_sha256={digest}\n"
            manifest = manifest.encode("ascii")
            if len(manifest) > 4096:
                raise ValueError("manifest exceeds 4096 bytes")
            header = struct.pack(">8sII", magic, len(manifest), 384)
            signature = subprocess.run(["openssl", "dgst", "-sha256", "-sign", os.fspath(args.key)],
                                       input=header + manifest, capture_output=True, check=False)
            if signature.returncode or len(signature.stdout) != 384:
                raise ValueError("signing failed; provide an RSA-3072 private key")
            created = False
            try:
                with args.output.open("xb") as output:
                    created = True
                    output.write(header + manifest + signature.stdout)
                    for snapshot in snapshots:
                        snapshot.seek(0)
                        while block := snapshot.read(65536):
                            output.write(block)
                    output.flush()
                    os.fsync(output.fileno())
            except BaseException:
                if created:
                    args.output.unlink(missing_ok=True)
                raise
    except (OSError, ValueError) as error:
        parser.exit(1, f"release packaging failed: {error}\n")


if __name__ == "__main__":
    main()
