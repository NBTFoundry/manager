#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / os.environ.get("BUILD_DIR", "build")
READELF = os.environ.get("QNX_READELF", "ntoarmv7-readelf")
NM = os.environ.get("QNX_NM", "ntoarmv7-nm")
SIZE = os.environ.get("QNX_SIZE", "ntoarmv7-size")
EXECUTABLES = [
    "nbt-managerd-qnx",
    "test-v1-qnx",
    "test-v2-qnx",
    "test-v3-broken-qnx",
    "nbt-manager-launcher",
    "nbt-manager-gui",
    "nbt-verify-release-qnx",
    "most-monitor-qnx",
]
ALLOWED_NEEDED = {
    "libc.so.3",
    "libm.so.2",
    "libsocket.so.3",
    "libscreen.so.1",
    "libcrypto.so.1",
    "libcrypto.so.2",
    "libstdc++.so.6",
    # zlib, for game archives (src/storage/zip_archive.cpp): NBTCarHU and 22
    # iDrive libraries need it on NBTevo_Y21432J (DT_NEEDED).
    "libz.so.2",
}
# nbt-runner (the SDK's, NBTRLS3 component runner) draws for the applications.
RUNNER_NEEDED = ALLOWED_NEEDED | {"libEGL.so.1", "libGLESv2.so.1"}
ALLOWED_REMOVED_PLUGIN_IMPORTS = {
    "_ZnwjRKSt9nothrow_t", "_ZSt7nothrow", "__aeabi_unwind_cpp_pr0",
    "__aeabi_unwind_cpp_pr1", "__cxa_end_cleanup", "__gxx_personality_v0",
}
TOOLCHAIN_EXPORTS = {
    "__bss_end__", "__bss_start", "__bss_start__", "__data_start", "__end__",
    "__exidx_end", "__exidx_start", "_bss_end__", "_btext", "_edata", "_end",
    "_fini", "_init", "_stack",
}
HEAP_IMPORT = re.compile(r"^(?:_Zn[aw].*|malloc|calloc)$")


