# QNX build image

The release workflows build the head-unit binaries in a private container image,
`ghcr.io/nbtfoundry/qnx65-build`, pinned by digest: this repository's
`release.yml` for the manager, and `app-release.yml`, which application
repositories call, for their packages. It holds:

- the files of the QNX 6.5 SP1 toolchain (`qcc`, GCC 4.8.3, binutils, headers,
  target libraries, its `env.sh`) that the manager's and the applications' builds
  open, the SDK's `nbt-runner` included (the executable of applications released
  as a module or guest bundle), listed in `files.txt` (about 330 files, 63 MB);
- four head-unit libraries the links need: `libscreen.so` and `libcrypto.so`
  (`QNX_SCREEN_LIBDIR`), `libAppFramework.A.so` and `libCoreFramework.A.so`
  (`RHMI_LIBDIR`);
- Ubuntu 24.04 with i386 libc and zlib for the 32-bit QNX host tools, and
  `g++`, `libssl-dev` and `zlib1g-dev` for the host verifier.

None of these files is in this repository. The image is unlinked from any repository
and private; its package settings grant read access under "Manage Actions access" to
this repository and to each application repository that releases with
`app-release.yml`, which is how the workflows' `GITHUB_TOKEN` pulls it.

The binaries built in the image are byte-identical to a local build with the same
toolchain and libraries.

## Rebuilding it

When the build needs a toolchain file the image lacks (a new QNX header or library),
the release build fails with a missing file. Then, on a machine with the toolchain:

```sh
export QNX_SCREEN_LIBDIR=/path/to/head-unit/screen/lib RHMI_LIBDIR=/path/to/framework/lib
ci/qnx/trace.sh ../nf-doom ../nf-snes9x ../nf-android-auto   # rewrites files.txt from strace'd clean builds
ci/qnx/build-image.sh --push    # needs `gh auth refresh -s write:packages` and docker login ghcr.io
```

`build-image.sh` prints the new digest; put it in `release.yml` and commit it with
`files.txt`. `QNX65_TOOLS` selects the toolchain (default
`~/src/tools/qnx-6.5sp1-tools`).
