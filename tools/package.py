#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Create an NBT Manager v1 single-executable signed package."""

import argparse
import bridge
import hashlib
import os
from pathlib import Path
import re
import secrets
import struct
import subprocess
import tempfile

MAX_PAYLOAD = 64 * 1024 * 1024
TOKEN = re.compile(r"[a-z0-9_.-]{1,48}\Z")
APP_TOKEN = re.compile(r"[a-z0-9_-]{1,48}\Z")
VERSION = re.compile(r"[A-Za-z0-9_.-]{1,48}\Z")
SETTING_KEY = re.compile(r"[a-z][a-z0-9_]{0,15}\Z")
SETTING_LABEL = re.compile(r"[ -{}~]{1,24}\Z")
SETTING_CHOICE = re.compile(r"[a-z0-9_-]{1,12}\Z")
# Information for people (PROTOCOL.md, "Package information"): printable ASCII.
PROPERTY_LABEL = re.compile(r"[ -{}~]{1,24}\Z")
PROPERTY_VALUE = re.compile(r"[ -{}~]{1,64}\Z")
TEXT_LINE = re.compile(r"[ -~]{0,120}\Z")


def file_argument(line):
    """One argument per line of an @file; blank lines and # comments are skipped."""
    return [] if not line.strip() or line.lstrip().startswith("#") else [line]


NOTE_READ = 65536


def nbt_note(data):
    """(abi_major, abi_minor, mode) of the SDK's NBT ABI note in an ELF32 ARM
    image, the mode matching the ELF type (a module ET_DYN, a guest ET_EXEC),
    or None: the SDK's rules (tools/nbt_elf.py, src/payload.c) and the
    manager's (src/package/payload_note.cpp)."""
    if len(data) < 52 or data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1 or data[18:20] != b"\x28\x00":
        return None
    elf_type = struct.unpack_from("<H", data, 16)[0]
    phoff, = struct.unpack_from("<I", data, 28)
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    if phentsize < 32 or not 0 < phnum <= 64 or phoff + phnum * phentsize > len(data):
        return None
    for i in range(phnum):
        p_type, p_offset, _, _, p_filesz = struct.unpack_from("<IIIII", data, phoff + i * phentsize)
        end = p_offset + p_filesz
        if p_type != 4 or end > len(data):
            continue
        pos = p_offset
        while pos + 12 <= end:
            namesz, descsz, ntype = struct.unpack_from("<III", data, pos)
            if namesz > 64 or descsz > 1024:
                break
            desc = pos + 12 + ((namesz + 3) & ~3)
            following = desc + ((descsz + 3) & ~3)
            if following > end:
                break
            if namesz == 4 and data[pos + 12:pos + 16] == b"NBT\0" and ntype == 1 and descsz >= 12:
                note = struct.unpack_from("<III", data, desc)
                return note if (note[2], elf_type) in ((1, 3), (2, 2)) else None
            pos = following
    return None