def run(*args):
    return subprocess.run(args, cwd=ROOT, check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def needed(path):
    return re.findall(r"\(NEEDED\).*\[([^]]+)\]", run(READELF, "-d", str(path)))


def symbols(path, option):
    result = set()
    for line in run(NM, "-D", option, str(path)).splitlines():
        fields = line.split()
        if fields:
            result.add(fields[-1])
    return result


def baseline(name):
    path = ROOT / "tests" / "baseline" / name
    return path.read_text(encoding="utf-8")


def stale_objects():
    """QNX objects with no dependency file, or older than a file they include:
    dev31 shipped a GUI built from objects with two layouts of one class."""
    problems = []
    for obj in sorted((BUILD / "obj" / "qnx").rglob("*.o")):
        deps = obj.with_suffix(".d")
        if not deps.is_file():
            problems.append("%s: no dependency file" % obj.relative_to(BUILD))
            continue
        text = deps.read_text(encoding="utf-8").replace("\\\n", " ")
        first = text.split("\n", 1)[0]
        built = obj.stat().st_mtime
        for item in first.split(":", 1)[1].split() if ":" in first else []:
            path = ROOT / item
            if path.is_file() and path.stat().st_mtime > built:
                problems.append("%s: older than %s" % (obj.relative_to(BUILD), item))
                break
    return problems


def main():
    errors = stale_objects()
    imported = set()
    for name in EXECUTABLES:
        path = BUILD / name
        if not path.is_file():
            errors.append("missing " + str(path))
            continue
        header = run(READELF, "-h", str(path))
        match = re.search(r"^\s*Type:\s+(\S+)", header, re.MULTILINE)
        if not match or match.group(1) != "EXEC":
            errors.append(name + ": expected ET_EXEC")
        libraries = set(needed(path))
        unexpected = libraries - ALLOWED_NEEDED
        if unexpected:
            errors.append(name + ": unexpected NEEDED " + ", ".join(sorted(unexpected)))
        if any(item.startswith("libcpp") for item in libraries):
            errors.append(name + ": links a libcpp runtime")
        if "libstdc++.so.6" not in libraries:
            errors.append(name + ": does not dynamically link libstdc++.so.6")
        if b"NBT_TEST_" in path.read_bytes():
            errors.append(name + ": contains NBT_TEST_ strings")
        imported |= symbols(path, "--undefined-only")

    runner = BUILD / "nbt-runner"
    if not runner.is_file():
        errors.append("missing " + str(runner))
    else:
        header = run(READELF, "-h", str(runner))
        match = re.search(r"^\s*Type:\s+(\S+)", header, re.MULTILINE)
        if not match or match.group(1) != "EXEC":
            errors.append("nbt-runner: expected ET_EXEC")
        unexpected = set(needed(runner)) - RUNNER_NEEDED
        if unexpected:
            errors.append("nbt-runner: unexpected NEEDED " + ", ".join(sorted(unexpected)))
        imported |= symbols(runner, "--undefined-only")

    plugin = BUILD / "NbtManager" / "libNbtManager.so"
    if not plugin.is_file():
        errors.append("missing " + str(plugin))
    else:
        expected_needed = set(re.findall(r"\(NEEDED\).*\[([^]]+)\]", baseline("plugin-needed.txt")))
        if set(needed(plugin)) != expected_needed:
            errors.append("plugin NEEDED list differs from tests/baseline/plugin-needed.txt")
        exports = symbols(plugin, "--defined-only") - TOOLCHAIN_EXPORTS
        if exports != {"pluginAppInfo"}:
            errors.append("plugin exports differ from {pluginAppInfo}: " + ", ".join(sorted(exports)))
        expected_undefined = {line.split()[-1] for line in baseline("plugin-undefined.txt").splitlines() if line.split()}
        undefined = symbols(plugin, "--undefined-only")
        added = undefined - expected_undefined
        removed = expected_undefined - undefined
        if added or removed - ALLOWED_REMOVED_PLUGIN_IMPORTS:
            errors.append("plugin imports changed; added=[%s], removed=[%s]" %
                          (", ".join(sorted(added)), ", ".join(sorted(removed))))
        heap = sorted(item for item in undefined if HEAP_IMPORT.match(item))
        if heap:
            errors.append("plugin imports heap allocation: " + ", ".join(heap))
        current_size = run(SIZE, str(plugin)).splitlines()[-1].split()
        old_size = baseline("plugin-size.txt").splitlines()[-1].split()
        print("plugin size text/data/bss: %s/%s/%s (baseline %s/%s/%s)" %
              (current_size[0], current_size[1], current_size[2],
               old_size[0], old_size[1], old_size[2]))

    capture = BUILD / "libnbt-capture.so"
    if not capture.is_file():
        errors.append("missing " + str(capture))
    else:
        header = run(READELF, "-h", str(capture))
        match = re.search(r"^\s*Type:\s+(\S+)", header, re.MULTILINE)
        if not match or match.group(1) != "DYN":
            errors.append("libnbt-capture.so: expected ET_DYN")
        if needed(capture) != ["libc.so.3"]:
            errors.append("libnbt-capture.so NEEDED differs from [libc.so.3]: " +
                          ", ".join(needed(capture)))
        cap_exports = symbols(capture, "--defined-only") - TOOLCHAIN_EXPORTS
        # The interposed call, and the install's self-test entry (refused inside HmiMain3D).
        expected_exports = {"DSIClientDataResponse", "nbt_capture_selftest_begin"}
        if cap_exports != expected_exports:
            errors.append("libnbt-capture.so exports differ from " +
                          "{DSIClientDataResponse, nbt_capture_selftest_begin}: " +
                          ", ".join(sorted(cap_exports)))
        cap_undef = symbols(capture, "--undefined-only")
        cap_heap = sorted(item for item in cap_undef if HEAP_IMPORT.match(item))
        if cap_heap:
            errors.append("libnbt-capture.so imports heap allocation: " + ", ".join(cap_heap))
        if b"NBT_TEST_" in capture.read_bytes():
            errors.append("libnbt-capture.so contains NBT_TEST_ strings")

    # QNX 6.5's C++ runtime, from the SDK the environment script sets up.
    default_runtime = Path(os.environ.get("QNX_TARGET", "/nonexistent")) / "armle-v7/lib/libstdc++.so.6.0.13"
    old_runtime = Path(os.environ.get("QNX_OLD_LIBSTDCXX", str(default_runtime)))
    if old_runtime.is_file():
        available = symbols(old_runtime, "--defined-only")
        missing = sorted(item for item in imported if item.startswith("_Z") and item not in available)
        print("libstdc++ 6.0.13 compatibility: %d imported C++ symbols absent" % len(missing))
        for item in missing:
            print("  " + item)
    else:
        print("libstdc++ 6.0.13 comparison skipped: runtime not present at " + str(old_runtime))

    if errors:
        for error in errors:
            print("qnx-check: " + error, file=sys.stderr)
        return 1
    print("qnx-check: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
