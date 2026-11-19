#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Git pre-commit, formatting, and standalone verification runner."""
import fnmatch
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
CXX_DIRS = ("include", "src", "apps", "plugin", "tests", "tools")
CXX_SUFFIXES = {".c", ".cpp", ".h", ".hpp"}


def load_ignore_patterns():
    ignore_file = ROOT / ".clang-format-ignore"
    if not ignore_file.is_file():
        return []
    patterns = []
    for raw in ignore_file.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line and not line.startswith("#"):
            patterns.append(line)
    return patterns


def is_ignored(rel_path: str, patterns) -> bool:
    for pat in patterns:
        prefix = pat[:-3] if pat.endswith("/**") else pat.rstrip("/*")
        if prefix and (rel_path == prefix or rel_path.startswith(prefix + "/")):
            return True
        if fnmatch.fnmatch(rel_path, pat):
            return True
    return False


def walk_repo_cxx_files():
    patterns = load_ignore_patterns()
    found = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        rel_dir = Path(dirpath).resolve().relative_to(ROOT).as_posix()
        if rel_dir == ".":
            rel_dir = ""
        kept_dirs = []
        for d in sorted(dirnames):
            candidate = f"{rel_dir}/{d}" if rel_dir else d
            if not is_ignored(candidate, patterns) and (rel_dir or d in CXX_DIRS):
                kept_dirs.append(d)
        dirnames[:] = kept_dirs
        for name in sorted(filenames):
            rel = f"{rel_dir}/{name}" if rel_dir else name
            if Path(name).suffix in CXX_SUFFIXES and not is_ignored(rel, patterns):
                found.append(rel)
    return found


def staged_files():
    res = subprocess.run(
        ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR"],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if res.returncode != 0:
        return []
    return [line.strip() for line in res.stdout.splitlines() if line.strip()]


def all_tracked_files():
    res = subprocess.run(
        ["git", "ls-files"],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if res.returncode != 0:
        return []
    return [line.strip() for line in res.stdout.splitlines() if line.strip()]


def is_cxx_file(rel_path: str, patterns) -> bool:
    p = Path(rel_path)
    return (
        p.suffix in CXX_SUFFIXES
        and bool(p.parts)
        and p.parts[0] in CXX_DIRS
        and not is_ignored(rel_path, patterns)
    )


def main():
    if "--format" in sys.argv[1:]:
        files = walk_repo_cxx_files()
        if not files:
            return 0
        return subprocess.run(["clang-format", "-i"] + files, cwd=ROOT).returncode

    patterns = load_ignore_patterns()
    check_all = "--all" in sys.argv[1:]
    files = all_tracked_files() if check_all else staged_files()
    cxx_files = [f for f in files if is_cxx_file(f, patterns) and (ROOT / f).is_file()]
    py_files = [f for f in files if f.endswith(".py") and (ROOT / f).is_file()]

    if cxx_files:
        cmd = ["clang-format", "--dry-run", "--Werror"] + cxx_files
        if subprocess.run(cmd, cwd=ROOT).returncode != 0:
            print("pre-commit: clang-format check failed (run 'make format')", file=sys.stderr)
            return 1

    line_args = [] if check_all else cxx_files
    if check_all or cxx_files:
        if subprocess.run([sys.executable, "tools/check_line_limits.py"] + line_args, cwd=ROOT).returncode != 0:
            return 1

    if subprocess.run([sys.executable, "tools/check_cxx.py"], cwd=ROOT).returncode != 0:
        return 1

    if (ROOT / "build" / "obj").is_dir():
        if subprocess.run([sys.executable, "tools/check_static_init.py"], cwd=ROOT).returncode != 0:
            return 1

    if py_files:
        if subprocess.run([sys.executable, "-m", "py_compile"] + py_files, cwd=ROOT).returncode != 0:
            return 1

    print("pre-commit: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
