#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Lists the files an strace'd build used, grouped by root (ci/qnx/trace.sh).

usage: trace_files.py TRACE ROOT... -> for each ROOT, the relative paths of the
files opened, executed or stat'ed successfully, plus the targets of any
symlinks among them; then the 32-bit system libraries the host tools loaded.
With --list ROOT, prints only that root's paths (ci/qnx/files.txt)."""
import os, re, sys
from pathlib import Path

args = sys.argv[1:]
only = None
if args[0] == "--list":
    only, args = Path(args[1]).resolve(), args[2:]
trace, roots = args[0], [Path(r).resolve() for r in args[1:]]
if only and only not in roots:
    roots.append(only)
start = re.compile(r'^(\d+) +(\w+)\((.*)$')
resumed = re.compile(r'^(\d+) +<\.\.\. (\w+) resumed>(.*)$')
pending = {}
calls = []
for line in open(trace, errors="replace"):
    line = line.rstrip("\n")
    m = resumed.match(line)
    if m:
        pid, name, rest = m.groups()
        head = pending.pop((pid, name), None)
        if head is not None:
            calls.append((name, head + rest))
        continue
    m = start.match(line)
    if not m:
        continue
    pid, name, rest = m.groups()
    if rest.endswith("<unfinished ...>"):
        pending[(pid, name)] = rest[:-len("<unfinished ...>")]
    else:
        calls.append((name, rest))

path_arg = re.compile(r'^(?:AT_FDCWD, |\d+, )?"([^"]+)"')
result = re.compile(r'\) += (-?\d+|0x[0-9a-f]+)')
used = {root: set() for root in roots}
system32 = set()


def add(path):
    for root in roots:
        try:
            rel = path.relative_to(root)
        except ValueError:
            continue
        if path.is_symlink():
            used[root].add(rel)
            target = Path(os.path.normpath(path.parent / os.readlink(path)))
            add(target)
        elif path.is_file():
            used[root].add(rel)
        return True
    return False


for name, body in calls:
    m, r = path_arg.match(body), result.search(body)
    if not m or not r or r.group(1).startswith("-"):
        continue
    path = Path(os.path.normpath(m.group(1)))
    if not path.is_absolute():
        continue
    if not add(path):
        s = str(path)
        if re.search(r"/(lib32|i386-linux-gnu)/|/ld-linux\.so\.2", s) and path.is_file():
            system32.add(s)
if only:
    for f in sorted(used[only]):
        print(f)
    sys.exit()
for root in roots:
    files = sorted(used[root])
    size = sum((root / f).stat().st_size for f in files if not (root / f).is_symlink())
    print(f"== {root}: {len(files)} files, {size / 1e6:.1f} MB")
    for f in files:
        print(f"   {f}")
print("== 32-bit system libraries")
for s in sorted(system32):
    print(f"   {s}")
