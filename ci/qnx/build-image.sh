#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Builds the private QNX build image of the release workflow, and with --push
# publishes it to ghcr.io and prints the digest to pin in
# .github/workflows/release.yml. Its context holds only the files listed in
# ci/qnx/files.txt, from a local QNX 6.5 SP1 toolchain, and four head-unit
# libraries; none of them is ever committed. See ci/qnx/README.md.
#
#   ci/qnx/build-image.sh [--push]
#
# QNX65_TOOLS (default ~/src/tools/qnx-6.5sp1-tools), QNX_SCREEN_LIBDIR (libscreen.so,
# libcrypto.so) and RHMI_LIBDIR (libAppFramework.A.so, libCoreFramework.A.so) name
# the inputs. NBT_QNX_IMAGE overrides the image name.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
tools="${QNX65_TOOLS:-$HOME/src/tools/qnx-6.5sp1-tools}"
image="${NBT_QNX_IMAGE:-ghcr.io/nbtfoundry/qnx65-build}"
push=0
[[ "${1:-}" == "--push" ]] && push=1

fail() { echo "build-image: $*" >&2; exit 1; }
[[ -x "$tools/sdk/host/linux/x86/usr/bin/qcc" ]] || fail "no QNX 6.5 SP1 toolchain at $tools (set QNX65_TOOLS)"
for f in libscreen.so libcrypto.so; do
  [[ -f "${QNX_SCREEN_LIBDIR:?set QNX_SCREEN_LIBDIR}/$f" ]] || fail "missing $f in QNX_SCREEN_LIBDIR"
done
for f in libAppFramework.A.so libCoreFramework.A.so; do
  [[ -f "${RHMI_LIBDIR:?set RHMI_LIBDIR}/$f" ]] || fail "missing $f in RHMI_LIBDIR"
done

context="$(mktemp -d)"
trap 'rm -rf "$context"' EXIT
mkdir -p "$context/qnx" "$context/car/screen" "$context/car/rhmi"
# tar keeps the toolchain's symlinks (files.txt lists their targets too).
tar -C "$tools" -cf - -T "$here/files.txt" | tar -C "$context/qnx" -xf -
cp -L "$QNX_SCREEN_LIBDIR"/{libscreen.so,libcrypto.so} "$context/car/screen/"
cp -L "$RHMI_LIBDIR"/{libAppFramework.A.so,libCoreFramework.A.so} "$context/car/rhmi/"
cp "$here/Dockerfile" "$context/"

# The tag names the content: the Dockerfile and every input file.
tag="$( (cd "$context" && find . -type f -print0 | sort -z | xargs -0 sha256sum) | sha256sum | cut -c1-12)"
docker build --pull=false -t "$image:$tag" "$context" >&2
echo "built $image:$tag ($(du -sh "$context" | cut -f1) of inputs)" >&2
if (( push )); then
  docker push -q "$image:$tag" >&2
  docker image inspect "$image:$tag" --format '{{index .RepoDigests 0}}'
fi
