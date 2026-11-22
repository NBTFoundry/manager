---
name: manager-kit
description: Build an NBT Manager in-car test kit (Windows installer, signed manager release, phone APK, test apps, procedure) with make kit and copy it to the kit folder for the user. Use when the car needs a manager, phone app or installer change before a test.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Manager test kit

The kit, `nbt-manager-windows-kit-devN.zip` and its folder `nbt-manager-devN/`:
the Windows installer (manager release and test app v1 inside), the phone APK,
`phone/NBT-Manager-devN.nbtr` (to update from the phone), test app v1, v2 and
a broken v3, the same three versions run by nbt-runner (`test-runner-*.nbt`),
the in-car procedure as `PROCEDURE.md`, the USB scripts.
`tools/make_kit.py` and `tools/build_windows_kit.py` describe it.

N comes from `~/.local/share/nbt-manager/keys/manager-sequence`, raised by
each kit. A car that installed a CI release (sequence `X*1000000+...`) no
longer takes dev kits as updates.

1. **What goes in is committed.** `make kit` refuses a dirty manager tree.
   The phone app and installer are built from `../mobile-app` and
   `../installer` as they are checked out: check their branch (normally
   `main`) and `git status`. The test apps build on the SDK at `../sdk`
   (`NBT_SDK_DIR`) with its NBT toolchain (`make toolchain` there).
2. **Build**, in the manager, in the background (several minutes), with the
   values from the workspace `CLAUDE.md`:
   ```sh
   QNX_ENV=… QNX_SCREEN_LIBDIR=… RHMI_LIBDIR=… NBT_KIT_DIR=<kit folder> make kit
   ```
   It runs `qnx-check stack-report`, builds and signs the release and the
   test apps, the APK (its keystore under the keys folder's `android/`) and
   the Windows installer (Rust `x86_64-pc-windows-gnu`, MinGW, cargo-ndk),
   copies to the kit folder without overwriting, and checks every SHA-256 there.
3. **Report** in a few lines: kit version and commit, the release SHA-256 it
   prints, and what to test that changed since the last kit
   (`docs/bench-checklist.md` sections, an app's car-test procedure).