def main():
    # @FILE reads arguments from FILE, one per line: an application keeps its
    # package description (app, compatibility, bridge, settings) in one file that
    # its Makefile and its release workflow both pass.
    parser = argparse.ArgumentParser(description=__doc__, fromfile_prefix_chars="@")
    parser.convert_arg_line_to_args = file_argument
    parser.add_argument("--app", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--sequence", required=True, type=int)
    parser.add_argument("--compat", required=True)
    parser.add_argument("--key", required=True, type=Path, help="private RSA-3072 PEM key")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--setting", action="append", default=[],
                        help="declare a setting: key|label|default|choice,choice (see src/sdk/settings.cpp)")
    parser.add_argument("--bridge", help="minimum bridge major.minor")
    parser.add_argument("--bridge-uses", default="", help="required comma-separated capabilities")
    parser.add_argument("--bridge-optional", default="", help="optional comma-separated capabilities")
    parser.add_argument("--bridge-most", default="",
                        help="MOST policy, required with capability most: rx:FB.II.FFF,tx:...,ctl:FB.II,sub:FB.II.FFF")
    parser.add_argument("--bridge-files", default="",
                        help="files policy, required with capability files: "
                             "ext,ext|largest file MiB|quota MiB[|folders=ext,ext]")
    parser.add_argument("--bridge-cds", default="",
                        help="car-data policy, required with capability car-data: name.name,name.name")
    parser.add_argument("--bridge-net", default="",
                        help="network policy, required with capability net: tcp:<port>@uap0+wfd0,...")
    parser.add_argument("--bridge-diag", default="",
                        help="diagnostics policy, required with capability diag: 12:read+codes+clear,...")
    parser.add_argument("--property", action="append", default=[],
                        help="information shown before an install: label|value (at most 8)")
    parser.add_argument("--text", action="append", default=[],
                        help="a line of text shown before an install (at most 20; empty separates paragraphs)")
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    if not APP_TOKEN.fullmatch(args.app) or not TOKEN.fullmatch(args.compat):
        parser.error("app and compatibility must be lowercase tokens (maximum 48 characters)")
    if not VERSION.fullmatch(args.version) or not 0 < args.sequence < 2**64:
        parser.error("invalid version or release sequence")
    try:
        bridge_text = bridge.requirement(args.bridge, args.bridge_uses, args.bridge_optional, args.bridge_most,
                                         args.bridge_files, args.bridge_cds, args.bridge_net,
                                         args.bridge_diag)
    except ValueError as error:
        parser.error(str(error))
    keys = set()
    if len(args.setting) > 12:
        parser.error("at most 12 settings")
    for setting in args.setting:
        parts = setting.split("|")
        choices = parts[3].split(",") if len(parts) == 4 else []
        if (len(parts) != 4 or not SETTING_KEY.fullmatch(parts[0]) or parts[0] in keys or
                not SETTING_LABEL.fullmatch(parts[1]) or parts[1] != parts[1].strip() or
                not 1 <= len(choices) <= 6 or len(set(choices)) != len(choices) or
                not all(SETTING_CHOICE.fullmatch(c) for c in choices) or parts[2] not in choices):
            parser.error(f"invalid setting: {setting}")
        keys.add(parts[0])
    if len(args.property) > 8:
        parser.error("at most 8 properties")
    for prop in args.property:
        label, bar, value = prop.partition("|")
        if (not bar or not PROPERTY_LABEL.fullmatch(label) or not PROPERTY_VALUE.fullmatch(value) or
                label != label.strip() or value != value.strip()):
            parser.error(f"invalid property: {prop}")
    if len(args.text) > 20 or not all(TEXT_LINE.fullmatch(line) for line in args.text):
        parser.error("text: at most 20 lines of at most 120 printable ASCII characters")
    if args.output.exists():
        parser.error("output already exists")

    # Snapshot the source once: the digest and emitted payload must describe the same bytes.
    with tempfile.TemporaryFile() as payload, args.executable.open("rb") as source:
        digest = hashlib.sha256()
        size = 0
        first = b""
        head = b""  # notes sit near the start
        while True:
            block = source.read(65536)
            if not block:
                break
            size += len(block)
            if size > MAX_PAYLOAD:
                parser.error("executable exceeds 64 MiB")
            if len(first) < 52:
                first = (first + block)[:52]
            if len(head) < NOTE_READ:
                head = (head + block)[:NOTE_READ]
            digest.update(block)
            payload.write(block)
        if (len(first) < 52 or first[:7] != b"\x7fELF\x01\x01\x01" or
                first[18:20] != b"\x28\x00" or first[16:18] not in (b"\x02\x00", b"\x03\x00")):
            parser.error("executable must be a 32-bit little-endian ARM ELF")
        # Built without the QNX SDP: only a manager with the runner runs it.
        runner = "runner" in (args.bridge_uses or "").split(",")
        note = nbt_note(head)
        if note and not runner:
            parser.error("the payload carries the NBT ABI note (a %s): declare --bridge 1.7 --bridge-uses runner"
                         % ("module" if note[2] == 1 else "guest"))
        if runner and not note:
            parser.error("runner declared, but the payload has no NBT ABI note (build it as a module or guest)")
        manifest = (f"app={args.app}\npackage={secrets.token_hex(16)}\n"
                    f"version={args.version}\nsequence={args.sequence}\n"
                    f"platform=qnx6.5-armv7le\ncompat={args.compat}\n"
                    f"size={size}\nsha256={digest.hexdigest()}\n" + bridge_text +
                    "".join(f"setting={setting}\n" for setting in args.setting) +
                    "".join(f"property={prop}\n" for prop in args.property) +
                    "".join(f"text={line}\n" for line in args.text)).encode("ascii")
        if len(manifest) > 2048:
            parser.error("manifest exceeds 2048 bytes")
        header = struct.pack(">8sIIQ", b"NBTMGR1\n", len(manifest), 384, size)
        signed = subprocess.run(["openssl", "dgst", "-sha256", "-sign", os.fspath(args.key)],
                                input=header + manifest, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, check=False)
        if signed.returncode or len(signed.stdout) != 384:
            parser.error("signing failed; provide an RSA-3072 private key")
        created = False
        try:
            with args.output.open("xb") as output:
                created = True
                output.write(header + manifest + signed.stdout)
                payload.seek(0)
                while True:
                    block = payload.read(65536)
                    if not block:
                        break
                    output.write(block)
                output.flush()
                os.fsync(output.fileno())
        except BaseException:
            if created:
                args.output.unlink(missing_ok=True)
            raise


if __name__ == "__main__":
    main()
