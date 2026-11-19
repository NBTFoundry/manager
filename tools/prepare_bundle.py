#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Prepare a new offline managed-tree bundle from a verified initial release.

The output is a workstation artifact, not an on-device installer. It must be
reviewed and installed by the separate setup procedure.
"""

import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

MAX_PACKAGE = 64 * 1024 * 1024 + 4096
TOKEN = re.compile(r"[a-z0-9_-]{1,48}\Z")
COMPAT = re.compile(r"[a-z0-9_.-]{1,48}\Z")


def snapshot(path, limit):
    with path.open("rb") as source:
        data = source.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"{path}: input too large")
    return data


def write_private(path, data, mode):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode)
    with os.fdopen(fd, "wb") as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True)
    parser.add_argument("--compat", required=True)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--public-key", required=True, type=Path)
    parser.add_argument("--worker", required=True, type=Path)
    parser.add_argument("--verifier", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if not TOKEN.fullmatch(args.app) or args.app == "manager" or not COMPAT.fullmatch(args.compat):
        parser.error("invalid application or compatibility token")
    try:
        package = snapshot(args.package, MAX_PACKAGE)
        key = snapshot(args.public_key, 16384)
        worker = snapshot(args.worker, 64 * 1024 * 1024)
        if (len(worker) < 52 or worker[:7] != b"\x7fELF\x01\x01\x01" or
                worker[18:20] != b"\x28\x00" or worker[16:18] not in (b"\x02\x00", b"\x03\x00")):
            raise ValueError("worker must be a 32-bit little-endian ARM ELF")
        with tempfile.TemporaryDirectory() as temporary:
            pkg_path, key_path = Path(temporary) / "package", Path(temporary) / "public"
            write_private(pkg_path, package, 0o600)
            write_private(key_path, key, 0o600)
            result = subprocess.run([str(args.verifier.resolve()), str(pkg_path), str(key_path),
                                     args.app, args.compat], capture_output=True, check=False)
        if result.returncode:
            raise ValueError("initial package verification failed: " + result.stderr.decode("utf-8", "replace").strip())
        fields = dict(line.split("=", 1) for line in result.stdout.decode("ascii").splitlines())
        base = {"app", "version", "sequence", "package_sha256"}
        bridged = base | {"bridge", "bridge-uses", "bridge-optional"}
        if set(fields) not in (base, bridged, bridged | {"bridge-most"}, bridged | {"bridge-files"},
                               bridged | {"bridge-most", "bridge-files"}) or fields["app"] != args.app:
            raise ValueError("unexpected verifier output")
        if fields["package_sha256"] != hashlib.sha256(package).hexdigest():
            raise ValueError("verified package snapshot changed")
        magic, manifest_len, signature_len, payload_len = struct.unpack(">8sIIQ", package[:24])
        offset = 24 + manifest_len + signature_len
        payload = package[offset:]
        if magic != b"NBTMGR1\n" or len(payload) != payload_len:
            raise ValueError("invalid verified package")
        declarations = [line for line in package[24:24 + manifest_len].decode("ascii").splitlines()
                        if line.startswith("setting=")]
        schema = "NBTSETTINGS1\n" + "".join(line + "\n" for line in declarations)
        values = "NBTVALUES1\n"
        for line in declarations:
            setting_key, _, default, _ = line[8:].split("|")
            values += f"{setting_key}={default}\n"
        sequence = int(fields["sequence"])
        level = (4 if "bridge-files" in fields else 3 if "bridge-most" in fields else
                 2 if "bridge" in fields else 1)
        record = (f"NBTSTATE{level}\nsequence={sequence:020d}\nhighwater={sequence:020d}\n"
                  f"version={fields['version']}\nsha256={hashlib.sha256(payload).hexdigest()}\n")

        if "bridge" in fields:
            record += "".join(f"{key}={fields[key]}\n" for key in ("bridge", "bridge-uses", "bridge-optional",
                                                                    "bridge-most", "bridge-files")
                              if key in fields)

        # A new directory is claimed exclusively. We never merge into an existing tree.
        args.output.mkdir(mode=0o700)
        for part in ("data", f"data/{args.app}", "apps", f"apps/{args.app}", f"apps/{args.app}/bin", "state",
                     "state/manager", f"state/{args.app}", "trust", "trust/apps",
                     "trust/keys", "manager"):
            (args.output / part).mkdir(mode=0o700)
        write_private(args.output / f"apps/{args.app}/bin/app", payload, 0o700)
        write_private(args.output / f"state/{args.app}/settings.schema", schema.encode("ascii"), 0o600)
        write_private(args.output / f"data/{args.app}/settings", values.encode("ascii"), 0o600)
        write_private(args.output / f"state/{args.app}/installed", record.encode("ascii"), 0o600)
        write_private(args.output / f"trust/apps/{args.app}.conf", f"NBTAPP1\ncompat={args.compat}\n".encode("ascii"), 0o600)
        write_private(args.output / f"trust/keys/{args.app}-0.pem", key, 0o600)
        write_private(args.output / "manager/nbt-managerd", worker, 0o700)
    except (OSError, ValueError, UnicodeError, struct.error) as error:
        parser.exit(1, f"bundle preparation failed: {error}\n")


if __name__ == "__main__":
    main()
