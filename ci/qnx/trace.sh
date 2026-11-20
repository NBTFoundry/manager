#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Rewrites ci/qnx/files.txt: runs clean QNX builds under strace and lists the
# toolchain files they used. Run it when a release build needs a header or library
# the image lacks, then rebuild the image (ci/qnx/README.md).
#
#   ci/qnx/trace.sh [APP_DIR...]
#
# Traces this repository's release build, the SDK's nbt-runner (the executable of
# every application released as a bundle: a module or guest appended to it; the SDK
# in NBT_SDK_DIR, default ../sdk) and, for each APP_DIR (an application repository
# such as ../nf-doom, with the SDK next to it), its `make qnx qnx-check`.
# QNX65_TOOLS, QNX_SCREEN_LIBDIR and RHMI_LIBDIR are as for build-image.sh.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
tools="${QNX65_TOOLS:-$HOME/src/tools/qnx-6.5sp1-tools}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
# The host's make: env.sh puts the QNX host tools, GNU make 3.81 among them, first
# on the PATH, and the SDK needs make 4.3 or newer.
make="$(command -v make)"

traced() { # NAME DIR MAKE-ARGS...
  local name="$1" dir="$2"
  shift 2
  (cd "$dir" && strace -f -qq -o "$work/$name.trace" -e trace=%file,%process -s 512 \
     "$make" -j "$(nproc)" "$@" BUILD_DIR="$work/$name" >"$work/$name.log" 2>&1) \
    || { tail -20 "$work/$name.log" >&2; echo "trace: the $name build failed" >&2; exit 1; }
}

# shellcheck source=/dev/null
source "$tools/env.sh"
export QNX_ENV="$tools/env.sh" QNX_SCREEN_LIBDIR="${QNX_SCREEN_LIBDIR:?}" RHMI_LIBDIR="${RHMI_LIBDIR:?}"
traced manager "$root" qnx qnx-plugin qnx-capture qnx-check \
  APPS_PUBLIC_KEY="$root/release/apps-public.pem" MANAGER_VERSION=trace
traced nbt-runner "${NBT_SDK_DIR:-$root/../sdk}" nbt-runner
for app in "$@"; do
  traced "$(basename "$app")" "$app" qnx qnx-check
done
for trace in "$work"/*.trace; do
  python3 -I "$here/trace_files.py" --list "$tools" "$trace"
done | LC_ALL=C sort -u >"$here/files.txt"
echo "ci/qnx/files.txt: $(wc -l <"$here/files.txt") files"
