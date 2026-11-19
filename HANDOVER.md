# NBT Manager — session handover

## Current state (2026-11-19, kit dev117: Collect everything, Settings > Status)

**Install kit dev117** (`nbt-manager-windows-kit-dev117.zip`, SHA-256
`16f70f75…5c0c`, release `ddd5136c…424c`, app versionCode 117, from
`7a2c996`, every checksum verified on the NAS by `make kit`) with **Doom
1.0.18** (`doom-1.0.18.nbt`, `64b00722…a47a`, on the NAS beside the kits).
dev116 to dev113 are superseded; dev91 to dev107 are not to be installed. The
car runs dev111. Kits carry one release (since dev114) and are made with `make
kit` (`tools/make_kit.py`, docs/install-flow.md).

**New in dev117** (the user's picks 1, 3, 4 and 5 from the second idea list):
**Collect everything** in both installers (one read-only report over SSH,
sections below in "Collect everything"), the manager's Settings > **Status**
and **Test the gamepad** (section "Settings > Status"), and `plugin.cpp` split
(`plugin/launcher_spawn.cpp`). The shim is dev115's.

**New in dev115** (the user's picks from the improvement list):

- **Diagnostics** (`651dd00`, docs/input-capture.md end): ring records carry
  the FBlock; an escape says how long BACK was held; a dropped BACK hold is
  `kind=7`. The plugin re-logs `capture shim active` and replays the ring as
  `capture diag (earlier)` whenever the debug log moves to a new file
  (`nbt_log_file`), so a fetched log starts with them. The dev111 log's start
  was not lost to rotation (58 KB, under 128 KB): it was switched off and on,
  or removed, before; not established which. The manager's list shows
  `iDrive gets the keys too (...)` when the shim escaped, is an observer or is
  not loaded (`CaptureWatch`).
- **One gamepad owner** (`0f9bbe9`, docs/gamepad-bridge.md): the GUI keeps the
  pad open for its whole life and publishes its state in
  `/dev/shmem/nbt-manager-gamepad`; application hosts relay it on
  `NBT_GAMEPAD_FD` (unchanged wire) and never open USB. The car's two
  `GET_CONFIGURATION` failures came right after the old switches.
- **Volume (decided with the user):** the knob sets iDrive's master volume
  only, as usual; the manager shows it with its own widget, as now; an app's
  volume is its own and multiplies with the master (no manager setting).
  Doom 1.0.18 (nbt-doom `39946e9`, pushed) drops 1.0.17's Volume setting:
  Doom's Options > Sound Volume does it, saved in `prboom-plus.cfg`.
- **The floor of 25** belongs to the browser source `0xb8` (`eSRC_BROWSER`,
  interrupting); NBT-AAD's native audio has it too (the user). Not
  established why: nothing in the `/opt` dump sets a minimum volume.
  `audioconn` also knows `eSRC_BON_BROWSER_ENT`, which looks like an
  entertainment variant of the browser source. Next: the inventory's
  `audioconn-l.txt` gives its number; Doom builds the connection named in
  `/var/opt/nbt-manager/data/doom/audio-conn` (e.g. `0x9a`) instead of `0xb8`.
- **`make kit`** (`8733861`): from a clean commit only; QNX checks, kit, copy
  to the NAS, checksums there, the docs row's facts.

**Waiting on the car**: Update to dev117 ("live test OK active"), restart the
head unit, install Doom 1.0.18. OPTION first thing in App Manager. Plug the
Xbox pad, then the PS4 pad: for each, Settings > Status (the gamepad line says
which pad is open or why none is) and Test the gamepad (every button, both
sticks and triggers). Switch between the manager and Doom a few times (no
`USB service detached` between them); the PS4 light bar; a favourite key in
Doom (`capture diag ... fblock=0x15 fkt=0x0c02`); Doom's own volume. Then
**Collect everything** from the installer (debug log on first): it has the
logs and `audioconn -l`, so the USB inventory is needed only for file copies.

**Not established:** whether the Xbox pad opens; whether the relay's
`/dev/shmem` record works between the GUI and a host on the car (same owner
checks as the capture control file, which works there); the favourite keys
in the feed; iDrive's rotary jump and stuck keys after a capture; the phone
installer in a car.

**Open:** the phone app's installer lacks **Shim on/off**. Several files warn
at the line limit. `test_killed_supervisor_rolls_back_on_next_launch` timed out once under
full-suite load and passes alone (3 of 3).

## 2026-11-19: Settings > Status and the gamepad test (not in a kit yet)

**Changed:** the manager's Settings end with **Status** (`src/gui/status_page.cpp`):
`Keys: ...` (the capture shim's state from `CaptureWatch`), `iDrive controller:
read over MOST` or `not read`, the gamepad (the open pad's family and
vendor:product; else another process holding the pads, `usbd_connect`'s error,
or a plugged-in pad that failed to open with the step, from the service's new
`Service::report` and `UsbDevice::failure`), and the last error screen since
the manager opened. **Test the gamepad** shows the pressed buttons (Sony names
for Sony pads), sticks and triggers in percent, live; the pad's buttons are
not acted on there, the iDrive controller's BACK leaves. Neither screen sends
worker requests. The view model moved to `include/nbt/gui/ui_view.hpp`; the
"Pair a phone" view moved to `phone_view.cpp` (room in `view_builder.cpp`).
Docs: gamepad-bridge.md, input-capture.md section 6, PROTOCOL.md,
in-car-procedure.md.

**Checked:** `make pre-commit test` (298), the QNX builds with `qnx-check
stack-report`. Tests: `test_status_page` (view, keys, `gatherStatus`, every pad
description, button names), the service report with a simulated and a refusing
transport, the lock held by another process (`test_gamepad.py`), pad buttons
ignored on the test (`test_gamepad.cpp`), the probe rendering both screens
(`test_manager_ui.py`). **Not run** in the car: whether `usbd_connect` or the
open step is what keeps the Xbox pad off is exactly what the page should show.

## 2026-11-18: Collect everything; `plugin.cpp` split (not in a kit yet)

**Changed:** the installers' Test tools gain **Collect everything**
(`desktop/core/src/collect.rs`, `Action::CollectAll`, `Event::Collected`): one
read-only report over SSH, `collect-<time>.txt` in the logs folder (the phone
shares it). Sections: `uname -a; mount`, both debug log files, `sloginfo`,
`audioconn -l` (both paths, stopped after 10 s, as the inventory does), `pidin arg`, `/dev/starter/status/overview`, the shim's
state and files, `ls -lR` of the manager's folders, `/dev/shmem`. A failed
section is noted and the rest run. It replaces the USB stick for the run-to-run
part of the inventory (docs/car-inventory.md), not the file copies. The
plugin's launcher start moved to `plugin/launcher_spawn.cpp` (`c17311c`;
`plugin.cpp` 330 to 259 lines).

**Checked:** `cargo test` (the fake SSH target runs the collection through the
host's shell: missing tools noted, the debug log present), clippy (two older
warnings remain), the app's tsc, eslint and jest, `make pre-commit test`.
**Not run** in the car or on a phone; `audioconn -l` has not run on the car
by any route yet (dev116's inventory added it), so its output and run time
are not established.

## 2026-11-18: diagnostics, one gamepad owner, Doom volume, `make kit`

**Changed:** `src/capture/{shim.c,shim_diag.c,classifier.c}`,
`plugin/capture_diag.hpp`, `src/log/log.cpp` (`nbt_log_file`),
`src/gui/capture_watch.cpp`; `src/gamepad/{service,shared_state,relay}.cpp`,
`src/gui/gamepad_input.cpp`, `src/process/host_channels.cpp`;
`tools/make_kit.py`, `Makefile`. Tests: the live test's dropped-hold record,
`test_capture_watch`, `nbt_log_file` in `test_log.cpp`, records and the relay
(`test_gamepad.cpp`, `test_gamepad.py` with the fake layer manager).

**Checked:** `make test` (297), `pre-commit`, QNX builds and `qnx-check
stack-report`, Doom's `make test`, `make kit` itself. **Not run** in the car.

## 2026-11-18: one release per kit; Update with the release's own launcher

**Changed:** `desktop/core/src/ssh.rs` (`update`, `update_writable`,
`stage_and_refresh`, `refresh`; no `CompatRelease`, no `--capabilities`; the
not-installed message), `desktop/core/{build.rs,src/bundle.rs}`,
`tools/build_windows_kit.py`, the native test and CI, docs.

**Checked:** `cargo test --workspace` (42 + 3 + 5), the native test, clippy
(the two older findings only), `pre-commit`, QNX build. **Not run** in the car
or on a phone.

## 2026-11-18: dev111 in the car; stale BACK escape, gamepad retries, DS4 light bar

**Changed:** `src/capture/{shim.c,classifier.c}`,
`include/nbt/capture/classifier.h` (`back_generation`),
`src/launcher/capture_selftest.cpp` (a BACK that goes by uncaptured),
`src/gamepad/usb_transport.cpp` (`same` with vendor and product, `block`,
`retryDue`), `src/gamepad/{reports,service}.cpp` (`dualShock4LightBar`),
`tests/test_{capture,gamepad}.cpp`.

**Checked:** `make test` (296), `pre-commit`, QNX builds and `qnx-check
stack-report`. **Not run** in the car.

## 2026-11-18: the active shim, the favourite keys, capability `controller`

**Changed:** `src/capture/{classifier.c,shim.c}`, `include/nbt/capture/protocol.h`
(0x15/0xC02), `Makefile` (`qnx-capture` active), `src/launcher/capture_selftest.cpp`
(favourite press and release held); `include/nbt/controller_wire.h`,
`include/nbt/controller/input.hpp`, `src/sdk/controller_{wire,input}.cpp`,
`src/most/controller_feed.cpp`, `src/most/service{,_api}.cpp` (feed before the
policy, its registrations, host socket optional), `src/process/*` (fd 8,
`nbt_app_channels`, `nbt_process_start_host_channels`, spawn limit 9, shown
gate), `src/model/bridge_api.cpp` (1.4). Tests: `tests/test_controller_feed.cpp`,
`test_controller_states_reach_a_shown_application`, the classifier's favourite
cases, bridge fixtures at 1.4.

**Checked:** `make test` (296 + controller-feed-test), `pre-commit`, QNX builds
and `qnx-check stack-report`, `cargo test` (41), Doom's `make test` and its
signed package (`nbt-verify`: bridge 1.4, `show,controller`). **Not run** in
the car.

## 2026-11-18: dev107 in the car; gamepad interface discovery; "Restart the head unit"

**Changed:** `src/gamepad/usb_discovery.cpp` (interfaces read one by one,
logged; the walk as a logged fallback), `tests/test_usb_gamepad.cpp` (the fake
answers per interface); the restart wording and confirmation in
`desktop/src/main.rs`, `desktop/core/src/bench.rs`, `src/gui/setup_ui.cpp`,
`src/worker/system_tools.cpp`, `tests/test_ui.cpp`, the phone app
(`InstallerScreen.tsx`, `session.ts`), and the docs (`docs/hmi-restart.md`,
`docs/install-flow.md`, `docs/in-car-procedure.md`, `desktop/README.md`).

**Checked:** `make test` (295), `pre-commit`, `cargo test` (41 + 3 + 5), the
phone app's `tsc`, `eslint` and `jest` (86). QNX build before the kit.

## 2026-11-18: dev105 in the car; the live test, the gamepad service, the installer

**Changed:** `src/storage/capture_probe.cpp` (live test in `/dev/shmem` on
QNX), `src/launcher/capture_selftest.cpp` ("no records"),
`src/storage/capture_install.cpp` + `src/launcher/setup_refresh.cpp` +
`src/worker/ipc_system.cpp` + `apps/manager/launcher.cpp` (a refused shim:
`NBT_CAPTURE_REFUSED`, `NBT_REFRESH_CAPTURE_REFUSED`, reason on standard
error), `src/gamepad/service.cpp` (`nonBlockingPrivate`), installer (no trial
after Update, no **Restart iDrive only** / **Try the shim**, a note for a
refused shim, controller settings folded), `tools/car_inventory.sh` (`/etc`).
Tests: `update_notes_a_refused_shim`, `--refresh-setup` with a refused shim.

**Checked:** `make test` (295), `pre-commit`, QNX builds and `qnx-check
stack-report` (shim unchanged, `c05e6804…7c49`), `cargo test` (41 + 3 + 5),
clippy (two older warnings). **Not run** in the car.

## 2026-11-18: the shim next to its environment file; Update tries it at once

The user found a shim missing behind an armed environment file unacceptable
(iDrive failing at every start until the file is deleted over SSH), and asked
for both fixes proposed. dev103 is superseded.

**Changed:**

- The shim lives in `state/manager/libnbt-capture.so` (`0700`, as the load
  test's copy the same loader read there), next to `state/manager/hmi.env`:
  an update of `/opt` cannot remove one and keep the other, and an unmounted
  `/var/opt` has neither. HmiMain3D runs as root (its files in
  `/var/opt/hmi/data/ID5` are root's, inventory). `NBT_FS_CAPTURE_SHIM`,
  `src/storage/capture_shim_file.cpp` (write with the old fault names, hash,
  remove, the old folder's check and removal).
- Install writes the shim without a remount; `/fs/sda0` is remounted only for
  `starter.cfg` and to remove `/opt/hmi/ID5/data/ro/NbtCapture` (only when it
  holds nothing else). Remove disarms first, restores `starter.cfg` and
  removes the old folder in one writable window, then deletes the shim.
  `starter.cfg`'s Environment 6 legacy value keeps the old path.
- Children of iDrive get both shim paths stripped from `LD_PRELOAD`
  (`include/nbt/process/hmi_environment.hpp`).
- Windows installer: after an Update that replaced the iDrive entry or the
  shim, `--try-shim` runs at once; "Restart iDrive?" only when nothing
  restarted (`try_shim_after_update`). Remove also calls `--remove-capture`
  for a shim in the state folder.
- Tests: install and remove at the new place, the old folder moved or refused
  when it holds anything else, faults (the shim's before any remount),
  `update_tries_the_shim_before_asking_for_a_restart`, preload stripping.

**Checked:** `make test` (295), `pre-commit`, QNX builds and `qnx-check
stack-report` (shim unchanged, `c05e6804…7c49`), `cargo test` (41 + 3 + 5),
clippy (two older warnings). **Not run** in the car.

**Kit dev105** (`nbt-manager-windows-kit-dev105.zip`, SHA-256 `a1089391…1473`,
on the NAS with `nbt-manager-dev105/`, every checksum verified there): manager
dev105 (`NBTRLS2`, release `6ea998e2…5e8e`) with the observer shim
(`c05e6804…7c49`), the `NBTRLS1` compatibility release dev104, and the phone
app at versionCode 105. dev103 is not to be installed. From the phone: dev104,
then dev105 (no immediate trial from the phone). **Not verified** in the car.

## 2026-11-18: the shim tested and kept automatically (dev103; the shim's place superseded by dev105)

The user asked for the shim's testing and keeping to be automatic (dev101 needed
a trial and "keep on" by hand). dev101 is superseded.

**Changed:**

- The shim deletes iDrive's environment file when it loads into HmiMain3D
  (constructor in `src/capture/shim_env.c`, `NBT_CAPTURE_HMI_ENV_PATH`), so
  each arming covers one start. The car's libraries use the same
  `DT_INIT_ARRAY` start-up (checked on `libsys_dsi_colibry.cpp-ne.so.8`).
- Install and Update arm a new shim (`state/manager/shim-state`: `armed`, SHA,
  strikes; `src/storage/hmi_env.cpp`). The trial record is gone.
- The plugin runs `launcher --shim-check` once, 60 s after iDrive started
  (`plugin/phone_runtime.cpp`); `src/launcher/shim_control.cpp`
  (`decideShimCheck`): mapped and file gone, kept and armed again; not mapped
  and file gone, a strike (re-armed once, `failed` at the second); mapped but
  file left, `failed`. No operation lock, so an open manager cannot cost a
  strike.
- `--shim on|off` replaces `--keep-shim`; `--try-shim` arms, restarts iDrive,
  and on success leaves the shim kept. Installer: **Shim on**, **Shim off**.
- Tests: `tests/test_hmi_restart.cpp` (trial and check decisions),
  `test_shim_is_armed_and_kept_automatically`,
  `test_shim_deletes_idrives_environment_file_only_inside_idrive`.
- Docs: `docs/hmi-restart.md`, `docs/install-flow.md`, `docs/input-capture.md`,
  `docs/in-car-procedure.md` (dev103), `docs/controller-input.md`.

**Remaining risk:** a shim the loader refuses before its constructor runs would
leave the file armed; the install's load test refuses that case.
`plugin/plugin.cpp` is at 330 lines, the limit.

**Checked:** `make test` (295), `pre-commit`, QNX builds and `qnx-check
stack-report` (new shim `c05e6804…7c49`), `cargo test` (40 + 3 + 5). **Not run**
in the car.

**Kit dev103** (`nbt-manager-windows-kit-dev103.zip`, SHA-256 `145ac4bb…969c`,
on the NAS with `nbt-manager-dev103/`, every checksum verified there): manager
dev103 (`NBTRLS2`, release `02e3a8b8…4ad4`) with the observer shim
(`c05e6804…7c49`), the `NBTRLS1` compatibility release dev102, and the phone
app at versionCode 103. dev101 is not to be installed. From the phone: dev102,
then dev103. **Not verified** in the car.

## 2026-11-18: the shim through iDrive's environment file, one start at a time (dev101; trial record and `--keep-shim` superseded by dev103)

The user chose idea 1: load the shim through the environment file the process
manager reads at each start, so a bad shim cannot outlive one start.

**Found** (the car's `starter`, `dc6bac6d…5d4a`, `docs/hmi-restart.md`): a
process's `<EnvFile>` is read at every start of it (`0x113aac`); a missing file
is logged and the process starts as stock; an `LD_PRELOAD` line replaces the
table's value; the entries are freed after each start. **Not established:**
that `starter.cfg` is read only at boot (so the new entry needs one restart of
the head unit), `/var/opt` being mounted at iDrive's first start, and the
recovery `STOP`/`RUN` after iDrive ended by itself.

**Changed:**

- `src/storage/starter_config.cpp`: the install sets HmiMain3D's
  `<EnvFile/>` to `/var/opt/nbt-manager/state/manager/hmi.env`; Environment 6
  stays stock. The dev91-dev99 form (shim in Environment 6) is recognised as
  `StarterLegacyShim`: Update moves it, Remove restores stock; both at once is
  refused.
- `src/storage/hmi_env.cpp` (new): the environment file (written whole,
  renamed, synced), the trial record `state/manager/shim-trial` (the shim's
  SHA-256). Install of a different shim and Remove delete both first.
- `src/launcher/shim_trial.cpp` (new): `--try-shim` and `--keep-shim on|off`;
  `src/launcher/hmi_restart.cpp`: hooks before `RUN` and once the new
  HmiMain3D exists, and `processHasLibrary` (`DCMD_PROC_MAPINFO` +
  `DCMD_PROC_MAPDEBUG`).
- Windows installer: Test tools **Try the shim (restarts iDrive)**, **Keep the
  shim on**, **Turn the shim off** (`bench::marker` for the launcher's OK
  lines). Not in the phone app's installer.
- Tests: `tests/test_hmi_restart.cpp` (7 trial cases against the fake process
  manager), `tests/test_capture_install.py` (new form, old form, keep rules),
  `launcher_tools_need_their_ok_line`; seeds in `tests/corpus/starter-config/`.
- Docs: `docs/hmi-restart.md`, `docs/install-flow.md` step 8 and recovery,
  `docs/input-capture.md` section 5, `docs/in-car-procedure.md` (dev101
  steps), `docs/controller-input.md`.

**Checked:** `make test` (294), `pre-commit`, `make fuzz`, QNX builds and
`qnx-check stack-report`, `cargo test` (40 + 3 + 5). **Not run** in the car.

**Kit dev101** (`nbt-manager-windows-kit-dev101.zip`, SHA-256 `f3ea0144…9e86`,
on the NAS with `nbt-manager-dev101/`, every checksum verified there): manager
dev101 (`NBTRLS2`, release `6e272180…5970`) with the observer shim
(`de43c44f…6976`), the `NBTRLS1` compatibility release dev100, the installer's
shim trial tools, and the phone app at versionCode 101. dev99 is not to be
installed. From the phone: dev100, then dev101; the trial needs the Windows
installer (or `nbt-manager-launcher --try-shim` over SSH). **Not verified** in
the car.

## 2026-11-18: restart iDrive alone, through the process manager (experimental)

The user wants to restart iDrive's program alone while still connected, to
see a shim problem at once and undo it in the same session.

**Found** (car files read here, `docs/hmi-restart.md`): package 14 `HMI` holds
only HmiMain3D; nothing requires it; requests are a `devctl` `0x800c050f` on
`/dev/starter/start` with `{domain, package, RUN=2|STOP=3}`, the lifecycle
manager's own path. **Not established:** domain 0, the heartbeat watchdog's
reaction, the lifecycle manager's reaction.

**Built:** `nbt-manager-launcher --restart-hmi` (`src/launcher/hmi_restart.cpp`,
sequence host-tested in `tests/test_hmi_restart.cpp`) and the Windows
installer's **Restart iDrive only (experimental)** (`RestartHmiOnly`,
`hmi_only_restart_needs_the_launchers_ok`). `car_inventory.sh` saves the
process manager's status. In-car steps: `docs/in-car-procedure.md`, step 2.

**Checked:** `make test` (292), `pre-commit`, QNX builds and `qnx-check
stack-report`, `cargo test` (48). **Not run** in the car.

**Kit dev99** (`nbt-manager-windows-kit-dev99.zip`, SHA-256 `d912fb1c…8061`, on
the NAS with `nbt-manager-dev99/`, every checksum verified there): manager
dev99 (`NBTRLS2`, release `a3343d3e…5bfe`) with the observer shim
(`de43c44f…6976`), the `NBTRLS1` compatibility release dev98, the installer's
iDrive-only restart, and the phone app at versionCode 99. dev91 to dev97 are
not to be installed. From the phone: dev98, then dev99. **Not verified** in
the car.

## 2026-11-18: live test of the capture shim during the install

The user asked for the shim's behaviour to be tested live during the install,
not only its loading.

**Changed:**

- `src/capture/shim.c`: `nbt_capture_selftest_begin(dir, forward, clock,
  &observer)`, refused inside HmiMain3D; redirects the control, heartbeat and
  diagnostics files to `<dir>/capture-selftest.*`, the clock and the forward
  target. The paths, clock and process check moved to `src/capture/shim_env.c`
  (shim.c was 428 lines, now 293).
- `src/launcher/capture_selftest.cpp` (new) and the launcher's
  `--capture-selftest <shim> <dir>`: drives the shim with the GUI's writers
  (`nbt_capture_control_write`, `holdLivenessWrite`) and MOST-shaped knob and
  BACK messages through 12 steps (see `docs/input-capture.md`); prints
  `CAPTURE_SELFTEST OK observer|active` or `CAPTURE_SELFTEST FAILED <step>`.
- `src/storage/capture_probe.cpp`: after the load test, runs it in iDrive's
  environment and refuses the shim unless it passes ("live test: ...");
  `tools/check_qnx_artifacts.py` allows the second export.

**Checked:** the self-test passes against the host observer and active
shims, refuses a library without the entry and refuses inside the process name
the shim acts in (`tests/test_capture_shim.py`); the install path runs it and
refuses a loadable non-shim (`test_install_runs_the_live_selftest`);
`make test` (292), `pre-commit`, QNX builds and `qnx-check stack-report`.
**Not covered:** real MOST messages and the real DSI library inside HmiMain3D
(the observer run in the car).

**Kit dev97** (`nbt-manager-windows-kit-dev97.zip`, SHA-256 `b312efa2…4aaa`, on
the NAS with `nbt-manager-dev97/`, every checksum verified there): manager
dev97 (`NBTRLS2`, release `ff803456…1936`) with the observer shim
(`de43c44f…6976`), the `NBTRLS1` compatibility release dev96, and the phone app
at versionCode 97. dev91, dev93 and dev95 are not to be installed. From the
phone: dev96, then dev97. **Not verified** in the car.

## 2026-11-18: shim load test in iDrive's environment and preload order

**Changed:** `src/storage/starter_env.cpp` (new) reads HmiMain3D's Process
block in `starter.cfg` and the Environment entries it requires (strict: one
block, each number once, plain values). `probeCaptureShim` runs the launcher
in that environment and loads the shim after BMW's preload list in iDrive's
order with `LD_BIND_NOW=1`. When BMW's list cannot bind every symbol outside
HmiMain3D, the test binds the list lazily and the shim alone fully; when the
list does not load at all, the shim alone. The debug log names the variant
(`capture: load test passed: ...`). The environment for the test is never
silently shortened (over 256 entries refuses). Refusal text now starts
"capture shim failed its load test, starter.cfg unchanged:" so a short error
buffer keeps the important part.

**Found:** port 6801 is the car gateway's diagnostic port, not a head-unit
service (`desktop/core/src/activation.rs`). Turning SSH on is a diagnostic job;
which head-unit process carries it out is not established.
`tools/car_inventory.sh` now records `/etc/inetd.conf` and the `/etc` and
`/etc/ssh` listings (no keys) to show what "SSH on" consists of. The car's
`starter.cfg` shows `inetd` (which starts `sshd`) does not depend on
HmiMain3D, and the starter ignores HmiMain3D ending.

**Checked:** new test `test_load_test_uses_idrive_environment_and_preload_order`
(environment, order, both fallbacks, no HmiMain3D block), the car's own
`starter.cfg` copy parsed by hand-run (three entries, iDrive's order),
`make test` (289), `pre-commit`, QNX builds, `qnx-check stack-report`.

**Kit dev95** (`nbt-manager-windows-kit-dev95.zip`, SHA-256 `d849241e…e471`, on
the NAS with `nbt-manager-dev95/`, every checksum verified there): manager
dev95 (`NBTRLS2`, release `b44ddc2f…c2f9`) with the observer shim
(`58b4abcf…9039`), the `NBTRLS1` compatibility release dev94, and the phone app
at versionCode 95. dev91 and dev93 are not to be installed. From the phone:
dev94, then dev95. **Not verified** in the car.

## 2026-11-18: capture shim test-loaded before starter.cfg names it; dev91 skipped

The user asked whether the installer tests the shim before iDrive's start
depends on it. It did not, so the user chose to skip dev91.

**Changed:** `src/storage/capture_probe.cpp` (new), called from
`nbt_capture_refresh_from_release` (every install path) after the shim is
read from the release and before the backup, transaction, remount or
`starter.cfg`. It runs the installed launcher's `--capabilities` alone, then
with a copy of the shim (`state/manager/capture-probe.so`, new path kind
`NBT_FS_CAPTURE_PROBE`) as `LD_PRELOAD` and `LD_BIND_NOW=1`; both must exit 0
within 5 s. The environment is inherited minus any `LD_PRELOAD`/`LD_BIND_NOW`
(iDrive's own preloads reach the manager). A refusal reads "capture shim failed
its load test (<reason>); starter.cfg unchanged", reported like the other
capture refusals (launcher and plugin still refreshed). Host builds skip it
unless `NBT_TEST_PRELOAD_PROBE` names the program.

**Checked:** 2 new tests (glibc's loader refuses a library with an unresolved
symbol under `LD_BIND_NOW`; a failed test leaves `starter.cfg`, `NbtCapture`,
the backup and the transaction untouched, with no remount), `make test`
(288), `pre-commit`, QNX builds and `qnx-check stack-report`. **Not
established:** ldqnx's exit for these cases on the car (the VM table says 1).

**Kit dev93** (`nbt-manager-windows-kit-dev93.zip`, SHA-256 `4820ca07…e807`, on
the NAS with `nbt-manager-dev93/`, every checksum verified there): manager
dev93 (`NBTRLS2`, release `763c4438…96b4`) with the observer shim
(`58b4abcf…9039`), the `NBTRLS1` compatibility release dev92, and the phone app
at versionCode 93. From the phone: send dev92 first, then dev93 (the car's
dev89 cannot read `NBTRLS2`); the installer's Update does both. **Not
verified** in the car.

## 2026-11-18: gamepad — every silent stop logged, car's libusbdi loaded

The 2026-11-17 car run never reached `usbd_connect` (GUI and Doom's host), and
nothing said why. The failing step is not established; details and the log
lines to expect are in [docs/gamepad-usb-research.md](docs/gamepad-usb-research.md),
"Fix for the next kit".

**Changed:**

- `src/gamepad/service.cpp`: each step logs `gamepad: <step> failed (errno N)`;
  the lock failures once per streak. `getsockopt(SO_TYPE)` removed. `ENOBUFS`
  on send (io-pkt's full datagram receiver) no longer stops the service.
- `src/gamepad/usbdi.cpp` (new): `dlopen("/lib/libusbdi.so.2")`, all 20 entry
  points or none; the static SDK copy otherwise. All USB calls go through
  `usbdi()`. No `DT_NEEDED`; no `usbd_*` export (checked with `readelf`).
- `src/gamepad/usb_transport.cpp`, `usb_discovery.cpp`: libusbdi's default
  path, as NBT-AAD; every reported device logged; a failed connect logged once
  per error code.
- `src/gui/gamepad_input.cpp`: a bad manager root and an fd beyond `select`
  are logged.

**Checked:** `pre-commit`, `make test` (286 tests, all pass now that the
`/tmp` quota has room), gamepad, USB-shim and ownership tests (the shim test
exercises the fallback: `dlopen` fails on the host), QNX builds with
`qnx-check stack-report`. **Not verified** in the car.

**Test environment:** the full `/tmp/user/1000` quota was old Claude Code
sessions' task output; the user approved deleting it (1.8 GB to 100 MB).

**Kit dev91** (`nbt-manager-windows-kit-dev91.zip`, SHA-256 `273b2898…7053`,
on the NAS with `nbt-manager-dev91/`, every checksum verified there): manager
dev91 (`NBTRLS2`, release `36ae64ef…1658`) with the observer capture shim
(`58b4abcf…9039`), the `NBTRLS1` compatibility release dev90, and the phone app
at versionCode 91 with the user's app changes (`2c9c268`, `e4df178`). The
in-car procedure has a dev91 checklist (iDrive back after the restart, the
controller, a gamepad, the debug log). **Not verified** in the car.

## 2026-11-18: input capture review — risks fixed or mitigated

A review of the four input-capture commits (`9cd0f10` to `3196bcf`) found
these; details in [docs/input-capture.md](docs/input-capture.md), "Risk
mitigations".

**Fixed:**

- The GUI wrote a 120 s watchdog with the escape off during installs: a hung
  GUI could keep iDrive's controller for 2 minutes. The record now always has
  1.5 s and the shim's escape (`src/gui/capture_link.cpp`); the shim refuses
  more than 1.5 s (`NBT_CAPTURE_MAX_WATCHDOG_MS`) and requires the GUI process
  to exist (`src/capture/shim.c`). The GUI still ignores its own escape while
  installing.
- The installer's Remove asked every launcher for `--remove-capture`; a
  launcher from before NBTRLS2 (the car's dev89) exits 2, and nothing was
  removed. Now asked only when `NbtCapture` exists (`desktop/core/src/bench.rs`).
- A `starter.cfg` backup lost to an older installer's Remove made install and
  removal refuse; it is rebuilt exactly from the current file
  (`rebuildOrigBackup`, `src/storage/capture_io.cpp`;
  `test_lost_backup_is_rebuilt_exactly`).
- An unresolved real `DSIClientDataResponse` is recorded and logged by the
  plugin instead of passing silently.

**Documented, not fixable in code:** remove NBT Manager before a BMW
firmware update; recovery needs SSH while iDrive is down, which is not
established (`docs/install-flow.md`).

**Test environment:** this user's `/tmp` quota was full (1.8 GB under
`/tmp/user/1000`); 7 host tests failed with EDQUOT before the change. Run them
with `TMPDIR` on another disk; the system-log test always writes to `/tmp`.

## 2026-11-18: Stage 4 & Stage 5 (host-complete / observer target) — capture install/recovery & release wiring

Implemented Stage 4 storage policy, transactional `starter.cfg` splicing, removal, installer `RemoveAll` integration, and Stage 5 observer-mode safety gate (`-DNBT_CAPTURE_OBSERVER=1` retained for QNX target until car/bench observer validation passes).

**Changed:**

- **Path policy & `starter.cfg` editor:** `include/nbt/fs.h`, `src/storage/path_policy.cpp`, `include/nbt/storage/{starter_config,capture_install}.hpp`, `src/storage/{starter_config,capture_io,capture_install,capture_remove}.cpp`: added `NBT_FS_CAPTURE_STARTER_ORIG{,_NEXT}` (`state/manager/starter.cfg.orig`) and `NBT_FS_CAPTURE_TX{,_NEXT}` (`state/manager/capture.tx`); byte-preserving `starter.cfg` Environment 6 (`System_HMI_PRELOAD`) parser/splicer that preserves CRLF line endings, `0664` metadata, and allows QNX-style `st_nlink` 1–2 while rejecting symlinks, extra hardlinks, nested/duplicate Environment 6 blocks, and leftover temporaries; transactional install (`/opt/hmi/ID5/data/ro/NbtCapture/libnbt-capture.so` written, verified by SHA-256, synced before updating `starter.cfg` and reading it back by hash) and ordered removal (`starter.cfg` restored and hash-verified **before** deleting `NbtCapture/`, refusing automatic restore and retaining shim/backups if `starter.cfg` changed outside preload); `nbt_capture_operational_status` reports `pending-restart` until `/dev/shmem/nbt-manager-capture-diag` reports active/observer load.
- **Launcher, worker & installer integration:** `include/nbt/manager_refresh.h` (`NBT_REFRESH_CAPTURE`), `src/launcher/setup_refresh.cpp`, `apps/manager/launcher.cpp` (`--remove-capture`, reports `REFRESHED plugin` when capture changes so iDrive restart is requested), `src/worker/ipc_system.cpp` (folds `NBT_REFRESH_CAPTURE` into plugin-restart reply and logs `nbt_capture_operational_status`), `desktop/core/src/{bench.rs,ssh.rs}` (`Action::RemoveAll` runs `--remove-capture` first and aborts `rm -rf` on refusal; asserts `NbtCapture` removal).
- **Tests & docs:** `tests/capture_install_probe.cpp`, `tests/test_capture_install.py` (including real vehicle `opt/sys/etc/starter.cfg` CRLF/mode preservation, 12 fault-injection/crash cases, external edit refusal, refresh/launcher CLI, operational status), `tests/fuzz.cpp` (`starter-config`), `docs/{install-flow,in-car-procedure,controller-input,input-capture}.md`.

**Verified:** `pre-commit` (`format`, `check-line-limits`, `check-cxx`, `check-static-init`), `make test` (all C++ host tests + 285 Python tests OK, 3 skipped for missing optional tools), `make fuzz` (`starter-config`, `capture-control`, `capture-stream`), `cargo test --locked --workspace` (39 + 3 + 5 OK plus `--ignored` `native_bootstrap_over_ssh`), QNX ARM build (`qnx qnx-plugin qnx-capture qnx-check stack-report`), and `mobile/nbt-manager` `yarn test`.

**Not verified / held pending hardware:** QNX ARM target runtime in car or on bench. Per Stage 5 gating, QNX `build/libnbt-capture.so` stays in **observer mode** (`-DNBT_CAPTURE_OBSERVER=1`) until bench/car testing establishes live observer message flow, press/release key reconciliation, rotary delta continuity across capture boundaries, and 5-minute stability without DTCs. Active suppression (`-DNBT_CAPTURE_OBSERVER=0`) and 5 s hard-`BACK` escape remain validated only on host (`test_capture_shim.py`, `test_layer_manager.py`).

## 2026-11-18: Stage 3 — observer preload shim (`libnbt-capture.so`), control protocol & 5 s escape

Implemented the C99 libc-only `DSIClientDataResponse` observer shim, control/diagnostic shared-memory protocol, child `LD_PRELOAD` stripping, and the unified 5-second hard-`BACK` escape path (tracked across manager + app surfaces, ignored during install via extended watchdog `120000 ms` + `esc=0`). Target `libnbt-capture.so` remains built in observer mode (`-DNBT_CAPTURE_OBSERVER=1`: always forwards; control-file contents cannot enable dropping). Installer/`starter.cfg` mutation is Stage 4.

**Changed:**

- **Capture protocol, classifier & shim:** `include/nbt/capture/{protocol,control,classifier,diag}.h`, `src/capture/{control,classifier,diag,shim}.c`: pure C99 zero-heap zero-static-init shim exporting only `DSIClientDataResponse`, verifying `HmiMain3D` (`DCMD_PROC_MAPDEBUG_BASE` on `/proc/self/as` under QNX) and `MOSTSubSystem.DSYSMOSTServer`, caching control + liveness reads at most every `100 ms` while checking cached liveness expiry on every candidate message, and writing lock-free diagnostic records to `/dev/shmem/nbt-manager-capture-diag`.
- **GUI capture link & 5 s hard-`BACK` escape:** `include/nbt/gui/capture_link.hpp`, `src/gui/{capture_link,gui_session,gui_wait}.cpp`, `apps/manager/main.cpp`, `include/nbt/{ccs.h,most_input.h}`, `src/most/{ccs,input,input_api}.cpp`: writes `off` before the GUI's first heartbeat, requests `on` only when controller input is active (`inputFd_ >= 0`) and a manager or app surface is `OnPanel` (preserving generation across handovers), writes `off` on aside/panel-loss/shutdown, tracks 5 s hold on hard `BACK` button slots only (excluding left tilt, ignored during `ui_.installing()`), synchronously flushes `STOP <app> fault`, and closes GUI cleanly.
- **Child environment & plugin drain:** `include/nbt/process/hmi_environment.hpp`, `include/nbt/app_settings.h`, `src/lifecycle/app_settings.cpp`, `plugin/{capture_diag.hpp,phone_display.cpp,plugin.cpp}`: strips only exact `/opt/hmi/ID5/data/ro/NbtCapture/libnbt-capture.so` into bounded caller storage (failing on overflow), drains diagnostic ring records on plugin poll without adding dynamic imports.
- **Tests & QNX build:** `Makefile`, `tools/check_qnx_artifacts.py`, `tests/{core_test.cpp,test_capture.cpp,fake_dsi_base.c,test_capture_shim.c,test_capture_shim.py,test_layer_manager.py,fuzz.cpp}` (`capture-control`, `capture-stream`).

**Verified (host + QNX build):** `pre-commit`, `capture-test`, `core-test`, `test_capture_shim` (observer, active, wrong-process), `test_layer_manager` (all 16 tests including 5 s escape, tilt non-escape, preview `off`, handover generation), fuzz (`capture-control` + `capture-stream`), and QNX ARM build (`make qnx qnx-plugin qnx-capture qnx-check stack-report`).

**Not verified:** in the car or on target QNX storage (Stage 4 installation and car/bench observer validation pending).

## 2026-11-18: Stage 2 — dual NBTRLS1/NBTRLS2 release compatibility & `--capabilities`

Preparing the software upgrade path before shipping any target preload shim (`libnbt-capture.so`). No changes to iDrive's `starter.cfg` or `LD_PRELOAD` yet.

**Changed:**

- **Release format (`NBTRLS2`):** `include/nbt/release.h`, `src/package/release.cpp`, `include/nbt/package/release_verifier.hpp`, `tools/package_release.py`, `tools/verify_release.cpp`, `tools/build_release.py`, `tools/build_windows_kit.py`: added optional ninth component `capture_lib` (validated as ARM `ET_DYN` ELF, `size >= 52`) in `NBTRLS2` alongside unchanged 8-component `NBTRLS1`. Windows kits embed the signed `NBTRLS1` compatibility release (`NBT_MANAGER_COMPAT_RELEASE`) when packaging `NBTRLS2`.
- **Launcher capabilities & gating:** `apps/manager/launcher.cpp` (`--capabilities` prints `formats=NBTRLS1,NBTRLS2\n`), `src/worker/system_tools.cpp` (`nbt_manager_launcher_supports_format`), `src/worker/manager_upload.cpp`, `desktop/core/src/{release.rs,bundle.rs,ssh.rs}` (`--capabilities` pre-check and `NBTRLS1` compatibility upgrade staging before `NBTRLS2`).
- **Stager & phone protocol:** `src/launcher/{bootstrap,manager_setup,stager}.cpp` account for `components[8].size`; phone `src/protocol/{releaseHeader.ts,packageHeader.ts}` accept `NBTRLS1` and `NBTRLS2` headers; `docs/manager-release.md` updated.

**Verified (host + QNX build):** `pre-commit` (`format`, `check-line-limits`, `check-cxx`, `check-static-init`), C++ host tests + `test_release.py` + `test_phone_manager_update.py` + full `tests/` suite, Cargo workspace tests (`nbt-installer-core` including `native_bootstrap_over_ssh`, `nbt-installer-mobile`, `nbt-manager-installer`), `mobile/nbt-manager` `yarn test`, and QNX cross-compile (`make qnx qnx-plugin qnx-check stack-report`).

**Not verified:** on target car/bench (Stage 3–5 preload shim not yet introduced).

## 2026-11-17 night: kit dev89 — iDrive held again at once after BACK; controller trace

dev88 was built but not published; dev89 contains it.

**Reported** (the user): fast knob turns or repeated presses in an
application bring iDrive's interface back. **Found** in the manager log of
2026-11-17: every loss of the blank state followed a BACK, which iDrive takes
0.1 to 0.5 s after the press. The plugin re-held at its next poll, up to
250 ms later; a second BACK or a knob turn in that window took iDrive to its
list (603 s in the log).

**Changed:**

- `plugin/plugin.cpp`: `onParentFocus` runs the hold's poll at once when
  iDrive reaches the parent state, so event 50 goes out within milliseconds
  (through the dev87 sender thread, so the callback does not wait).
- `src/most/input_trace.cpp` (`InputTrace`, host tested in
  `tests/test_input_trace.cpp`): the GUI's MOST client keeps its last 48
  messages raw; on `aside` the GUI writes them to the debug log
  (`nbt_most_input_log_recent`).

**Asked and answered** (`docs/controller-input.md`): keeping iDrive from
receiving the controller at all is not established as possible.
`CRemoteControlService` in `libhmiremoting.so` injects knob and key events
into iDrive's controller device, it does not divert them; blocking them
inside HmiMain3D needs that device's event path (`getTCDevCCS35up`) reversed
first, with a release on GUI exit, on lost heartbeats and on a 5 s BACK hold
(the user's escape). Stopping HmiMain3D is ruled out: the watchdog resets
iDrive.

Kit: `nbt-manager-windows-kit-dev89.zip`, SHA-256 `5852b60d…8145`, release
SHA-256 `739a443a…bb21`, on the NAS.

**Not verified:** in the car (all of dev88 and dev89).

## 2026-11-17 night: kit dev88 — an application is paused while iDrive has the screen

**Reported** (the user): in an application, NAV, OPTION and other hard keys
switch to iDrive while the application keeps running behind it, reading the
controller and playing sound. Wanted: paused, closed after 2 minutes.

**Changed:**

- `src/gui/app_aside.cpp`: on the plugin's `aside`, the GUI sends `SIGUSR2`
  to the host of the application on the panel (`nbt_app_host_signal`), then
  exits as before.
- `src/process/app_pause.cpp` (`AppPause`), used by `app_host.cpp`: the host
  stops the application's process group (`SIGSTOP`), holds its heartbeat
  clock, and through `HostChannels::pause` discards MOST messages
  (`MostSession::setPaused`, checked in `MostService::forward`) and switches
  the gamepad relay off. A show request (`START` of that application from
  App Manager) continues it first. After 2 minutes paused the host stops it:
  `SIGTERM`, `SIGCONT`, then the usual stop. Logged as `host <app>: paused
  while iDrive is shown`, `resumed`, `paused for 2 minutes; stopping`, in the
  debug log and the application's log.
- `nbt_process_signal` / `ProcessGroup::signal`: the group-or-process signal
  that stop already used.
- Documented in `PROTOCOL.md` ("Paused while iDrive is shown") and
  `docs/controller-input.md`.

**Verified:** host tests (`tests/test_app_pause.cpp`, a real child process:
stopped, continued, stopped again and terminated), all host tests, `make
pre-commit`.

**Not verified:** in the car. Whether io-audio goes quiet when the
application stops in the middle of its sound (an underrun is expected); how
Doom behaves after a long pause (its clock jumps); that a camera view or a
warning also sends `aside` (it should: any exit from the blank state).
Resuming goes through App Manager's list: the manager opens on its list and
the user selects the application. It does not jump straight back into it.

## 2026-11-17 night: kit dev87 — focus events off the plugin's thread; thread snapshot when iDrive hangs

**Found** (disassembly of the car's `libAppFramework.A.so` and `HmiMain3D`,
[docs/car-issues-2026-11-17.md](docs/car-issues-2026-11-17.md), "What
`triggerEvent` waits for"): `PluginApp::triggerEvent` is a synchronous round
trip. iDrive's `CLocalCommAdapter` queues the event on a `CHmiGenMessenger`
and waits for the reply with no timeout. In the freeze, iDrive's main thread
was waiting for the plugin framework's lock, so the plugin's iDrive thread
waited forever. Whether something also waited for the plugin's thread (a
cycle through us) is **not established**.

**Changed** (a mitigation, not a proven fix):

- `src/gui/focus_sender.cpp` (host tested, `tests/test_focus_sender.cpp`,
  `make test` runs `focus-test`): a queue and a sender thread for focus
  events, plus a watch thread that reports an event in iDrive for 3 s and
  10 s. `plugin/focus_events.cpp` wires it to `triggerEvent`; if the threads
  cannot start, events go out from the plugin thread as before.
- `plugin/plugin.cpp`: the poll drops the hold and sends the GUI aside while
  an event has been stuck for 3 s; dropped events are logged.
- `plugin/hmi_threads.cpp` (QNX only, no host test): the thread snapshot
  (state, blocked object and its two words, CPU time, code addresses on the
  stack via `dladdr`), from `DCMD_PROC_TIDSTATUS` on the HMI's own `/proc`
  entry. Thread name and `dladdr` functions are looked up with `dlsym`.
- `make qnx-plugin` builds the new files; all 271 host tests pass. Imports
  compared with release 83's plugin (which loaded in the car): new are libc's
  pthread functions, `devctl` and `strrchr`.

Kit: `nbt-manager-windows-kit-dev87.zip`, SHA-256 `e7e36d4c…b9b7`, release
SHA-256 `62488e35…4cdd`, on the NAS. `qnx-check`'s import baseline
(`tests/baseline/plugin-undefined.txt`) gained the 11 new imports (`a5a9258`).

**Not verified:** anything in the car. Whether the freeze still happens,
whether the snapshot runs inside HmiMain3D (its own `/proc` entry, reading
other threads' stacks), and how long it takes.

**Next:** install dev87, restart iDrive, open App Manager several times,
also early after a start. On a freeze, collect the manager's debug log (the
`hmi threads` lines), `sloginfo` and the dump folder with its path. The main
thread's line `on <address> [count owner]` names the lock's owner thread.

## 2026-11-17 evening: iDrive freeze on launch analysed; head unit diagnostics documented

No code change. dev82 to dev86 (gamepad, volume display, DSI exit fix, up to
`6c2c7a8`) were made in another session and have no entry here; the car ran
release 83 for this.

**Freeze** ([docs/car-issues-2026-11-17.md](docs/car-issues-2026-11-17.md)):
opening App Manager froze iDrive until the watchdog reset it. The plugin's
`PluginApp::triggerEvent(50)` (the hold on iDrive's blank state) never
returned, twice in the manager's log (boot 5 at 43 s, boot 3 at 756 s after
an installer update had replaced the loaded plugin); every other focus event
returned in 4 to 52 ms. Meanwhile three of HmiMain3D's `bmw::Looper` threads
spin (47 CPU seconds each by 2:06) and the main thread waits for the plugin
framework's global lock in `libhmiplugin.so`. Read from the dumper's
`HmiMain3D.core` and the watchdog's thread report. Who holds the lock and why
only some opens hang: **not established**. dev79's freeze has the same
signature, so dev81's "LayerManager silent" was a consequence, not the cause.
Ruled out: the volume display's second window (present in seven opens that
worked).

**Diagnostics** ([docs/car-diagnostics.md](docs/car-diagnostics.md)): the
dumper and watchdog files and how to match them to an event (not by pid: the
boot is deterministic), the storage layout from `df`, the tools on the head
unit (`pdebug`, `qconn`, `tcpdump`, `hogs`, ...), notable `/lib` and `/dev`
entries. New `tools/qnx_core.py` reads the car's QNX 6.5 ARM cores (threads,
states, blocked objects, objects, stack scan); checked on the 2026-11-17
cores only, no automated test (the cores are BMW's and stay out of git).

**Next:** see the issue document's next steps: disassemble
`triggerEvent`'s wait in `libAppFramework.A.so`; next time, copy the dump
folder right after a freeze and note its path on the head unit (not recorded
this time). Until then: restart iDrive after a plugin change, and avoid
opening App Manager in the first minute after a start (a precaution).

## 2026-11-17: kit dev81 — no restart loop when LayerManager is silent; media libraries for apps

**dev79 in the car, iDrive frozen at start** (`idrive.log`, sloginfo of one
boot, on the NAS 11:35). The user: the manager froze iDrive when starting and
seemed to start twice (iDrive's loading animation twice), also on dev80.

- +26 s: the plugin starts the door (`--door`); the launcher starts the door
  worker and exits, as designed.
- +33 s: App Manager opened. The GUI's first LayerManagerControl call
  (`get screen 0`, made by `ScreenWindow::open`) got no answer; it was killed
  at the 3 s limit, the GUI exited (no panel size) and the launcher restarted
  it, each time with iDrive held again: 4 starts, then the launcher gave up at
  +61 s. LayerManager answered none of the 8 calls (4 by the GUI, 4 by the
  launcher's restore) from +33 s to +61 s; iDrive's own `ilm_` calls stop in
  that window too, and its surface 256 drew no frame from +32 s to after +61 s.
- From +63 s iDrive's own processes miss their watchdogs on their DSI send
  threads (NBTCarHU, NavNBTEvoController, TunerApp; TunerApp killed at
  +111 s), and at +87 s three HmiMain3D looper threads used most of the CPU.
  Whether the manager caused LayerManager's stall is **not established**; it
  began right as iDrive reported its normal operating state (+32.7 s).
- The manager's own log of that boot was not uploaded; the GUI's exit reason
  above is from the timings, not from its log line.

**Fixes:**

- A GUI that gets no panel size from LayerManager ends with status 0
  (`exit (LayerManager did not answer)`), so the launcher does not restart it
  and iDrive is not held again; the driver opens App Manager again. On a
  trial a status-0 exit still counts as a failed start, as before.
- LayerManagerControl calls now run a wait hook every 250 ms; the GUI uses it
  to keep the plugin's liveness (1.5 s limit) and the launcher's heartbeat
  (3 s limit) while a call waits up to its 3 s. Before, any call slower than
  about 1.5 s could make the plugin drop the hold, and one at the limit made
  the launcher restart the GUI.
- Tests: `test_gui_ends_without_a_restart_when_layer_manager_is_silent`,
  `test_gui_keeps_its_heartbeat_while_layer_manager_is_slow`
  (`fake_layer_manager.py` takes `NBT_FAKE_LM_HANG_S` and `_ON`).

**Doom 1.0.5 runs with sound off** (`app-doom (4).log`, the user): with
`sound=0` it reaches `stage: running` and plays the demo. With sound on it
dies right after `S_Init` (same instruction both runs: pc and lr both moved
by 0x29000, a shared library loaded elsewhere). So the crash is on the sound
path after `libasound` failed to load; Doom should treat that as no sound
(left to the Doom work, outside this session).

**Media libraries for apps:** applications' `LD_LIBRARY_PATH` now ends with
`/opt/mm/lib` (`withMediaLibraries`, `hmi_environment.hpp`). From the car's
files (`~/src/reverse/opt`): `/opt/mm/lib/libasound/libasound.so` needs only
`liba2i_client.so` (in `/opt/mm/lib`) and `libc.so.3`, and the car's own
`/opt/mm/bin/mmlauncher.sh` puts `/opt/mm/lib` on its path. That QNX 6.5's
loader then finds `liba2i_client.so` for Doom's `dlopen` is **not verified**.

`gui_session.cpp` went over the line limit; its heartbeats and wait moved to
`src/gui/gui_wait.cpp`.

**Verified:** 265 tests (3 skipped), `pre-commit`, `qnx-check stack-report`.
**Not verified** in the car.

**Doom 1.0.6** (`~/src/reverse/nbt-doom` `0ddeef1`, sequence 7, test-app key;
not pushed). From the user in the car with 1.0.5: quit asked for y, and
tilting did not move the player (rotation turns). 1.0.6: while PrBoom waits
for y/n, the knob's push answers y and BACK n; a mixer without an audio
device turns sound off as the Sound setting does; the crash line is followed
by the library and symbol of pc and lr (`dladdr`); with its Debug log on,
each MOST line carries the first 8 data bytes. Why tilt does nothing is
**not established**: the manager's own logs record decoded inputs only, and
whether the manager's list follows a tilt in this car is not recorded.
Package SHA-256 `d96d21861df8c6c2807cb5f6776ee614894e2c709833cb2fdc410b70e67a604e`,
on the NAS as `doom-1.0.6.nbt`.

**Doom 1.0.7** (`ed48fa6`, sequence 8, not pushed; supersedes 1.0.6, which
was not tried): tilt fixed. The user: tilting does not move the player at
all. `0xC46` byte 4 is a clock starting at 1 (1 up, 4 right, 7 down, 10 left;
NBT-AAD's car capture and the HMI's `rcs::rotaryDeflectionToMost`, see
`docs/controller-input.md`); Doom read 12/3/6/9, so every tilt was a
diagonal. That a diagonal key pair moved nothing is not explained. The
manager's own tilt ranges already fit those values; no manager change. SHA-256
`8c218b2248c4018c167cf4e4fcb90e606494faca9a20d36437141200171a7af0`, on the
NAS as `doom-1.0.7.nbt`.

**Kit dev81:** 20338333 bytes, SHA-256
`b6880fe0c48e819f2143d35731fbbfc64c48bcc8656e60d977422fc5378023b7`, release
SHA-256 `e5782e79d2577193dec4181283d4208424c278e7e9332d5da292b2cf3015ffee`.
On the NAS, all 11 checksums verified there.

**Next car test:** update to dev81 (`phone\NBT-Manager-dev81.nbtr`), install
the APK, then Doom 1.0.7 with Sound on and its Debug log on. Expect either
sound, or `no audio device; sound off` and no crash; if it still crashes, the
lines after `FATAL` name the library. In a game, tilt each way, then save
Doom's log: its MOST lines show what a tilt sends. Open App Manager right
after a boot too: one attempt at most, no repeated iDrive animation.

## 2026-11-17: kit dev80 — dev79 in the car; 2-minute hold in the background

**dev79 in the car** (Manager log and app logs on the NAS, 2026-11-17 10:47-10:50):

- Application logs work: the test app's and Doom's open with the checked open
  (no `checked open failed` line, no errno 89). Why earlier kits failed is
  still not established.
- Each log shows the wrapper's line, `nbt-managerd: limits: core 0, files
  128, data 256 MiB, threads 64`. It is preceded by `unknown symbol: kdMain
  referenced from libKD.so.1`: libKD, loaded through iDrive's `LD_PRELOAD`
  which apps inherit, says so in any process without `kdMain` (already
  noted for LayerManagerControl). Harmless.
- Storage: `/var` is a link to `/fs/sda1`. `/var/opt/nbt-manager` is on qnx6
  `/dev/sda0t178` mounted at `/fs/sda1`: 380 MiB free of 491 MiB. The user
  thought it was a separate mount point with plenty of space: separate, yes;
  491 MiB in all.
- Durations: verification of Doom 352 ms, its install copy 1173 ms.
- `setup manager: 3 ms (failed)` right after the update was SETUP answering
  "on trial" (1), not a failure; logged `(on trial)` now (`8a16468`).
- **Doom 1.0.5** passes its install check, then dies at start with SIGSEGV
  (`addr=0x0 pc=0x11758dc lr=0x116f2f4`, stage `I_Init done`), right after
  `S_Init: default sfx volume 8`. Both addresses are outside Doom's `.text`
  (ends `0x1d67a8`), so the fault is in a shared library; which one is not
  established. Doom's crash handler could name it with `dladdr` (QNX 6.5's
  libc exports it).
- Doom's sound is off: `dlopen /opt/mm/lib/libasound/libasound.so failed:
  Library cannot be found` although the file exists. The car's
  `libasound.so` needs `liba2i_client.so`, which is in `/opt/mm/lib`.
  Applications get the HMI's `LD_LIBRARY_PATH` (`starter.cfg`
  `System_HMI_LD_LIBRARY_PATH`), which has no `/opt/mm/lib`; the media
  processes' path has it. From the car's files; the live value is not
  logged.
- Doom is being changed outside this session (1.0.5 built and committed at
  10:43); its fixes are left to it.

**App:** the car's network is now kept 2 minutes after the app goes to the
background (`e8bd018`, the user: switching apps to save logs disconnected
it). See the app's handover.

**Verified:** 263 tests, `qnx-check stack-report`. **Not verified** in the
car.

**Kit dev80:** 20336426 bytes, SHA-256
`484e47335704b010452dfca78f3fd9f0b11969026b1f693e209bfe020598e614`, release
SHA-256 `dab1d2ef7065a40a69adfbc7e2c096ec66ad12c2ebf17c5e1a3f61e03cff03b7`.
On the NAS, all 11 checksums verified there.

**Next car test:** update to dev80 from the phone
(`phone\NBT-Manager-dev80.nbtr`) and install the APK. Save a log to another
app and come back within 2 minutes: the car should still be connected.

## 2026-11-17: kit dev79 — application limits, slow steps off the loop, errno audit, storage

dev76 was not tried in the car yet. The user agreed to the three fixes that
need no car: limits for applications, the worker's slow steps on a thread,
and no stale errno in the logs.

**Application limits on the car (`ed649d0`).** QNX 6.5's `fork()` fails in a
threaded process, and `spawn()`'s `struct inheritance` has no limits, so
since dev71 applications ran without any. The host now spawns its own binary
as `nbt-managerd --app-exec PATH ARGV0 ...`. That wrapper sets the limits,
writes them to the application's log (`nbt-managerd: limits: core 0, files
128, data 256 MiB, threads 64`) and `execve()`s the application. Checked in
the QNX 6.5 VM:

- The application keeps the wrapper's pid, its process group, the lower
  priority and its descriptors, and `waitpid` on the spawned pid works.
- `RLIMIT_NOFILE`, `RLIMIT_NTHR` and `RLIMIT_DATA` are enforced, and
  `RLIMIT_DATA` alone caps `malloc`.
- `RLIMIT_FSIZE` is refused with EINVAL for every value, so it is not set on
  QNX. The old fork path's file-size limit never applied in the car either.

The thread limit is new: the old path never set it.

**No stale errno (`0c97d09`).** I audited the log lines that print errno:

- The host's start: a bridge refusal and autostart are no system call, so
  they log no errno; the refusal logs its reason. Recovery logs its own
  reason. The other steps clear errno first. The preparation moved to
  `host_start.cpp`.
- SESSION no longer prints the loop thread's errno when the Wi-Fi Direct job
  failed: errno is per thread.
- START names the step that failed.

The storage checks behind `nbt_fs_open` were already setting errno on every
failure. So the application log's errno 89 came from a real call in the car.
Its cause is still not established; dev76's fallback log will name it.

**Slow steps on a thread (`ca9389d`).** The GUI pings the worker every second
with a 2 s timeout and then terminates it, and it waits 5 s for APPROVE. On
the loop, package verification, a new application's copy (done before APPROVE
answered), an update's copy and SETUP could exceed that with a large package.
They now run one at a time on `WorkerJob`'s thread (`worker_job.cpp`,
`worker_jobs.cpp`, `worker_tasks.cpp`), and the loop keeps answering:

- `/v1/upload` answers 202 `waiting` "Checking the package". The result
  (`verified` or `failed`) comes through `/v1/status`; the app already
  follows it, and since `087b7d4` it says "The car is checking the package."
  A forged signature or a version that is not newer is therefore refused
  through status, not with HTTP 400.
- GUI `STATUS` stays 6 during verification. A CANCEL meanwhile applies when
  verification ends.
- A new application's APPROVE answers at once, then state 3.
- SETUP's reply waits for its job, as SESSION's waits for Wi-Fi Direct.
- `busy()` refuses sessions, uploads, START, STOP and removals meanwhile.
- The debug log gives each step's duration (`verification doom: 812 ms`), so
  the next logs show the car's real times. Without a thread, a step runs on
  the loop as before.
- OpenSSL 0.9.8m (the SDK's) needs locking and thread id callbacks before
  two threads use it; the worker sets them at start (`crypto/threads.cpp`).

**Where `/var/opt/nbt-manager` really is (`a706228`).** The user thinks
`/var` is a separate mount point with plenty of space. Not established: the
car's `mount` output (nbt-inventory-5) shows no `/var`, only `/var/pps`, and
nothing there names its device or free space. At start the worker now logs
`storage /var/opt/nbt-manager: <type> on <device> at <mount point>, real path
...; N MiB free of M MiB; /var is a ...`. The QNX 6.5 VM only showed that
these calls work (they fail with ENOTTY on `/dev/shmem`, which the line then
says). The car's layout comes from that line alone.

**Factory reset of iDrive.** The user thinks it is most likely iDrive's
failsafe after too many restarts in a row. The logs on the NAS have no trace
of it: not established.

**Verified:** 263 tests, ThreadSanitizer (now covering the transaction tests
too), `qnx-check stack-report`, the QNX 6.5 VM checks above. **Not
verified** in the car.

**Kit dev79:** dev77 was built before the app's "checking" text was
committed, and dev78 before the storage line; neither was published. dev79:
20335699 bytes, SHA-256
`253c192f3b671b313cbc2b5cd7f4fafaed8e5d8e82f92a1653b5bd41e436ef11`, release
SHA-256 `9c5e4e5c26282fdb8c4b203cd7f0eeccaafc36aa39e528f66e2451e1540e9d4d`.
On the NAS, all 11 checksums verified there.

**Next car test:** dev76's list still applies: update from the phone
(`phone\NBT-Manager-dev79.nbtr`), start
the test app and Doom, then Show log for each, and share the Manager log.
Also look for:

- each application log's first line, `nbt-managerd: limits: ...`;
- the Manager log's `verification <app>: N ms` and `install copy` lines;
- its `storage` line, which says where `/var/opt/nbt-manager` is and how
  much space it has.

## 2026-11-16: kit dev76 — the application log no longer depends on the checked open

The user: "Fix the app logs before I try again. ... This is mandatory." dev75
was not tried in the car.

**Established:**

- On the car, the host's `nbt_fs_open(NBT_FS_APP_LOG, NBT_FS_APPEND)`
  failed on every start with errno 89. The worker read `output.log` as
  `missing` before the first failure and as `0 bytes` after, so the
  `open()` created the file and something after it failed.
- The same `nbt_fs_open(..., NBT_FS_APPEND)` of `manager/debug.log` works in
  the same process. The host lock in the same `state/<app>/` folder opens
  through the same `openOwnedFile` checks.
- In the user's QNX 6.5.0 VM (qnx6 root, like the car), a program doing the
  host's sequence works throughout: threads, `spawn()` with the
  manager's flags, then open (`O_WRONLY|O_APPEND|O_CREAT|O_EXCL`),
  `fcntl(F_SETFD)`, `lstat`, `fstat`, the identity check, all while the
  child runs.
- In the same VM, a successful `localtime_r()` changes errno (22). `note()`
  calls it just before the open. A check that sets no errno (only
  `openOwnedFile`'s lstat/fstat identity check did) then reports a stale
  value.

**Cause not established.** The fix does not depend on it.

**Built (`0a36e29`):**

- `openOwnedFile` sets ESTALE when the identity check fails.
- `AppLog::open`: when the checked open fails, plain opens follow:
  `O_WRONLY|O_APPEND`, then `O_WRONLY` and `O_RDWR` with a seek to the end
  before each write (no hole after the phone's Clear). Each is accepted only
  for a regular file of the manager's owner on its device, with one link
  and no group or other write. The folders were checked by `nbt_fs_path`,
  and QNX 6.5 has no `O_NOFOLLOW`. The debug log says
  `host <app>: application log <path>: checked open failed (errno N); opened
  with a plain <way> open`, or each way's errno.
- Tests force the checked open to fail (`NBT_TEST_APP_LOG_CHECKED_FAILS`),
  then `O_APPEND` too (`NBT_TEST_APP_LOG_NO_APPEND`, with a truncation while
  the app writes). The directory case is said too.

`9547ae0` restores the dev71 explanation, now verified: QNX 6.5's `fork()`
returns ENOSYS (89) once the process has a second thread. That comes from the
SDK's `libc.so.3` (it checks `_Multi_threaded`), QNX's 2009 libc source and
the 6.5 VM. QNX 6.6's reference describes a later libc.

**Verified:** 258 tests, `qnx-check stack-report`. **Not verified** in the car.

**Kit dev76:** 20304731 bytes, SHA-256
`acfe273e4228c6b04d9f751e1028b54231f3dc7515b1ad3327891a9a85fd8bca`, release
SHA-256 `abfefd8c3ae06cc3deb33d2e267da06facad5dd842f7395ec40048ea885eab16`.
On the NAS, all 11 checksums verified there.

**Next car test:** update to dev76 from the phone. Start the test app, then
Doom → Show log. Share the Manager log too: its `application log` line says
which open worked.

## 2026-11-16: kit dev75 — why the application log cannot be opened

**dev74 in the car** (`manager.log` and `idrive (3).log`, shared from the
phone): the Manager log works. Every application start logs
`host <app>: application log open failed (errno 89)`. In QNX 6.5's
`errno.h`, 89 is ENOSYS ("Unknown system call"). The QNX library reference
(6.6) gives ENOSYS for `open()` as "not implemented for the filesystem" and
for `fcntl()` as "the filesystem doesn't support the operation". The phone
reads `output.log: 0 bytes`. The file exists, since the worker opens it for
reading. The same host appends to `manager/debug.log` with the same flags
(`O_WRONLY|O_APPEND`; QNX 6.5 has no `O_NOFOLLOW`) on the same partition.
**Cause not established.** Doom then exits 0.5 s after start, without a
fault in the iDrive log, after "MOST socket closed by the application", and
is "blocked after faults" (3 faulty ends; hold it on the car, then **Unblock
doom**). How it exited went only to the failing application log.

**Built (`dd37022`):** once per host, the debug log gives the application
log's path, what `lstat()` says (mode, links, owner) and the errno of a plain
`O_WRONLY|O_APPEND` open. `host <app>: application exited (status N, signal
N, fault N)` is now in the debug log too.
`test_an_unwritable_application_log_is_explained_in_the_debug_log` covers
both.

**Verified:** 256 tests, `qnx-check stack-report`. **Not verified** in the car.

**Kit dev75:** 20303814 bytes, SHA-256
`f47f270176533d012ee79f25132c71c465d212844da140e461a2aa8c679db0e7`, release
SHA-256 `d6c6825044e5300b883229c2968df0b75f4da2e3e9cc973f2587f3c255370fef`.
On the NAS, all 11 checksums verified there.

**Next car test:** update to dev75 from the phone. Unblock doom on the car,
start the test app and Doom, then share the Manager log.

## 2026-11-16: kit dev74 — the manager's debug log on the phone

**dev73 in the car** (`idrive (2).log`, fetched with the phone): after Doom's
crash the manager comes back (render order 256 at 1:08, 1290 at 1:09), with
no black panel. Doom still crashes in its first frame (`ip 0x121c56c`). The
app's Show log for Doom reads empty. The user tests with the phone only: "If
you want better logs, make them available in the mobile app."

**Built:** `POST /v1/logs/manager` (`10dc76a`): `debug.log.1` then `debug.log`,
the newest 4 MiB of each, raw, empty while the debug log is off. The app
(`571e501`) has **Manager log** beside **iDrive log** on the car's page; an
empty reply says to turn on Debug log in App Manager's settings.

**Why the app log is empty: not known.** Every phone read logs
`phone: <app> log output.log: N bytes` (or `missing`), and a failed write
logs `host <app>: application log <step> failed (errno N)`. The next manager
log answers it.

**Verified:** 255 tests, app typecheck, lint, 78 Jest tests, the car-link and
device-key unit tests, `qnx-check stack-report`. The kit's APK bundle holds
the new route. **Not verified** in the car.

**Kit dev74:** 20303288 bytes, SHA-256
`437f5718f20e5c7306bb0356c5dd9c7666793e14e5cc1e404c00963cd6c78414`, release
SHA-256 `a9b71eafe0bf633221a8670ac950e9400789191aab5142dceb39b997f41594ff`.
On the NAS, all 11 checksums verified there. Install the APK as well.

**Next car test:** install the dev74 APK on the phone and update the manager
from it. Turn on Debug log in App Manager's settings, start Doom, then on
the phone: Doom → Show log, and the car's page → Manager log. Share both to
the NAS.

## 2026-11-16: kit dev73 — black panel after an application crash; "not newer" refused clearly

**dev71 in the car** (`idrive.log` on the NAS, the user's report): applications
start and the test app works. App logs stay empty (corrected 2026-11-17:
this entry first said they were written). The manager was updated
from the phone. Sending the installed test app again "failed verification".
Doom makes "iDrive crash": the panel goes black (first try: back to the
manager).

**Doom, from `idrive.log`:** Doom (pid 1196146) created surface 1291, called
`eglSwapBuffers` once and faulted (`signal 11 code 0 ip 0x10d756c`; Doom's
program, code and data, ends at `0x6f0204`: the address is outside it;
what is mapped there is not known). After the fault
the GUI handed the panel to 1291 (`render order 0x50b`). LayerManager closed
the window but kept 1291 in the render order. The GUI's once-a-second check
(`holds()`) read only the render order, so it never saw the application
leave. iDrive did not crash; its surface 256 was off the layer and nothing
put a surface back.

**Built:**

- `47e9ca2`: `display::Placement` also asks `get surface` whether the
  surface still has a window. Two misses in a row mean the application left
  ("surface 1291: its window is gone"), and the manager is shown again.
  `test_gui_comes_back_when_the_application_window_is_gone` fails on the
  previous code (verified against a rebuilt old binary) and passes now.
- `6b15384`: `Worker::uploadEnd` refuses a package whose sequence is not
  above the installed high-water mark, before the car's review: "Package
  rejected: version X is not newer than the installed Y". It used to fail
  only at install, as "release sequence rejected". The phone shows it after
  "The car reported a failure."

**Verified:** 254 tests, `qnx-check stack-report`. **Not verified** in the
car.

**Open:** Doom's own crash. Its app log should now hold `[doom] FATAL sig=…
addr=… stage=…`. Ask for it (app: Doom → Show log → Share) with the debug
log. Reproduce on the host if the stage points at video or sound init.

**Kit dev73:** 20302737 bytes, SHA-256
`0d8b6a38829990adef656e67b93609bbd608c148add285cafa443fccb933d919`, release
SHA-256 `dcbad042a2ce85716f500ec0e3db087ef1147df2a2b66f36714d72b86216810d`.
On the NAS, all 11 checksums verified there. dev72 was built and not used.

## 2026-11-16: kit dev71 — applications start again (spawn, not fork, on QNX)

**dev70 in the car** (`debug-1794852944.log`): pairing works. No application
starts: `command START` then `state failed: cannot start application` 14 ms
later, no host, no application log.

**Cause (verified 2026-11-16, after a wrong retraction):** QNX 6.5's `fork()`
fails with ENOSYS (89) once the process has a second thread. The SDK's
`libc.so.3` checks `_Multi_threaded` in `fork()`, then stores 89 in errno
and returns -1 (disassembly). QNX's 2009 libc source (`lib/c/1/fork.c`,
"TEMPORARY until libc is fork safe wrt mutexs") does the same, and a QNX
6.5.0 VM (the user's, on the NAS) returns -1/89 for `fork()` with a second
thread. QNX 6.6's reference describes a later libc. Two places forked on the
car:

- `src/process/spawn.cpp` `detach()`: the worker forks twice to start an
  application's host. The worker has threads (MOST client, Wi-Fi Direct job).
- `src/process/child_sandbox.cpp` (`663008f`): the host forks to start the
  application, after opening its MOST client.

This entry first said exactly that. A later correction called it unverified,
because the 6.6 reference says otherwise; the 6.5 library settles it.

**Fix:**

- `48f8f07`: on QNX the host is spawned as its own session leader
  (`SPAWN_SETSID | SPAWN_NOZOMBIE`) on `/dev/null`.
- `ad21b23`: on QNX the sandbox spawns the application in a new process group
  with default signals and a lower priority (`SPAWN_EXPLICIT_SCHED`).
  `spawn()` cannot set another process's resource limits, so 663008f's
  memory, file, thread and core limits apply on the host build only (fork),
  and the `nbt_apps` scheduler-partition join is gone. To restore them on the
  car, a small spawned wrapper would set them and then exec the application
  (not done).
- `Worker::start` logs which step failed, with errno.

**Verified:** 253 tests, `qnx-check stack-report`. The host build still
forks, so these tests do not exercise the QNX path. **Not verified** in the
car.

**Kit dev71:** 20302318 bytes, SHA-256
`58f5bf20a395117a59f54cc5cc00605e64db5adddaf3a311b192d2d5b8ccc2c9`, release
SHA-256 `999335a968071f24070944b04cc1f6f0cc5b0e37f4b6f6ec5344fbde59d26427`.
On the NAS, all 11 checksums verified there.

**Next car test:** install dev71, open App Manager for 10 s, start the test
app and Doom, Show log; send the debug log.

The manager can also be updated from the paired phone (procedure 8b, the
kit's `phone\NBT-Manager-dev71.nbtr`); the user reports that path worked
with earlier kits.

## 2026-11-16: kit dev70 — WFDPass.conf's passphrase; Install test app explains its refusal

**dev69 in the car** (`debug-1794851767.log`): `MOST passphrase differs from
WFDPass.conf; dhcpd for wfd0 running; its config present; leases 0`, still 0
at the end. The phone associated and dropped eight times without a lease: the
WPA handshake failed with MOST's passphrase.

**Built:** `4602e28` puts `WFDPass.conf`'s passphrase in the Wi-Fi code (dev45's
working pairing and NBT-AAD use it); MOST's is the fallback when the file
cannot be read. `ce08356`: the launcher says `manager update awaiting
confirmation` when a staged manager release waits for its trial (it said
"manager operation is in progress" for that too), and the installer's
Install test app explains both: close App Manager, or open it for 10 s and
close it.

**Verified:** 253 tests, `cargo test --workspace`, `qnx-check stack-report`.
**Not verified** in the car.

**Kit dev70:** 20307683 bytes, SHA-256
`cd681f87a2e12d1899607c6c4234923a72e62b046f7973ac6c93246a45a73b22`, release
SHA-256 `d088140decc9d4c7a71713d0b28699560d4c893dd17e879fec18812ecd9cb04c`.
On the NAS, 11 checksums verified there.

**dev70 in the car** (`debug-1794852944.log`, `slog-1794852947.log`,
`installer-1794852713.log`): **pairing works** (the user's report). No
application starts: under investigation.

## 2026-11-16: kit dev69 — Wi-Fi Direct beside Android Auto; why a phone cannot join

**dev67 in the car** (`debug-1794850561.log`, `slog-1794850564.log`,
`installer-1794850417.log` on the NAS):

- Pair a phone works on the car. The group came up 2 s after
  `SelectWLANState3(1)` (`DIRECT-03-BMW08722`, key from MOST
  WLANConfiguration) and the code showed twice. The phone could not join
  ("Could not join the car's Wi-Fi"). The Wi-Fi driver logged
  `UAP_FW_STA_CONNECT` and then `STA_DISCONNECT` 4 to 8 s later, five times.
  The car's log does not say whether the WPA handshake or DHCP failed.
- Suspects, both new since the iDrive reset: dev67 is the first build to
  advertise MOST's passphrase (dev45's working pairing used `WFDPass.conf`),
  and the group no longer starts at boot. The car's inventory shows
  Connectivity running `dhcpd ... wfd0` (`/var/opt/conn/dhcpd.wfd0.conf`,
  pid file `/tmp/dhcpd.pid.wfd0`) with `mwud`; whether that `dhcpd` runs
  when the manager switches the group on is unknown.
- "Install test app" failed with "manager operation is in progress": App
  Manager was still open, and its launcher holds the manager operation lock
  while it runs (`src/launcher/supervisor.cpp`, unchanged since `1bbd5dd`).
  Not a regression. Close App Manager first. The installer's message should
  say so (not done).
- "SelectWLANState3 answer op 6 not understood" was our own request echoed
  back; no longer logged.

**Built:**

- `0a59018`: when the manager switched the group on, it leaves it on at the
  end if NBT-AAD uses it (`src/worker/android_auto_wifi.cpp`: `NBT_ALT_WIFI`
  in `/opt/aa/nbt-aad.env` wins, else the "Use WiFi Direct Network" flag
  file). `docs/android-auto-wifi.md` covers coexistence on the car and the
  phone. On Android 12 and later the phone shares one connection to the
  same network between the app and Android Auto (AOSP `WifiNetworkFactory`).
  A join to another network replaces Android Auto's.
- `4bc73de`: debug-log lines `Wi-Fi Direct check: MOST passphrase same
  as/differs from WFDPass.conf; dhcpd for wfd0 running/not running/no pid
  file; its config present/missing; leases N` when the session is ready, and
  `... at session end: ... leases N (M at start)`. No secret is logged.

**Verified:** `make pre-commit test` (252 tests, 3 optional skips; C++
checks for NBT-AAD's settings and the lease count), `make tsan`,
`qnx-check stack-report`. **Not verified** in the car.

**Kit dev69:** `dist/nbt-manager-windows-kit-dev69.zip`, 20307501 bytes,
SHA-256 `1a66b75150dff6d9648009c709c7203ac019e88c722229ef5315bbc07db46701`,
release SHA-256 `6e1f34e211e669623911206db0510080944998e8f38b48978206a89dd0476073`.
APK versionCode 69. On the NAS (zip and `nbt-manager-dev69/`), all 11
checksums verified there. dev68 was built and never used. Its NAS copy is
incomplete: the user interrupted it.

**Next car test:** debug log on, Pair a phone, scan from the app. Send the
debug log: the `Wi-Fi Direct check` lines say which of the two failed. A quick
test without a new kit: scan the car's code from Android's Wi-Fi settings and
note its message ("Check password" or an IP address failure).

## 2026-11-16: kit dev67 — Pair a phone reads the car's Wi-Fi Direct answer

**dev66 in the car** (`debug-1794848654.log`, `slog-1794848666.log` on the
NAS): the manager stayed open through Pair a phone (the threads work), and
the slow-frame lines showed at most one frame per 10 s over 33 ms, the slowest
38 ms. Pair a phone said there was no Wi-Fi. The worker sent
`SelectWLANState3(1)` on MOST A0.03 at 110.8 s; the system log shows the
driver switching `wfd0` to the access-point role at once and
`MLAN_EVENT_ID_UAP_FW_BSS_START` 2 s later, and the car reported
WLANState3 = 1 (the restore path runs only on that). The worker still timed
out at 10 s and switched the group off (`SelectWLANState3(0) timed out`
too).

**Cause and fix (`227e4df`):** the method report OpTypes in
`include/nbt/worker/wifi_direct_most.hpp` were swapped. MOST: ErrorAck 9,
ProcessingAck A, Processing B, Result C, ResultAck D, Error F (Vector's CAPL
MOST documentation lists the same). The car's ResultAck (D) was read as
ProcessingAck and ignored. The fake car in `tests/test_wifi_direct.py`
answered with the same wrong values, so nothing failed. Now:

- ProcessingAck 0x0A, ResultAck 0x0D; `tests/test_most.cpp` pins them.
- `WfdMostClient::selectState` also completes when WLANState3 reports the
  requested state after the request ("WLANState3 now N; SelectWLANState3 not
  answered" in the debug log).
- Answers to SelectWLANState3 for another handle, or with an OpType not
  understood, are logged with their OpType.

**Verified:** `make pre-commit test` (251 Python tests, 3 optional skips, and
the C++ tests), `make tsan` (clean), `qnx-check stack-report`. New test
`test_most_wfd_state_report_alone_completes_the_switch` (the car answers only
with the state report, as dev66's did) fails on the previous code and passes
in under 5 s. **Not verified** in the car: which answer the car actually
sends (the debug log will show the fallback line if it is not a ResultAck).

**Kit dev67:** `dist/nbt-manager-windows-kit-dev67.zip`, 20303436 bytes,
SHA-256 `4a60967bb1e999ecb5cbe3148c7646d5619c171a0409374df313afa721f91f29`,
release SHA-256 `d6973e2b89c5923b0970585822597a54867293116ce2bdb368072b8512902b8a`.
APK versionCode 67. On the NAS (zip and `nbt-manager-dev67/`), all 11
checksums verified there.

**Next car test:** debug log on, Pair a phone: the Wi-Fi code should appear
within a few seconds. Then pair from the app, install the test app, Show log
(install-check lines), Doom. Send the debug log.

## 2026-11-16: kit dev66 — threads for the GUI's worker requests and Wi-Fi Direct; 30 fps

The user asked for proper threads with locks for the parts that block ("no
wonder the gui is sometimes not responsive"), then a 30 fps target. pthreads
via `core::Mutex`/`LockGuard` (`nbt/core/mutex.hpp`); `<thread>`/`<mutex>`
stay banned by `tools/check_cxx.py` because the car's libstdc++ 6.0.13 lacks
them.

**Built:**

- `3f43393` GUI: `WorkerChannel` (`src/gui/worker_channel.cpp`) owns the
  worker connection on its own thread. The UI thread submits one request and
  takes the reply when it is there (woken through a pipe); the controller
  already ignored keys while a request was outstanding. The thread asks for
  the list when idle for 1 s; the heartbeat ('R') goes on while the worker
  answered within 3 s or a request is in flight within its deadline.
  Readiness still needs a LIST answered first. A lost connection fails any
  queued request. Replaces the keep-alive of `97e18e8`.
- `3509641` worker: `WifiDirectJob` (`src/worker/wifi_direct_job.cpp`) runs
  the up-to-10 s Wi-Fi Direct preparation on a thread; SESSION is answered
  when it ends (`ipc_session.cpp`), the GUI's later commands wait
  (`ControlHandler::replyPending`), the loop keeps running and does not
  reopen its default listener meanwhile.
- `92ae5d6` 30 fps: `FramePacer` (`src/gui/frame_pacer.cpp`). The loop comes
  round at least every 33 ms (it waited up to 100 ms) and draws a changed view
  at most once per 33 ms; frames slower than 33 ms are reported in the debug
  log ("N frame(s) slower than 33 ms in 10 s, slowest M ms").
- `make tsan`: the GUI channel probe and the Wi-Fi Direct session tests under
  ThreadSanitizer.

**Verified:** `make pre-commit test` (250 Python tests, 3 optional skips, and
the C++ tests), `make tsan` (clean), `qnx-check stack-report`. New tests: a
worker answering SESSION after 3.5 s (the UI loop keeps polling, the worker
counts as alive), a 1.5 s preparation with LIST and QUIT queued behind
SESSION (answered in order), 30 knob detents in a burst (frames at least 30
ms apart, first and last within 50 ms of their input; failed on the old
loop). **Not verified** in the car: frame times on the head unit's CPU
(watch the debug log's slow-frame lines), pthreads on QNX 6.5 in these two
places (the DSI client already ran one).

**Still in the worker's loop:** SETUP (plugin copy, up to 120 s allowed),
verifying an uploaded package (up to 64 MB), the install's binary copy. SETUP
fits the SESSION pattern; the other two change install state and need their
results applied back on the loop. The launcher and app hosts are separate
processes with nothing else to serve; input already has its own thread (the
DSI library's).

**Kits:** dev65 (threads, before 30 fps) built, not distributed.
**Kit dev66:** `dist/nbt-manager-windows-kit-dev66.zip`, 20303239 bytes,
SHA-256 `ba2dd01c812758ce121813f4b5184ebb93ecf8f7b75d3e419834b1fef1f48c69`,
release SHA-256 `14edd6e1fd50a97f00541f170c87709393433a1b01557aad8f7b5a8a64b1bd8f`.
APK versionCode 66. On the NAS (zip and `nbt-manager-dev66/`), all 11
checksums verified there.

**Next car test:** as for dev64 (debug log on, restart iDrive, pair, install
the test app, Show log, then Doom), and turn the knob quickly on the app list:
the debug log's slow-frame lines say whether 30 fps holds.

## 2026-11-16: kit dev64 — pairing no longer closes the manager; install checks logged

**Pair a phone closed the manager (dev62, in the car).** Logs on the NAS:
`debug-1794840862.log` (plugin lines only: the debug log was switched on after
the GUI and worker had started), `slog-1794840865.log`,
`installer-1794840775.log`. Cause (from the code, consistent with the plugin
log releasing the window 4.8 s after showing it): since `65412c3`, `SESSION
wfd0` prepares Wi-Fi Direct over MOST inside the worker's loop for up to
10 s (`WifiDirectController::prepareSession`), while the GUI waits on the
reply (15 s) in a blocking `WorkerClient::request` and writes no heartbeat.
The launcher stops a GUI silent for 3 s ("manager GUI heartbeat or lease
lost"), and the worker with it. **Fixed** (`97e18e8`): once ready, the GUI
writes 'R' every second while a request waits; the request keeps its own
deadline. The same applies to the 120 s setup request. Not addressed: the
worker's loop (phone HTTP, door) still stalls for those seconds. The slog also
shows `ASSERTION FAILED … gcf_core … cdispatcher.c` lines and the Wi-Fi
radio re-initialising at 00:00:38, about 11 s after the manager opened;
whether the `A0.03` enable caused them is not established.

**Install health checks reach the application log** (`7519a2a`). The worker
runs a new version for up to 45 s before confirming it, itself and without a
host; that run's output and its end went only to the debug log. That explains
"No log yet" for an application installed but not yet started, and Doom
crashed about a second into a run that may well have been this check. The
candidate's stdout and stderr are now a pipe the worker drains into
`state/<app>/output.log`, with `install check of <version> started (pid)`, how
it ended, and `install check passed` / `failed` (PROTOCOL.md, Logs).

**Verified:** `make pre-commit test` (248 Python tests, 3 optional skips, and
the C++ tests), `qnx-check stack-report`. New tests: a candidate that prints
and dies by SIGSEGV through the worker's path (`process-probe check`), the
daemon install test finds the check's lines, a worker answering SESSION after
3.5 s gets heartbeats (`client-probe keepalive`). **Not verified** in the car.

**Kits:** dev63 was built with the log change only and is not distributed.
**Kit dev64:** `dist/nbt-manager-windows-kit-dev64.zip`, 20289948 bytes,
SHA-256 `079e5070e093d2ccb6126e5854a78086a301e272dfb06e4ded0f1e752e8f1589`,
release SHA-256 `6854091b13519fe8fbc9df7c61fe309eb21237cb09a92582e9e063fe35262b15`.
APK versionCode 64 (includes the app's pairing fix `633b64f`). On the NAS (zip
and `nbt-manager-dev64/`), all 11 checksums verified there.

**Next car test:** update to dev64, switch the head unit's debug log on, then
restart iDrive so the GUI and worker log too. Pair a phone; then install the
test app from the phone and Show log at once: the install check's lines should
be there. Then Doom.

## 2026-11-16: application-log diagnostics before the next car test

The user confirmed **dev59**, with "No log yet" for every application even
without an iDrive restart. The earlier snapshot-loss explanation below does
not account for this case. The car's exact failure remains unconfirmed.

Host investigation reproduced two ways to hide failures behind that message:
a nonempty log whose seek fails was served as HTTP 200 with zero bytes, and
a failed write left an empty file without reporting the failure. Normal app
capture and signed downloads through the worker and door passed; the mobile
protocol contract passed too. Its application-log case covers an app that
never ran, so the crash test now also downloads the host-produced log over
the signed API and checks its complete contents.

The reader now returns 503 for stat/seek failures, including system-log
preparation. With the debug log enabled, application-log requests name each
missing file or its byte count and report read failures with errno. The host
reports open, stat, write, sync and rotation failures once per operation per
host. Failed rotation retains the existing bounded log and drops the new
chunk. Fault injection is confined to the new host test binary's linker
wrappers; no runtime fault hooks were added to production.

**Verified:** `make pre-commit test` (238 Python tests, three optional QR
decoder/encoder skips, plus the C++ tests including the new fault tests),
the mobile `yarn test:contract`, and QNX `qnx-check stack-report`. The fault
test failed before the fix. Read preparation failures return 503, writer
failures are reported once and can recover, failed rotation keeps the log
bounded, and a real crashed app's stdout/stderr and signal reach the signed
HTTP download.

**Kit dev61:** `dist/nbt-manager-windows-kit-dev61.zip`; signed manager release
SHA-256 `ed2e04bf42dfa4ebefbd9eaaeb1b06817970edf2c51a807d31831a29beb0cb14`.
APK versionCode 61, unchanged app code with the new installer release embedded.
Archive: 20242192 bytes, SHA-256
`982ea86ca6bb9e92d36ba2afdae6f2c42500c41fdb255fc31208c1e7579649e1`.
Folder and archive are on `/mnt/nas/public/nbt/`; all ten manifest entries
and the archive checksum were verified on the NAS.

**Next car test:** update to the diagnostic release, switch the head unit's
debug log on, open App Manager, start the normal test app and leave it with
BACK. Open App Manager again, then Show log on the phone. Save the head
unit's debug log immediately, without clearing the app log. Keep App Manager
open for the request so it reaches the updated worker rather than an older
boot listener. The debug log separates missing/empty files from a failed
write or read. Test Doom after this baseline. No car validation is claimed.

## 2026-11-16: kit dev59 — the application log survives an iDrive restart

In the car with dev58 (confirmed by the user), Doom crashed, iDrive then
restarted by itself, and the phone found no Doom log ("No log yet"). The
iDrive log (`idrive.log` on the NAS) shows the crash: `run fault pid … signal
11 … ip 0x106056c …/apps/doom/bin/app`, about a second after Doom's first
`eglSwapBuffers` (its window was surface 1291, 1440x540). The address is
outside Doom's code (0x100000 to about 0x6f0000) and the libraries (about
0x78000000): most likely a jump through a bad pointer into its heap. iDrive
kept running at least 8 s after the crash in that log.

Cause of the missing log (inferred, not seen on the car): the QNX6 file
system keeps writes in memory until its next snapshot, so the host's freshly
created `output.log` was lost when iDrive restarted.

**Built** (`16ecac5`): `AppLog` syncs its notes at once and output within a
second (`syncDue`, also from the host loop), and reports an open failure in
the debug log. App (`70e3fa9`): a failed log request is now said on the page.

**Verified:** `make pre-commit test` (238), `qnx-check stack-report`, an
strace of the crash test (fsync after "started" and after the end).

**Kit dev59:** `dist/nbt-manager-windows-kit-dev59.zip`, 20239347 bytes,
SHA-256 `86bb307212f05eb869f8fdf270b7b89929df09db16569ce277bcc1931c197ad2`,
release SHA-256 `970e28158adeb73a90d50ad82c59076cd9bbb0c8f53ed1824d4ad88d9d73a05b`.
APK versionCode 59. On the NAS, checksums verified there.

**Docs:** `docs/app-bridge.md` no longer says an application's output
reaches only the debug log.

**nbt-doom** (`~/src/reverse/nbt-doom`, local commits only, no remote):
`c1e446a` logs to stderr only and removes the old `doom.log`; its README has a
Logs section. The packages on the NAS (`doom-1.0.3.nbt` and older) were built
before `c1e446a`: they still write `doom.log` too, but they also print each
line and the crash line to stderr, so `output.log` has them. A package from
`c1e446a` needs the apps signing key (`make doom-package`).

**Next:** run Doom on dev59, read its log (with Doom's own `[doom] FATAL`
line: stage and address); fetch the iDrive log right after iDrive restarts.
Find why a crashing application restarts iDrive: that must not happen.

## 2026-11-16: kit dev58 — application logs owned by the manager

The user's test application (nbt-doom) crashed with no information: an
application's output reached the debug log only while it was on, and how an
application ended was noted there only. The user asked for app logs and the
system log on the phone, and for the manager to own the logs (rotation,
deletion, download) rather than each application.

**Built** (`4178337`, `0507616`):

- `src/process/app_output.cpp`: the host makes its application's stdout and
  stderr a pipe and copies it into `state/<app>/output.log` (`src/log/app_log.cpp`,
  rotated to `.1` at 64 KiB), with `---` notes: started, exited with status,
  ended by signal (named), stopped for a broken contract (`nbt_process.fault`:
  not ready in 5 s, no heartbeat for 3 s, bad byte, lease dropped), stopped by
  the manager, or a refused start with its reason.
- `src/worker/phone_logs.cpp` and a streamed reply in `web_connection.cpp`:
  `POST /v1/logs/app` (the log, up to 256 KiB), `/v1/logs/system`
  (`sloginfo` into an unlinked file in `/dev/shmem`, polled from the worker's
  loop, newest 8 MiB, one at a time), `/v1/logs/clear` (empties `output.log`
  in place, removes `.1`). Worker and door. PROTOCOL.md "Logs".
- nbt-doom (`~/src/reverse/nbt-doom`, local commit `c1e446a`, no remote):
  logs to stderr only, no more `doom.log`, removes the old one at start.

**Verified:** `make pre-commit test` (238), `make fuzz`, `qnx-check
stack-report`; new host tests (crash with SIGSEGV, never ready, stop, exit
status, rotation), phone tests (read, clear while a writer appends, symlink
refused, door, 8 MiB system log, failing sloginfo), HTTP parser cases; the app
repo's contract test reads, clears and fetches the system log against the
host worker. nbt-doom: `make test`, `qnx-check`.

**Not verified:** in the car: sloginfo's run time and the reply over Wi-Fi
Direct; nbt-doom's output there.

**Kit dev58:** `dist/nbt-manager-windows-kit-dev58.zip`, 20238438 bytes,
SHA-256 `9f0309950485be803d424475b003fff8bae8abaa857e7bfb1dae42924bcf02f5`,
release SHA-256 `a870b98d304db00a62e5dcc1185b5a39d525d7552127fded31d2f92f774d8f6c`.
APK versionCode 58, release certificate `86feff0d…3f7d`. On
`/mnt/nas/public/nbt/` (zip and `nbt-manager-dev58/`), checksums verified
there. dev57 was built mid-way and not distributed.

**Next:** in the car, update to dev58, rebuild nbt-doom against it and run
it; read its log and the iDrive log from the phone (in-car procedure 9b).

## 2026-11-15: the app's open decisions answered

No code here changed. The user answered the three decisions in the app
repository's handover: its AGENTS.md now names the installer as the one
exception to car-side approval (NBT Manager itself, never applications); the
phone's SSH question keeps not naming the activation service; the app adds no
notice about where it keeps a car's key. **Next:** unchanged, the in-car test
with kit dev56.

## 2026-11-15: kit dev56 — Restart iDrive? when it is needed

The user noted that the Windows installer's Restart iDrive is hidden under
Test tools and asked for a modal when a restart is required.

**Built** (`49511a9`): the core sends `Event::RestartNeeded` after a completed
install and after an update that replaced the iDrive plugin (not when the
plugin was unchanged). The window keeps it in `restart_prompt` and, once the
operation has finished, shows an `egui::Modal` "Restart iDrive?" with Restart
iDrive and Later (Escape or a click outside is Later). Disconnecting withdraws
it. Test tools keep the button. `desktop/mobile` passes the event on as
`InstallerEvent::RestartNeeded`; the app shows the same question.

**Verified:** `cargo test --workspace --locked` (core tests assert the event
comes exactly with a completed install, and never for an unchanged plugin),
`native_bootstrap_over_ssh`, clippy (the two old warnings), the Windows
target, `cargo fmt --check`. **Not seen on screen:** the Windows modal itself
(it needs a connected head unit; no mock drives the window).

**Kit dev56:** `dist/nbt-manager-windows-kit-dev56.zip`, 20223970 bytes,
SHA-256 `c04f0db4b82d6240faaffd4979845354af6a4bea7001d12d5126d9257e5002aa`,
release SHA-256 `44e5f796b7a9722b2cd1dc0908e3dc4e63095aa8b7780c8d33d43487ae266198`.
APK versionCode 56, release certificate `86feff0d…3f7d`, with the app's
shorter text. On `/mnt/nas/public/nbt/` (zip and `nbt-manager-dev56/`),
checksums verified there.

**Next:** the in-car test with kit dev56 (phone over the adapter, or Windows
over the cable).

## 2026-11-15: kit dev55 — find the head unit by its SSH port after enabling it

The user asked why, after SSH is enabled, the installer keeps trying the
link-local address instead of the head unit's address from the adapter's
DHCP, and whether a port scan could find it. dev54 retried
`169.254.199.119` ten times (up to about 110 s, unreachable from a phone),
then searched the adapter's network once, which misses a head unit whose SSH
starts late.

**Built** (`2df8bbc`): when this device has no `169.254` address
(`discovery::needs_link_local`), `ssh.rs` no longer tries the built-in
address, before or after enabling SSH. After enabling, `connect_elsewhere`
searches the activation gateway's network for port 22 every 2 s for up to
60 s (`sweep::wait`; about 2 s per /24 round), then connects to the first
address that accepts, which must still pass `uname -s`. The gateway itself is
never a candidate. If nothing accepts, the log says so and suggests typing
the head unit's address under Connection settings. Over the cable nothing
changes.

**Verified:** `cargo test --workspace --locked` (35 core tests, with the new
`the_search_waits_for_a_host_that_starts_accepting_late`),
`native_bootstrap_over_ssh` with `build/launcher-host`, clippy (only the old
`map_or` warning). The phone library in the APK carries the new messages.
Afterwards, at the user's request, `desktop/` was formatted with `cargo fmt`
(`216af22`, formatting only, listed in `.git-blame-ignore-revs`). `make format`
now runs it too, and the pre-commit hook checks it whenever a staged file is a
`.rs` under `desktop/` (`make pre-commit` always checks it).

**Not verified:** in a car. Whether the head unit takes an address from the
adapter is still the open question; the log answers it ("SSH answers at …").

**Kit dev55:** `dist/nbt-manager-windows-kit-dev55.zip`, 20223135 bytes,
SHA-256 `045e55d15f9d3dcc0f861842b02632d732798999def232901d449108af386893`,
release SHA-256 `69c8a3d8cb837a61b3cca5f90c4f8a7e12debdd39fc940cc67073c8e284da8d3`.
APK versionCode 55, release certificate `86feff0d…3f7d`. On
`/mnt/nas/public/nbt/` (zip and `nbt-manager-dev55/`), checksums verified
there.

**Next:** the user tries "From the phone instead" with kit dev55.

## 2026-11-15: milestone 4 started — the installer on the phone (kit dev54)

The user asked to start milestone 4 assuming the head unit takes an address
from a Wi-Fi ENET adapter, untested until the next car session.

**Built** (`ad18bb5`, `925db3d`, `15b020b`, `b9e67b3`, and the kit commit):

- `desktop/` is now a workspace. `desktop/core` (`nbt-installer-core`) holds
  everything but the window: `ssh.rs`, `activation.rs`, `discovery.rs`,
  `sweep.rs`, `relay_codec.rs`, `release.rs`, `bench.rs`, the embedding
  (`build.rs`, new `bundle.rs`) and the built-in defaults (`lib.rs`). The egui
  window (`desktop/src/main.rs`, `terminal.rs`) uses it unchanged.
- `core/src/net.rs`: every socket opens here. On Android, car sockets are tied
  to the adapter's Wi-Fi (`android_setsocknetwork`) and the activation
  service's to mobile data, its name resolved there
  (`android_getaddrinfofornetwork`); the car network's own addresses replace
  the interface list for both searches. A computer configures nothing.
- `desktop/mobile` (`nbt-installer-mobile`, library `nbt_installer`): UniFFI
  0.32. `Session.connect(connection, networks, listener)`, then
  `confirm_activation`, `install`, `update`, `tool`, `disconnect`;
  `bundled_release`, `default_connection`, `controller_config_example`. The
  `uniffi-bindgen` binary is behind the `bindgen` feature. UniFFI's Kotlin
  needs error fields not named `message` (`925db3d`, `15b020b`).
- On a phone the unreachable-address hint and the no-gateway message say what
  to check instead of which addresses to set (`b9e67b3`).
- `tools/build_windows_kit.py` builds the phone app after the test packages
  and passes it the signed release, the test application and
  `NBT_INSTALLER_SOURCE` (this repository), and checks for cargo-ndk and the
  `aarch64-linux-android` target.
- The app side (module `car-install`, Install on a car screen) is in the app
  repository's handover.

**Verified:** `cargo test --workspace --locked` (41 tests, then 42),
`native_bootstrap_over_ssh` with `build/launcher-host`, `cargo check` for
Windows, clippy (only the two old warnings), the arm64 release library with
dev53's release embedded (4.8 MB). On the x86_64 emulator: the library loads,
the Wi-Fi is held, the built-in address fails, the phone hint and Enable SSH
question appear, the gateway search asks only from the Wi-Fi's address and
fails with the phone's message, and the session ends cleanly.

**Not verified:** anything in a car or on a real phone; mobile data alongside
the adapter's Wi-Fi; the relay over mobile data. One run of
`cargo test --workspace` exited 101 and three reruns passed: a flaky test I
could not reproduce; watch for it.

**Kit dev54:** `dist/nbt-manager-windows-kit-dev54.zip`, 20220007 bytes,
SHA-256 `d1a45b6cb4e87d57174a5c7f0089da44b0225b44f2485184acbcc96c088d74fc`,
release SHA-256 `d4c720173b6de209ddddccec39425e9b0c967e7b474a3df7ea7a9f2e6ac24f1d`.
The APK (versionCode 54, release certificate `86feff0d…3f7d`) carries
`lib/arm64-v8a/libnbt_installer.so` and JNA's `libjnidispatch.so`; the phone
library and the Windows installer both embed the dev54 release. The first
attempt failed in R8 on JNA's `java.awt` references; the app module now ships
JNA keep rules. On `/mnt/nas/public/nbt/` (zip and `nbt-manager-dev54/`),
checksums verified there.

**Next:** the in-car test of `docs/in-car-procedure.md` "From the phone
instead" with kit dev54. Its first question is still whether the head unit
takes an address from the adapter (the log says "SSH answers at …").

## 2026-11-15 (second capture): a whole cable session, and who is who

`installer2.pcapng` on the NAS, 572 packets over 8.5 minutes on the bare cable
after the bus sleep. It holds one complete successful run: SSH refused at
`169.254.199.119` (RST, five tries), the gateway found at `169.254.24.4`, the
activation relay on its port 6801, then SSH enabled and three sessions to
`169.254.199.119`.

- **Correction to the entry below: the head unit is `DIAGADR62`, not `63`.**
  ARP settles it: `169.254.199.119 is-at 00:1c:d7:a4:f1:ca`, which is the MAC
  in `DIAGADR62BMWMAC001CD7A4F1CA`. `DIAGADR63` (`44:ea:d8:e9:0b:96`) holds
  `169.254.199.99` and broadcasts on port 3804 — the address `desktop/src/main.rs`
  already records as a different module and the wrong default. The two are
  presumably the NBT's two processors, which matches the installer's "QNX and
  host processor responded".
- **The head unit really is a DHCP client**, so the inference the search rests
  on holds: `DIAGADR62` sends Discover in both captures. Still not confirmed
  that an adapter's server answers it, because neither capture had one: every
  DHCP message here is a Discover with no Offer or ACK, and every module falls
  back to link-local.
- **Listening on 7811 cannot replace the search.** Only the gateway announces
  there; the head unit never does. So finding the head unit on an adapter's
  network needs the port search or a typed address, nothing cheaper.
- **The gateway is back on `169.254.24.4`** after the sleep, confirming the
  adapter lease cleared.
- **It answers each asking socket separately**: two sockets drew three replies
  in all. Deduplicating by VIN (dev52) is what keeps that from being reported
  as more than one gateway.
- **The cable is fast**: SYN to SYN/ACK in 0.5 ms, which is the baseline to
  compare an adapter against when judging whether a link is slow.

## 2026-11-15 (capture): the car's modules are DHCP clients

A Wireshark capture on the ENET link (`installer.pcapng` on the NAS, 129
packets) settles the adapter questions and corrects the entry below it.

- **Every module is a DHCP client, head unit included.** Each asks with a host
  name that names it: `DIAGADR10BMWMACD8182B92E799` (gateway),
  `DIAGADR62BMWMAC001CD7A4F1CA` (the head unit; see the correction below),
  `DIAGADR63BMWMAC44EAD8E90B96`, plus an `INTEGRITY1`. They fall back to
  link-local only because the bare cable serves no DHCP. So behind the adapter
  the head unit also holds a `192.168.16.x` address: nothing needs configuring
  on a laptop or a phone, the head unit simply has to be found there. The
  earlier conclusion that it keeps only its link-local address was wrong; it
  came from SSH being refused at `192.168.16.105`, which is the gateway.
- **The gateway holds its address for a while, but a sleep does clear it.** It
  broadcasts a DHCP request asking for `192.168.16.105` back (`Requested-IP`)
  every 8-16 s and keeps the address meanwhile, which is why the cable cannot
  enable SSH straight after the adapter was used, and why a link-down did not
  help. After a few minutes of bus sleep the user found dev52 working again on
  the cable, so it does give up and fall back to link-local; the capture, 36 s
  long, only caught it still retrying. An earlier note here predicted a sleep
  would not clear it: that was wrong.
- **The gateway announces itself** from `192.168.16.105:6811` to
  `<broadcast>:7811`, carrying the same `DIAGADR`/`BMWMAC`/`BMWVIN` record the
  search asks for. Listening on `7811` would find it without asking; not built.
- **The search itself is correct**: the request leaves the ENET port from each
  local address as intended. Only the gateway's unicast reply is lost.

Code: `src/sweep.rs` searches the SSH port on the networks a gateway answered
on, before the Enable SSH offer and again after enabling it, and
`settings.host` becomes what answered. Gateways and this computer's own
addresses are left out, the link-local /16 is too large to search, and
`uname -s` still decides. 37 tests pass; `cargo check --target
x86_64-pc-windows-gnu` passes. Not yet tried in the car.

Kit dev53: `dist/nbt-manager-windows-kit-dev53.zip`, 17730942 bytes, SHA-256
`b1b5383f767f0beb23667486cfaa2c5559ed1d9b3ccb672006eaec6e3856e94d`, release
`478069480fd36be009fed40eeeb4817faff4c776ef08f4c325cf0005d47835e0`, APK
versionCode 53 (no app code change since dev51); every `SHA256SUMS.txt` entry
checked, zip and folder on the NAS, checked there. Next in the car: with the
adapter plugged in and no address set by hand, press Connect and read which
address the search used. Still unconfirmed: that the adapter's DHCP server
actually gives the head unit an address. The capture shows it asking on the
bare cable, where nothing answers, not receiving one. A capture taken with the
adapter plugged in, started before the OBD connector goes in, would show the
ACK to `44:ea:d8:e9:0b:96` and settle it.

## 2026-11-15 (later): slow-link timeouts, and the gateway's stale lease

Same car session, continuing the adapter work above.

- **The installer gave up where OpenSSH did not.** Over the bridged adapter
  every round trip is slower, and `uname -s` had 10 s for the whole channel
  open, command and reply, while login had 15 s. The probe gates Install and
  Update, so a slow link disabled both while `ssh` worked by hand. Both are now
  60 s (`AUTH_SECONDS`, `PROBE_SECONDS`), and the debug log records how long
  the handshake and the probe took, which names a slow link instead of leaving
  it to be guessed. Nothing is lost by waiting: every wait is in a `select!`
  with the command channel, so Disconnect ends it at once.
- **Restarting iDrive switches SSH off, and can strand you.** The user did,
  and then could not enable it again: that needs the gateway, the gateway was
  still holding the `192.168.16.105` lease from the adapter, and the laptop was
  back on the cable with an APIPA address (`installer-1794758835.log`: "asked
  from 169.254.104.166, 192.168.11.51"). The Restart iDrive button warns that
  SSH goes down; it does not know that turning it back on may be impossible.
- **Why no broadcast target can fix that.** The request does reach the gateway:
  each socket is bound to a local address, which pins the interface, and a
  limited broadcast is received by every host on the link whatever its subnet.
  The gateway answers unicast to the source address, and from
  `192.168.16.105` it has no route to `169.254`. The reply is what is lost,
  not the request.
- **A link-down does not reset the gateway.** Unplugging the OBD connector for
  30-60 s and returning to the bare cable left the lease in place, so the ZGW
  does not restart its DHCP client on link-up. A few minutes of bus sleep did
  clear it: dev52 then worked on the cable again.

Open, and what decides the phone path: whether the head unit takes an address
from the adapter at all. If it does, the installer can find it and nothing
needs configuring on laptop or phone; if it does not, no software on a
`192.168.16.x`-only machine can reach it, and the answer has to be the
adapter's DHCP server being off, or another adapter. Test: with the adapter in
and SSH on, scan `192.168.16.100-200` for port 22, or read `ifconfig` on the
head unit.

## 2026-11-15: the Wi-Fi ENET adapter in the car (dev52), and what it needs

Tried with kit dev52 and an Exctool adapter over its RJ45 socket, on a Surface
with Windows 11. Settles most of the plan's milestone-4 adapter questions, in
the car rather than by reasoning.

- **The adapter bridges.** The gateway answered HSFZ discovery across it at
  `192.168.16.105`, a DHCP address from the adapter
  (`installer-1794757125.log` on the NAS). dev51 would not have found it; the
  multi-address search of `cfdd09e` did.
- **Enabling SSH works over the adapter**, with the relay's internet on
  another connection ("Gate: open", "SSH: enabled").
- **The gateway is not the head unit and refuses SSH** at `192.168.16.105`.
- **The head unit keeps only its link-local address.** It took no DHCP address
  from the adapter, so `169.254.199.119` stays correct, and the computer needs
  a `169.254` address to reach it. With a fixed `169.254.1.10` the user
  connected and iDrive answered.
- **Windows adds a second address only once DHCP is off**, so the adapter's
  connection must be fully static: `169.254.1.10/255.255.0.0` for the head
  unit, plus `192.168.16.50/255.255.255.0` under Advanced for the gateway. The
  user had to drop DHCP and so had only the first.

Code: the connection failure now says this instead of only offering to enable
SSH, which cannot help when no address can reach the head unit
(`discovery::needs_link_local`, `ssh::unreachable_address`, shown in the log and
in the Enable SSH window). A port search for a head unit on the adapter's
network was written and then removed: the head unit is not there, and scanning
would have offered the car's root password to unrelated hosts.

Verified: `cargo test --locked` (34 pass; the new test covers the cable, the
adapter, no addresses, a routable address and a name) and `cargo check --target
x86_64-pc-windows-gnu`. `desktop/README.md` and `docs/in-car-procedure.md` (new
adapter section) carry the findings. Not in a kit yet; the car still runs dev47.

## 2026-11-15: phone on the car's Wi-Fi and home Wi-Fi at once

Reported by the user: the phone (a Pixel 6 in earlier sessions) stayed on the
home Wi-Fi and still reached the manager over the car's Wi-Fi Direct group.
Android holds the app's local-only network on a second Wi-Fi connection where
the phone supports it (Android 12 and later); other phones drop the home Wi-Fi
meanwhile. Nothing to change on the car. For the installer on the phone
(milestone 4), the adapter's Wi-Fi and the activation service's internet can be
used together on such phones. Details in the app repository's `HANDOVER.md`.

## 2026-11-15: the gateway search after a Wi-Fi ENET adapter

Reported by the user: after using an Exctool Wi-Fi ENET adapter (a generic
`ENET_WIFI` design, likely a Hi-Link HLK-RM04: DHCP server on 192.168.16.x),
the cable no longer enabled SSH; nothing answered the gateway search. Likely
cause, not confirmed in the car: the car's diagnostic Ethernet takes a DHCP
address when one is offered and falls back to `169.254` only without one, and
keeps the adapter's lease after it is unplugged. The search only broadcast from
the address the system picked.

- The search now asks from every local IPv4 address (new dependency
  `if-addrs` 0.15, feature `link-local`: by default it drops `169.254` on
  Windows), merges answers by VIN, and survives one address failing.
  **Connection settings** → Gateway skips the search. The failure message names
  the addresses asked from and the adapter case. `desktop/README.md` and
  `docs/in-car-procedure.md` (record step) describe both.
- Verified: `cargo test --locked` (33 pass, 4 new: the send plan, a failing
  source, the message, the Gateway field) and `cargo check --target
  x86_64-pc-windows-gnu`. Not verified: Windows interface listing with a
  `169.254` address, and the cause itself. In the car: with the cable, a
  Wireshark capture after ignition-on shows whether the car still uses an
  adapter address; the adapter's page (192.168.16.254) shows its mode.
- For milestone 4 (phone over the adapter): if the adapter hands the car a
  DHCP address, the built-in addresses stop being right. Turning the adapter's
  DHCP off on the car side keeps the car on `169.254`.
- On the bridge question (raised after the commit): the symptom itself says the
  adapter bridges rather than NATs, since in NAT mode its DHCP server would not
  reach the car on the WAN side. On a bridge, broadcasts pass, so the search
  over the adapter's Wi-Fi should find the gateway at its `192.168.16.x`
  address; the kit's log shows which.

Kit dev52: `dist/nbt-manager-windows-kit-dev52.zip`, 17714805 bytes, SHA-256
`2af84233b9e7ba57b064617caee19cccbbdb282615afc0cbfa4c23f145fd02c7`, release
`d016db529b5fccedf0f7130f556cd270f2cf55824cda9bae94cde5fb0bc544f1`, APK
versionCode 52; every `SHA256SUMS.txt` entry checked, zip and folder on the NAS,
checked there. No manager or app code changed since dev51.

## 2026-11-15: pairing under Settings, the code drawn large; kits dev50 and dev51

Requested by the user after dev47 in the car (dev49 not tried):

- `4fafcff`: the main menu lists the applications, then Settings (no Install or
  update: a paired phone's upload is reviewed from the main menu). The manager's
  Settings end with Pair a phone, which opens the session for the Wi-Fi details
  and goes straight to the Wi-Fi code, then Paired phones. The confirmation draws
  the six digits in Montserrat 48 on a highlighted panel (`UiView::code`), as the
  app does. Tests open sessions directly where they used the menu entry.
- Same commit: the self-update leaves the launcher and plugin alone when their
  SHA-256 match the release, and asks no iDrive restart (already so in `SETUP`;
  now tested). The build is reproducible: dev48 to dev51 have identical plugin
  files, dev51 a new launcher only. The installer's Update says whether the
  plugin was replaced (`--refresh-setup` prints `REFRESHED plugin`) and asks for
  a restart only then.

Kit dev51 (dev50 skipped): `dist/nbt-manager-windows-kit-dev51.zip`, 17705569
bytes, SHA-256 `eff7ee86a1704f42a33cc7f6a6aeb77696c79fdf598ef9f1a0428adc98b6d439`,
release `fb7c0b38…0cf3`, APK versionCode 51; on the NAS, verified. Host: full
suite, cargo 29 + native, pre-commit, `qnx-check`. Next in the car: Update from
dev47 (its plugin differs: one restart), pair through Settings.

## 2026-11-14: dev47 in the car; kit dev48

In the car (Pixel 6; logs on the NAS: `installer-1794686309.log`,
`debug-1794686662.log`, `slog-1794686666.log`):

- Installer: **Install** refused an installed manager ("use Update instead"),
  **Update** staged dev47, then the user removed and installed again before
  opening App Manager, so dev47's in-manager refresh never ran (it waits for an
  opening). The fresh install's `SETUP` found everything current. Fixed in
  `5da844a`: **Update** now runs the staged release's launcher
  (`--refresh-setup`, `nbt_manager_refresh_staged`) between its remounts and
  replaces the launcher and plugin at once; the in-manager `SETUP` stays as the
  fallback and for phone updates.
- Phone: pairing, reading applications and installing `test-v2` worked. The
  dev47 manager release sent to a dev47 car was refused by the car ("not newer",
  correct), but the app showed "malformed": the car answers a rejected upload 400
  and the app now reads `/v1/status` for the reason (app `b147e46`).
- Opening from the phone did not open the manager, and opening it by hand
  afterwards froze iDrive until it restarted by itself about a minute later. The
  debug log had been switched off before, so nothing was recorded. Likely cause,
  unproven: the plugin fires iDrive's open event (52) from its 1 s timer outside
  the manager's menu. Decided with the user: `aa3b029` turns it off
  (`kOpenFromPhone` in `plugin/phone_runtime.cpp`): no car-data subscriptions,
  every wake answered `off`, the setting leaves the manager's Settings. The door
  still serves lists and settings. Next for it: a logged test build.
- `b325194`: the log rotation test counted on 2000 lines; at 620763 s of uptime
  lines are about 80 bytes and 1956 fit.

Kit dev48: `dist/nbt-manager-windows-kit-dev48.zip`, 17651944 bytes, SHA-256
`abafbd21a20908cea93063c3880dc42bec3bc343f950a1e47534cb79e104e76c`, release
`9b067c50…a908`, APK versionCode 48, `phone/NBT-Manager-dev48.nbtr`; on the NAS,
verified. Host: 230 tests, cargo 29 + native, pre-commit, `qnx-check` (plugin
imports baseline without the car-data calls). Next in the car: installer
**Update** to dev48 (log: "Launcher and iDrive entry updated"), Restart iDrive;
then, with a later kit, the phone manager update.

## 2026-11-14: the manager updates its own plugin; manager updates from the phone (dev47)

Requested by the user: updating the manager from the phone must be possible and
include the iDrive plugin, and the manager must be able to restart iDrive.

- `b646da6`: once its list shows, the GUI sends `SETUP`; the worker brings the
  stable launcher and the iDrive plugin to the confirmed release
  (`src/launcher/setup_refresh.cpp`, `src/storage/hmi_plugin_swap.cpp`): launcher
  through a checked `.next` renamed over it; plugin written in
  `.NbtManager.setup`, folders swapped through `.NbtManager.old`, `/fs/sda0`
  remounted rw and back ro around it (`mount` found on iDrive's PATH,
  `src/worker/system_tools.cpp`). During a trial it waits. A replaced plugin makes
  the GUI ask **Restart iDrive?** (Later focused); only Restart iDrive sends
  `IDRIVE RESTART` (`OnOffDSICommander appreset`). The manager runs as root (it
  only opens a root-owned 0700 managed root), so the remount is allowed; `pf_aa`
  remounts `/fs/sda0` the same way from an iDrive-started process.
- `912e353`: a manager release (`NBTRLS1`) uploaded by a paired phone is verified
  against `trust/manager.pem`, refused when not newer, while another update is
  pending, or while the stable launcher is older than the active release; reviewed
  as "NBT Manager"; on approval it becomes `state/manager/release.upload` plus
  `state/manager/stage.request`. The launcher stages it at the next opening
  (`src/launcher/stage_request.cpp`, called from `nbt_manager_supervise`) and runs
  it on trial. The worker never stages: the launcher holds those locks while the
  manager runs, and a launcher restarting a crashed child would run a slot staged
  under it without a trial.
- `96a5f14`: the kit has `phone/NBT-Manager-<version>.nbtr`; the procedure's step 2
  uses **Update** for an installed car (no removal), step 8b covers the phone path.
- Verified on the host: 230 tests (7 refresh cases incl. interrupted swaps and a
  refused remount; 4 phone manager-update cases; UI review and restart), pre-commit,
  `qnx-check`. Not verified in a car: the remount from the manager, the plugin
  folder swap while iDrive has the old plugin loaded, `appreset` started by the
  manager.

Kit dev47: `dist/nbt-manager-windows-kit-dev47.zip`, 17646556 bytes, SHA-256
`3ac093170befd26fe27e3d74e52fc3b1f4150911337b3099194625722ceca4f9`, release
`1bb2c7db…83fe4`, APK versionCode 47; on the NAS, verified. Next in the car: Update
to dev47, open App Manager, accept Restart iDrive, check the entry and the door;
the phone path needs a kit newer than the installed one (dev48).

## 2026-11-14: first car session with the app (dev43), kit dev45

In the car with dev43 and a Pixel 6 (logs on the NAS: `installer-1794678227.log`,
`debug-1794678613.log`, `slog-1794678617.log`; details in `docs/install-flow.md`):

- The manager update to dev43 staged and its trial confirmed, but **Install** was
  refused: "existing HMI plugin conflicts with release". The car has the plugin and
  launcher of a kit before dev41; `PluginInstaller::preflight` never overwrites a
  different plugin. The procedure now says to Remove NBT Manager, restart iDrive,
  Connect and Install. Until then the door and opening from the phone cannot work.
- Pair a phone showed the code; the app rejected it. Cause in the app (fixed in
  `246cf5c`): expo-camera's `data` is ML Kit's display value (the SSID for a Wi-Fi
  QR); the `WIFI:` text is in `raw`. The emulator check had used the desk pairing,
  not the camera, so this was never exercised.
- The busy dots flashed every second: background status polls counted as busy.
  `f0d2eca` keeps polls from lighting the indicator (input during a poll is still
  ignored, as before); `test_pairing_timeout` covers it and fails without the fix.

Kit dev45: `dist/nbt-manager-windows-kit-dev45.zip`, 17069095 bytes, SHA-256
`519da04bf251d61670cccf46cb68d1c1c94ed61fc802a805be2039d2ee46c9be`, manager release
`01223f03…8833`, APK versionCode 45 (same certificate); on the NAS, verified. dev44
had the same code with the procedure before the plugin note.

Next: in the car with dev45: Remove, Install, then procedure steps 5 to 8.

## 2026-11-14: kit dev43, procedure for the phone app

`dist/nbt-manager-windows-kit-dev43.zip` (17068996 bytes, SHA-256
`f69824e622945b92967cc6a1c681e46530a69e8ae4eff19cd5a7ecac514489ef`) replaces dev42
for the car; copied with its unzipped folder to `/mnt/nas/public/nbt/` and verified
there. Manager dev43 sequence 43, release SHA-256
`07e30027564d9f5cb519e940c87c10a7a854ee9382b6d903b9bc43bf1d59aa65`; no manager code
changed since dev41. The APK is versionCode 43, same release certificate, with the app's
pairing hints corrected (app `ba130dc`: the car's path is App Manager → Install or update
→ Pair a phone).

`docs/in-car-procedure.md`: section 5 (the browser, install link and token flow removed
in dev41) is now "The car's Wi-Fi code": the Install or update and Pair a phone screens,
the 60 s timeout, the `Wi-Fi Direct:` and `firewall:` log lines. Section 8.1 says the
door's launcher comes from step 2's **Install**, not a phone update. Step 4.4 lists
"Open from phone" and **Paired phones** in the manager's Settings. Car finding: a
pairing that times out says "Timed out; no package received", the install wording
(`src/gui/ui_input.cpp`); harmless, left for a later kit.

Next: milestone 3 in the car with dev43 (plan section 9, steps 1 to 6).

## 2026-11-14: kit dev42 with the phone app

`dist/nbt-manager-windows-kit-dev42.zip` (17069273 bytes, SHA-256
`c9c9203edfc17965a18f0815d02816fdde3785ebc97adca9a38e08d6ab53cc1a`) is the first kit
with the phone app: `phone/NBT-Manager-0.1.0.apk`, versionCode 42, signed with the
app's dedicated release key (certificate SHA-256 `86feff0d…3f7d`, committed in the app
repo as `android-release-cert.sha256`; keystore outside both repos, backed up in
Bitwarden folder `nbt`). Embedded manager dev42 sequence 42, release SHA-256
`13391b5f76c0a93955022839271358cc011abfd66256ba44cd2aad2fd84184f7`; no manager code
changed since dev41, which was never installed. Every `SHA256SUMS.txt` entry verified.

`tools/build_windows_kit.py` now calls the app's `scripts/build-release.sh <sequence>
<dir>` (it took `--output` before), reads the fingerprint from the app's
`android-release-cert.sha256`, finds `apksigner` under `ANDROID_HOME` when it is not on
`PATH`, and accepts apksigner's `V2 Signer:` output. `docs/in-car-procedure.md` lists
the APK and how to install it. Built with the environment of the clean-checkout block
below (`BUILD_DIR=build/phone-qnx65`), without `--without-phone-app`.

Next: milestone 3 in the car with dev42 (plan section 9, steps 1 to 6). Nothing has
run on a phone or in the car yet.

## 2026-11-14: milestone 2, phone app 0.1 (app repository)

The phone app is in `~/src/mobile/nbt-manager` (github.com/cornera-org/nbt-mobile-app),
pinned to this repository at `f9cdfa6`; its `HANDOVER.md` has the state. No manager
code changed. The app was checked against this repository's host worker and door
(`build/managerd-test`) with its contract test, not on a phone or in a car.

Kit build order, once the app's release keystore exists: manager release, then
`scripts/build-release.sh <sequence>` in the app repository (signed APK, certificate
checked against its committed fingerprint), then `tools/build_windows_kit.py` with the
APK in `phone/`. Until then dev41 ships car-only with the notice. Finding for this
repository: a first install through a generic session fails when the managed root
lacks `trust/keys`. Setup creates that directory, so cars are not affected, but the
worker could create it or tolerate its absence.

## 2026-11-14: milestone 1, car-side phone access and dev41

Milestone 1 is complete in the signed implementation checkpoint `4372742`
and the following kit/handover checkpoint. The requested pause is in place;
milestone 2 implementation has not started. GPG signing succeeded after the
user requested a retry. Signing remains enabled. The working tree started
clean at `98195818f17f468896b0bf4c79b19c1f1926cdb4`.

The user first selected the mobile milestone, then explicitly redirected work
to milestone 1 (car side) first. They requested frequent commits, a pause at
each milestone and branch backups. `backup/pre-phone-api-2026-11-14` preserves
the original baseline on both configured remotes. The signed mobile foundation
is `5ddd421`, preserved by `backup/app-foundation-2026-11-14` locally (that repo
has no configured remote). The completed manager checkpoint is preserved as
`backup/milestone-1-2026-11-14`; publication targets are `main` and that backup
on both origin and gitea. Verify signatures and both remote tips before resuming.
Earlier private-permission patch snapshots are also retained at
`/tmp/nbt-manager-milestone1-2026-11-14.patch` and
`/tmp/nbt-mobile-foundation-2026-11-14.patch`.

Built:

- Per-car P-256 phone trust (four phones), nonce pool, canonical signed `/v1/`
  requests, bounded streaming uploads, settings, install/update/remove/status;
  the bearer session, browser page and link QR are removed.
- Wi-Fi-only pairing QR, six digits with car-side No/Yes, immutable local
  action IDs, named uninstall, paired-phone management and Open from phone.
  Package approval remains digest-bound and private to the GUI channel.
- Worker `--door` on wfd0:8081, verified known-good launcher selection and
  readiness, independent managed lifetime lock, separate refreshed `nbt-door`
  packet-filter rule, plugin's 15-second delayed start and three-attempt guard.
- Plugin CDS speed/context subscription plus periodic get, bounded async layer
  probe and wake markers. No plugin heap/STL imports or static constructors.
  Polling retains child ownership until reaping, avoiding recycled PID signals.
- Public shared request/signature/pairing vectors and software-phone host test
  client. `PROTOCOL.md` alone defines the wire contract; mobile links to it.

The file map, decisions and target assumptions are in
[docs/phone-access.md](docs/phone-access.md). Related protocol, README, plugin,
controller, bridge, car procedure and install history documents were updated.

Verified on this workstation:

- `nice make -j $(nproc) pre-commit test`: checks and all 13 C++ executables
  passed; 219 Python tests passed with 3 optional QR dependency skips.
- Full ASan/UBSan suite: the same 219 tests and C++ checks passed (same 3 skips).
  `nice make -j $(nproc) fuzz`: all 19 targets passed with sanitizer coverage,
  including signature, pair and context/wake parsers, one second per target.
- The optional QR libraries were installed in `/tmp/nbt-phone-qr-qa`; all 10
  QR and real-worker UI tests then passed, including independent decoding of
  the rendered Wi-Fi-only pairing screen. ZXing 3 drops empty symbols; empty
  encoding remains covered by the existing byte-level test.
- `cd desktop && cargo test --locked`: 29 passed, one integration test ignored;
  that `native_bootstrap_over_ssh` test was run separately with
  `NBT_TEST_LAUNCHER_HOST=<repo>/build/launcher-host` and passed.
- QNX 6.5 ARMv7 cross-build, `qnx-check stack-report`: passed; no unavailable
  target C++ symbols, no plugin heap imports. Plugin text/data/bss
  20490/672/3396. The largest QNX frames are bounded worker/door storage around
  87 KiB; this report does not establish runtime stack margins on the car.
- Static shipped-framework inspection confirmed the CDS method signatures,
  UTF-16 delegate and shared get/subscription callback map. Exact firmware
  runtime behavior still needs the car.
- Car-only Windows dev41 kit built with external existing RSA keys. Every ZIP
  checksum was verified, the signed manager release is embedded in the Windows
  executable, test packages verify, and an explicit APP-STATUS notice replaces
  the not-yet-built App 0.1 APK. Default kit builds refuse a missing app script.

Artifacts (generated locally; ignored by git):

- `dist/nbt-manager-windows-kit-dev41.zip` (4247151 bytes).
- Kit SHA-256: `fabf2d61b0dec87a4bc689aaa34fbd0c2e092868455c914da7ddd9f35904fac0`.
- Embedded manager dev41 sequence 41 SHA-256: `fa6b197b5d4cb23dc67945ee88f25a2175aea550d96b2e1c929b498c3b52c949`.
- Build tree: `build/phone-qnx65`, separated from older QNX 6.6 objects.
  External manager sequence is now 41. The unshipped kit was refreshed at 41
  after final plugin cleanup; no sequence counter was lowered.

Unverified and deliberate limits:

- No Android handset, APK, USB phone contract test or in-car result is claimed.
  P-256 in the actual car's libcrypto, CDS physical freshness, QNX shmem/pf,
  boot behavior, display ownership, power loss and installation require target
  checks. Dev41 has not been installed by this work.
- Open from phone ships off. Automatic opening initially permits only the
  observed ID6 home context 3 / `LT_Table_ID6L_Pad_Title`, fresh received zero
  speed and iDrive alone. Navigation/unknown/camera contexts require manual
  opening. This conservative restriction is documented pending car evidence.
- The stable launcher/plugin must be installed through setup before boot tests;
  an ordinary GUI/worker slot update cannot upgrade an old launcher to --door.
- Application autostart remains a separate paused feature; the phone door's
  plugin/launcher integration is implemented.

Clean checkout:

```sh
git submodule update --init --recursive
nice make -j $(nproc) pre-commit test
ASAN_OPTIONS=detect_leaks=0 nice make -j $(nproc) BUILD_DIR=build/asan \
  CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
  CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
  LDFLAGS='-fsanitize=address,undefined -no-pie' test
nice make -j $(nproc) fuzz
source /home/mobyfab/src/tools/qnx-6.5-tools/env.sh
export QNX_SCREEN_LIBDIR=/home/mobyfab/src/reverse/Supra-iDrive6-Android-Auto/milestone-7/lib
export RHMI_LIBDIR=/home/mobyfab/src/reverse/opt/hmi/ID5/lib
nice make -j $(nproc) BUILD_DIR=build/phone-qnx65 qnx-check stack-report
python3 tools/build_windows_kit.py --without-phone-app
```

The kit command builds the next sequence and compiles in the external apps
public key. Use `BUILD_DIR=build/phone-qnx65` in its environment to reuse the
matching SDK tree. Never reuse objects across SDKs. The QNX checks alone use
the configured/default apps key, so rebuilding them can change intermediate
worker bytes; the kit always checks its embedded key and signs its own release.
The complete sanitizer, desktop and optional QR instructions also appear in
README/test sources. Private keys never enter either repository.

Next after the milestone pause: App 0.1 in `~/src/mobile/nbt-manager`.
Its Expo foundation is signed at `5ddd421`; its handover/submodule checkpoint
pins this completed manager milestone. Implement native device-key/car-link modules and shared
protocol/library/screens, generate the dedicated external Android release key,
then build and verify the signed APK and include it in the next kit. The user
approved Nearby Wi-Fi on Android 13+ and Fine Location limited to Android
10-12, without scanning or location use. ENET installer work remains milestone
4; do not start it during App 0.1.

## Archived snapshot: 2026-11-10

Snapshot: 2026-11-10. Initial bridge 1.0 includes show, status and live
settings (`ffe5103`); guarded autostart worker core is checkpointed at `d248409`.
Autostart GUI/launcher/plugin work is paused at the user's request. This
handover also records the separate third-party installer investigation below.
The manager has **never been installed on the car**. The current code defines
its very first installable release; no deployed-launcher migration is needed.

Earlier GUI baseline: `ae369cc` (hold for settings). Steps since the previous
handover, oldest first: `c44426a` phone
status, `1d4a1f9` controller release, `4203673` launcher `--provision-app`,
`138fa82` installer test tools, `9d2efba` DSA host key/remount/iDrive restart,
`dced19e` Windows in-car kit, `963ccfc` generic install session, `2b9799c`
detached application hosts, `708abc3` menu rework, `b0c5a2c` scrolling lists,
`176c6ff` stop dialog (replaced by `ae369cc`), `0a8cbfe` action colours, `cf610a1` application
settings and hello-world test app, `1c23bbf` manager settings, `ae369cc` hold
for settings.
This file records continuation context; read `AGENTS.md` first and verify the
current working tree and remote tips before changing or publishing anything.

## Start here

- Installer and launcher, end to end (steps, checks, car facts, test
  history): [docs/install-flow.md](docs/install-flow.md).
- Actual repository: `/home/mobyfab/src/reverse/nbt-apps-manager`.
  The former `/home/mobyfab/src/reverse/nbt-updater` path may still appear in the
  session environment or sandbox configuration. It is not the current checkout.
- Both remotes use SHA-1 history after migration from the original SHA-256 repo:
  `origin` is `git@github.com:fpoussin/nbt-apps-manager.git`; `gitea` is
  `git@gitea.netyxia.net:fpoussin/nbt-apps-manager.git`.
- The implementation baseline was pushed and verified on both `main` branches;
  the working tree was clean before adding this handover. The handover commit
  itself follows that baseline.
- Use the real checkout. `/tmp/nbt-desktop-work` was an editing scratch copy and
  is not authoritative. Prior builds needed sandbox escalation because the
  writable root still referenced the old repository name.
- Shell commands go through `rtk` (a Claude Code hook rewrites them; use
  `rtk proxy` for raw output, e.g. a diff to apply as a patch). `rtk grep`
  mishandles regex parentheses; read the file instead.
- Commits are GPG-signed. Never bypass signing; if pinentry times out, ask the
  user to unlock the key and retry.
- Secrets: never print key material or `BW_SESSION`. The Bitwarden session is in
  `~/.secrets` (source it in a subshell only); do not run `bw lock`.

## User decisions and working agreement

Build an original application manager for BMW NBT-EVO, targeting QNX 6.5 ARMv7.
The name is **NBT Manager**, not updater. Keep C/C++ interfaces reusable,
ownership explicit and untrusted input bounded; memory safety is a priority.

Keep the iDrive `.so` minimal: independent menu registration and launch bridge.
The standalone manager owns its GUI, pairing/QR display and eventual local
approval screens. The worker owns application verification and transactions.
A dynamically generated QR image does not need to be displayed by the plugin.

Manager releases use a manager-specific signing key, separate from application
release keys. Private keys stay on the signing workstation. Manager GUI/worker
updates share the launcher/slot recovery boundary; the stable launcher and plugin
are installed during setup and are not replaced by ordinary updates.

The desktop tool is Rust/egui, with simple buttons, progress and selectable,
copyable logs. SSH activation was explicitly requested from `../nbt_ssh/`, using
only the required activation protocol. It runs only when needed and after a
confirmation dialog. Traffic capture/dump functionality is outside scope.

GUI and input decisions made in this session:

- Render with **LVGL 9 (C99)**, a git submodule with the SSH remote, rather than
  the target's Qt. A desktop X11 simulator (`make run-simulator`) checks look and
  navigation; it must stay quick to rebuild and quit on Ctrl+C.
- Read the iDrive controller with the manager's **own MOST client**, not through
  the plugin.
- Pairing is **by QR code only**: nothing is typed, no address or code is shown.
  The QR holds the session's own URL `http://<address>/<token>` (32 random bytes
  from `/dev/urandom`); it opens the upload page directly and expires with the
  session. The pairing code and `/pair` were removed.
- The QR screen counts down **60 seconds** and cancels the session unless the
  phone opens the page; opening it stops the countdown (worker STATUS state 6).
- While the manager is shown, iDrive's focus is held on a blank plugin state,
  following NBT-AAD's car-validated approach (facts only; NBT-AAD is PolyForm
  Noncommercial, no code copied).
- Configuration files use the simplest format with the code we already have:
  strict `key=value` text with a format tag (as the app profiles), not YAML; no
  YAML library is available for QNX 6.5 here.

Menu and application decisions (2026-11-10):

- Main menu: **one entry per application** (a push starts it), then a generic
  **Install or update** (the uploaded package names the application) and
  **Settings**. The manager's own actions use a different colour (light blue
  text, teal focus) from applications.
- Starting an application **closes the manager; the application keeps running**
  in its own host. **Holding** the push on an application opens **its
  settings**: after 0.5 s a dialog over the dimmed menu shows a filling bar; at
  1.5 s the settings open (duration chosen by the agent, `NBT_UI_HOLD_MS`).
  **Stopping** is an ordinary last entry there, "Stop <app>", while it runs (no
  confirmation). Banner hint: "Push to start; hold for settings".
- A **banner** (the view title) names the current menu; on the main menu it is
  "NBT App Manager <version>".
- Lists that do not fit scroll with the focus and show a scroll bar (LVGL's own
  scrollable container; the offset is kept between renders).
- **App settings are declared in the signed package** (choices only for now);
  hold on the application → setting → choice.
- **Applications ship no iDrive plugin of their own**: the manager's menu
  starts them, and their settings and stop entry live in the manager (the
  user's intent behind "move the app's plugin menu entries there").
- **The bridge** is to be the documented interface between managed
  applications and the car, with the manager as its only implementation
  (user's framing), **versioned against applications** (major/minor,
  declared dependencies checked at install and manager update). Design:
  `docs/app-bridge.md`. Versioning, `show`, `status` and `live-settings` are implemented (below); other capabilities
  remain planned. NBT-AAD
  cannot drop its own plugin before components 4-6 (car data, focus hold)
  exist: its plugin feeds the reverse gear.
- The main menu's **Settings** opens the manager's own settings directly (no
  list of applications): Debug log, Invert knob, Hand back controller, on/off.
  Instances and key slots stay in the desktop installer (wrong ones would make
  the menu unreachable anyway).
- The test application is a "Hello world" on a background colour chosen in its
  settings; in the car **BACK exits it** (MENU too). The simulator runs the same
  drawing for `test` (2 simulated applications: `media-player`, `test`).
- No spare head unit, only the car: validation happens through the Windows
  in-car kit and `docs/in-car-procedure.md`, parked and carefully.

Commit and push each completed step directly to **both main remotes**; no PRs.
Give concise progress updates. If stopping at a milestone, explain why and list
what remains. No head unit is available now: continue useful host-side work and
keep hardware-dependent claims explicitly unverified.

## MOST bridge (2026-11-11)

User request: a documented bridge to MOST for managed apps, covering all MOST
functions, with a per-app function whitelist; first goal NBT-AAD on it (via a
fork). Sandboxing and one shared connection are a later iteration. Contract:
`docs/most-bridge.md`. Host-tested and QNX-built; **not run in a car**.

- Bridge **1.1** adds capability `most` (`nbt_bridge_current`, `tools/bridge.py`
  `CURRENT`; release packaging follows). Packages declaring `most` carry
  `bridge-most=<policy>` (`rx:FB.II.FFF`, `tx:FB.II.FFF`, `ctl:FB.II`; parser
  `src/model/most_policy.cpp`, Python twin in `tools/bridge.py`). A policy needs
  a manager serving `most` even when the capability is optional; staging then
  refuses releases without it. Records/journals: `NBTSTATE3`/`NBTJ3`, written
  exactly when a policy is present.
- `src/most/dsi_client.cpp`: the one-per-process DSI client, split out of the
  input loop. `src/most/service.cpp`: the host's MOST client (`NBTAPP_<app>`),
  link state, registry, rx filter, segment joining (`nbt_most_join` in
  `src/most/codec.cpp`), SEND checks (op 0-8, `tx` rule, link up, registry ring
  node 0x0100-0x0FFF, 100/s), request 0x2 builder (`nbt_most_process_message`).
  `src/sdk/most_wire.cpp`: app<->host datagram records.
- `src/process/app_host.cpp` starts the service before computing grants (`most`
  is available only if it started), passes fd 6 (`nbt_process_start_host`), polls
  the socket instead of sleeping. `NBT_TEST_DSI_LIBRARY` (testing builds only)
  points it at `build/libfake_dsi.so`.
- Facts from NBT-AAD `c34cdb0` (no code): its routes, controls and HUD sends
  are the example policy in `docs/most-bridge.md`; comm state 1 while messages
  flow, so comm-lost only after OK was seen.
- Tests: `build/most-test` (policy, wire, joiner, request 0x2 bytes),
  `test_app_host.py` `test_most_*` (end-to-end through worker, host and fake
  DSI), `test_transaction.py` policy records, `test_bridge.cpp` declarations.
- Next: the NBT-AAD fork using `NBT_MOST_FD` (its MOST client replaced by a
  socket client), then car checks listed at the end of `docs/most-bridge.md`.

## Bridge continuation (2026-11-10)

New AAD evidence from local `0a314d4`: button-opened camera views may change
neither gear nor PDC; contexts 1023 (F-series NBTevo_Y24073J) and 692 (Supra)
need camera-specific step-aside and suppression of focus re-holds. See
`docs/app-bridge.md` for provenance and future validation. `/dev/shmem` has
reported neither rename nor fsync support; bridge status is ephemeral.


Completed order-of-work steps 1-4 from `docs/app-bridge.md`:

- `include/nbt/model/bridge.hpp`, `src/model/bridge.cpp` (C ABI in
  `src/model/bridge_api.cpp`): bounded, allocation-free version,
  capability-list and grant logic. Current worker serves **1.1 with `show,status,live-settings,most`** (1.0 before the MOST bridge).
  Optional unknown capabilities are accepted but not granted. Required unknown
  capabilities, missing majors and newer minimum minors fail with a reason.
- Application packages: three optional ordered signed fields after `sha256`,
  before settings: `bridge`, `bridge-uses`, `bridge-optional`. Added
  `tools/package.py --bridge/--bridge-uses/--bridge-optional`; all fields present
  together or all absent. Existing no-bridge packages stay valid.
- The worker verifies dependencies before review and again at installation;
  failure reasons flow through GUI/phone STATUS. Hosts refuse incompatible starts.
  User launches and candidate health checks get only their declared grants as
  `NBT_BRIDGE`/`NBT_BRIDGE_CAPS`; no-bridge apps get neither variable.
- Requirements live inside the installed record (`NBTSTATE2`) and old/new
  transaction records (`NBTJ2`), paired with their executable across interrupted
  installs, confirmation and rollback. The planned separate `state/<app>/bridge`
  file was replaced by this journaled representation to avoid a crash window.
  Legacy no-bridge records/journals remain supported. Offline bundles retain
  bridge metadata too (`tools/prepare_bundle.py`).
- Manager releases include up to two signed `bridge=major.minor|capabilities`
  lines after `compat`, increasing by major. Default packaging is `1.0|show,status,live-settings`;
  `--bridge` overrides and `--no-bridge` are publisher/test controls. C++/Rust
  verifiers validate the same grammar; signed metadata must describe the worker.
- Manager staging checks every installed app before clearing a slot, holding
  app transaction locks as well as the manager operation lock. Normal dependency
  breaks name the affected app and refuse; explicit recovery warns on stderr.
  New app provisioning/updates wait during pending/trial manager activation.
  The stable launcher provisions against the active **signed release's** bridge
  metadata, rather than its own compiled worker capabilities.
- Bring-to-front: `nbt_app_host_show` checks the kernel's host identity and a
  separate ready/granted capability lock on `host.lock`, then signals the host.
  The host forwards `SIGUSR1` from its main loop only to its own unreaped child,
  after readiness and only with the `show` grant. Signal handlers only set flags.
  `START` on an app without show (or not yet ready) returns `ERR running`; the
  GUI displays "<app> is already running" and stays open. Normal start and an
  accepted show return `OK`; display success is asynchronous. See `PROTOCOL.md`
  for the byte-range lock contract and remaining target/PID lookup limits.
- `apps/test/main.cpp` restores its existing Screen buffer/window on show and
  resets the no-input hide timer. Health-check launches ignore show. The Windows
  kit's test packages require `bridge=1.0` and `show,status,live-settings`; the simulator covers a
  running media app without show and a test app with it.

- Status: `NBT_STATUS_FD=5` only for user hosts granted `status`; the host creates
  a fresh private flat file before spawning, through `src/storage/paths.cpp`. Bridge parse
  and format helpers bound text to 24 printable ASCII bytes and a canonical
  uint64 sequence. Candidate health checks receive no status descriptor.
  A ready/granted byte-3 marker (byte 2 stays free) gates worker reads, checked
  against the same lifetime owner before and after. `RUNNING status` attaches
  `|text` to names with valid labels, while plain `RUNNING` stays names-only.
  Missing/unsafe/malformed/stopped/unready/ungranted status falls back.
  The GUI polls once a second, keeps rendering I/O-free and accepts a push
  during refresh. Reply buffers now fit all 16 maximum-length entries and
  item labels retain the full app name plus status. The test app publishes
  Hello/Hidden plus its build version; the simulator includes a status label.
- AAD's inherited ignored `SIGCHLD` finding applies here: launcher, GUI and
  worker call reusable `nbt_child_reaping_init` before spawning to retain
  explicit child ownership. Tests launch GUI/worker with SIGCHLD ignored.
- AAD evidence is recorded in `docs/app-bridge.md`: camera contexts and
  missing gear/PDC triggers, proposed MOST speed/gear/parking-brake formats,
  media/wheel leads and validation limits. No AAD code was copied. Ephemeral
  status uses neither rename nor fsync on shared memory. Target behavior
  remains untested; rebuild the kit before car use.

**First release:** the user clarified that the manager has never been installed.
Keep this as the initial bridge 1.0 contract; the foundation-only commit was
unreleased development. Initial setup installs the current launcher/plugin,
and later ordinary OTA changes GUI/worker only. Kit dev3 (below) is the first
built from this contract.

Completed settings step (component 3, order-of-work step 4):

- `NBT_SETTINGS_MAX=12`; bounded two-digit GUI parsing and 2048-byte worker
  replies. Signed manifests retain their 2048-byte total bound; package tooling
  rejects count/size overflow. The settings page scrolls through twelve plus Stop.
- Provisioning and offline bundles create `data/<app>`; every launch gets
  `NBT_DATA_DIR`. App-owned preferences survive executable update/recovery.
  Existing data prevents provisioning; cleanup only removes newly owned entries.
- `SET` serializes on the app transaction lock, saves authoritative choices
  through `.next`, then publishes `data/<app>/settings` in place. Schemas and
  default snapshots are now included in offline bundles too. Confirmation keeps
  the update lock until schema/snapshot publication. The worker refuses SET on
  its own active update before touching a POSIX process-owned lock.
- Granted `live-settings` passes `NBT_SETTINGS_FILE` to user/candidate launches.
  Launch repairs the snapshot from authoritative values. `src/sdk/settings_file.cpp`
  is an optional reusable bounded reader, separate from the portable parser;
  failures preserve caller values. Snapshot delivery is eventual: partial or
  mixed concurrent reads are possible. A late save/sync failure may already have
  saved the durable choice; retry or next live launch repairs it.
- GUI uses `SETTINGS <app> live` (adds live/restart after count); plain SETTINGS
  stays compatible. The page says "Changes apply at once" with the grant.
  Native test refreshes its background each heartbeat; simulator labels it live.
- New tests cover full declarations, native colour changes without restart,
  malformed snapshots, protected paths, stable inode/shorter writes, startup
  repair, conflicting locks, partial write/truncate/sync faults and retry,
  provisioning conflicts, signed offline schemas and data preservation.
- NBT-AAD remains at local `0a314d4`; no additional findings since the status
  step. Settings durability belongs on managed Power-Safe storage; the separate
  `/dev/shmem` status channel still uses neither fsync nor rename.

Autostart core validation: C checks and **157 Python tests passed** (3 optional
QR checks skipped), full ASan/UBSan suite passed, QNX 6.5 build passed. No new
Windows kit or signed release artifact; no head-unit validation.

**Paused at the user's request (2026-11-10): autostart worker core checkpoint.**

- `src/lifecycle/autostart.cpp`, `include/nbt/lifecycle/autostart.hpp`: independent manager-owned
  `AUTOSTART <app> [off|on]` preference/guard; fixed `state/<app>/autostart`
  and `autostart-guard` records plus safe `.next` files. App setting keys and
  the twelve signed settings are unchanged. Shared `nbt_fs_replace_text`
  now handles bounded settings and autostart record replacement.
- Worker `--autostart` dispatches detached `--host-auto` hosts. Under lifetime
  and transaction locks the host durably increments before recovery/launch;
  real readiness/lease/heartbeats for 10 seconds clear it. Three unconfirmed
  attempts block. Duplicate dispatch skips existing hosts without show.
- Manual IPC START re-arms before start/show (including a failed manual
  attempt). Toggling preference does not re-arm. Malformed regular guards can
  be repaired by manual START; unsafe aliases are never overwritten.
- **Unfinished:** GUI last choice "Start with the car" and blocked reason;
  stable launcher `--autostart` selecting/re-verifying a known-good signed
  worker under manager operation lock (handle pending trials without GUI);
  plugin one-shot 15-second boot trigger. Current launcher/plugin unchanged.
- An incomplete UI experiment is backed up at
  `/tmp/nbt-autostart-ui-wip.patch`, outside Git. It is **not ready to apply**:
  its R_SET request branch is incomplete; use only as a reference or rebuild
  the small UI integration cleanly. The committed GUI remains unchanged.
- Resume component 6 / order step 5 only when asked. No hardware validation.

**Installer investigation (requested while autostart is paused):**
[docs/installer-transfer-analysis.md](docs/installer-transfer-analysis.md) records
extraction and transfer findings for NBTEvo Android Auto Installer 1.0.3.
The payload ships Windows `dependencies/plink.exe`; `upload_file` feeds an `rb`
file to Plink stdin and remote `cat > '<safe temp path>' && echo __UP_OK__`,
checking exit status and the marker. The identified upload path needs neither
SCP nor SFTP; no SCP/PSCP executable exists in its 66-file payload inventory.
Our Rust installer already uses SSH stdin streams, so no product change is
needed for that transport. The Wine-installed app and Plink match the extracted
hashes; native source anchors, REA function Evidence IDs and analysis limits are
in the report. All three requested native dossiers completed; the owned Hopper
session closed with verified cleanup and `native-transfer.snapshot.json` saved.

REA was updated to 6.8.0 during analysis and the user reinstalled Hopper.
No REA MCP tools appeared in this chat; the supported CLI and stdio MCP
interface were used with the existing installation. Scratch files live under
ignored `build/installer-analysis/`, never commit them: they include private
third-party binary/decoded material. Native startup needs a short socket path
(`TMPDIR=/tmp/nbt-rea`, a symlink into that scratch directory) and REA's returned
large-target allowance `REA_HOPPER_STARTUP_TIMEOUT_MS=600000`. The original
installer and Wine installation were unchanged; no car connection or target
execution was performed by the agent. Current app work stays paused.

**Firmware interface research (2026-11-10, after `14a9344`):** an `/opt` dump of
NBTevo_Y21432J is at `/home/mobyfab/src/reverse/opt` (outside the repo, never
commit it). Findings are in `docs/head-unit-interfaces.md`: DSYSMOSTServer 4.5
ids and payloads confirmed, the plugin ABI confirmed against
`libAppFramework.A.so`, the car-data subscription path (its `unsigned int` is a
debounce interval), the CDS catalogue, and the MOST function catalogue
`docs/most-functions.tsv` (2148 functions, FBlock and function ids), generated
by `tools/most_catalogue.py` (capstone; tests skip without it). The
`CDSIClient` table copy in `src/most/dsi_client.cpp` was bounded to its 8 slots; the
fake DSI table now has the firmware's size, so ASan catches a regression
(verified: the old 32-slot copy is a global-buffer-overflow). Hopper through
REA timed out on HmiMain3D (58 MB) at its 2-minute startup limit; the ids came
from the static analysis in the tool. Nothing here ran on the car.

**First car inventory (2026-11-10):** `/home/mobyfab/src/reverse/nbt-inventory-5`
(outside the repo; `pidin-arg.txt` holds the head unit's MAC). The `/opt` dump
is this car's (28735 of 28737 HMI files the same size, copied libraries
byte-identical), including `HmiMain3D` and `NavNBTEvoController`, so the
catalogue applies. NbtAad
(same manifest shape as ours) loaded from `Plugins/` and received
`hmi.graphicalContext`; Screen refused its z-order change on another context's
window. Details in `docs/head-unit-interfaces.md`, "Seen on the car".
`tools/car_inventory.sh` missed `HmiMain3D` (a `ziploader` path) and
`./NavNBTEvoController`; it now looks them up by name, and also copies the
plugin framework, `libhmiremoting.so` and the coded plugins' manifests and
libraries, never their certificates. Script tested on the host against a fake
root in sh, dash and bash; not yet rerun on the car.

## What exists

| Area | Implementation and reference |
| --- | --- |
| Minimal plugin | `plugin/plugin.cpp`, original manifest/XML/resources; menu launches `/var/opt/nbt-manager/manager/nbt-manager-launcher --open` and holds iDrive focus while the manager is shown. The only iDrive plugin: managed applications ship none. See `plugin/README.md`. |
| Manager GUI | `apps/manager/main.cpp`: QNX Screen window, worker startup/restart and supervised readiness/heartbeats; shows the controller's views; reads iDrive input over MOST (`src/most/input.cpp`, `src/most/ccs.cpp`, `src/most/codec.cpp`; see `docs/controller-input.md`). Closes after 2 min idle, or by itself without input. |
| Controller config | Optional `/var/opt/nbt-manager/manager/controller.conf` (`NBTCTL1`, `src/model/controller_config.cpp`, example `config/controller.conf.example`): controller instances, registered instances, rotation direction, MENU/BACK key slots per protocol, `release` (unregister on exit, request `0x5`, off by default: payload assumed). Invalid file: defaults plus a notice on the application list. Written by the desktop installer, or rewritten whole by the manager's Settings for `invert`/`release`. |
| iDrive focus hold | `src/gui/hmi_hold.cpp` (I/O-free decisions, `tests/test_hmi_hold.cpp`), plugin states 14/15 and focus events 50/51 in `plugin/NbtManager.xml`. BACK is re-held; other exits make the GUI step aside via `/dev/shmem/nbt-manager-hmi` (exit, or hide during an installation); the entry brings it back. |
| Pairing and upload page | `src/worker/session.cpp` (token), `src/worker/http.cpp`, `src/worker/web.cpp`: `GET /<token>` serves the page while the session lives (403 expired page otherwise) and marks it opened; upload uses the token as bearer. The page polls `GET /status` (waiting, verified, installing, installed, failed, cancelled, expired); after the session ends the opened page's token keeps read-only status access for 5 minutes, extended while installing (`nbt_manager_status`, watch in `src/worker/worker.cpp`). Script tested with `tests/page_harness.js` when Node is present. See `PROTOCOL.md`. |
| Debug log and bench checklist | `src/log/log.cpp` (format, size limit, plugin path), `src/log/log_rotation.cpp` (path policy, rotation); `manager/debug.log`, on only while the file exists (created by the installer or Settings → Debug log; off moves it to `debug.log.1`), 128 KiB plus one rotated copy, shared by GUI, worker and plugin, no secrets. `docs/bench-checklist.md` orders the hardware validation with the log lines each step should produce. Tests: `tests/test_log.cpp`, log cases in `tests/test_manager_gui.py` and `tests/test_manager_ui.py`. |
| In-car test kit | `make windows-kit` (`tools/build_windows_kit.py`): Windows installer (MinGW cross-build) with the manager release and test app v1 embedded, phone packages v2 and failing v3, `docs/in-car-procedure.md`. Personal test keys (manager, test app) in `~/.local/share/nbt-manager/keys`, outside the repo; private keys also stored in the user's Bitwarden vault (folder `nbt`). Manager compatibility `nbt-evo-dev`, test app `nbt-test-1`. Installer test tools: debug log on/save/off, provision test app (launcher `--provision-app`), controller.conf, restart iDrive (`OnOffDSICommander appreset`), remove everything. The installer has the head unit's address (`169.254.199.119`; `.99` is another module), user and published root password built in, with a plain-text password field; it accepts the old sshd's algorithms (group exchange from 2048 bits as OpenSSH, HMAC-SHA1, SHA-1 key exchanges, AES-CBC; `ssh-rsa` and `ssh-dss` host keys). |
| GUI rendering | LVGL 9.6.0 submodule (`third_party/lvgl`, SSH remote; `git submodule update --init`), config `config/lvgl/lv_conf.h`. `src/gui/ui_renderer.cpp` renders the view model with LVGL into an `nbt_frame` (partial buffer, stride-aware flush; QR painted by `src/gui/qr_code.cpp`). LVGL builds warning-free with QNX 6.5 GCC 4.8. `make simulator` gives an X11 preview with a simulated worker. User chose LVGL over target Qt (Qt presence/version unverified). |
| GUI controller | `src/gui/ui_controller.cpp` (I/O-free state machine: main menu with push-to-start and hold-for-settings (`NBT_UI_HOLD_SHOW_MS`/`NBT_UI_HOLD_MS`, release events from the controller decoder), generic install session, one settings screen for the manager or an application (with "Stop <app>") plus choices, QR-only pairing, review, two-step digest-bound approval, recovery, progress/result, strict reply parsing). The view model carries the banner title, per-item style (entry/action) and an optional modal dialog (`nbt_ui_modal`). `src/gui/ui_renderer.cpp` (LVGL): scrolling list with scroll bar, dimmed-menu dialog. Tests: `tests/test_ui.cpp`, `tests/test_manager_ui.py` via `tests/ui_probe.cpp` (prints the dialog fields) against the real worker. See `PROTOCOL.md`. |
| Application bridge | Version/capability declarations, signed manager providers, dependency checks, journaled app requirements and launch grants; `src/model/bridge.cpp`, `docs/app-bridge.md`. Initial provider 1.0 includes `show,status,live-settings`; repeat START requests the existing app to show, otherwise `ERR running` keeps the GUI open. |
| Application hosts | `src/process/app_host.cpp`: `START` runs the worker binary as `--host <app>` (double fork, own session, `state/<app>/host.lock`), so applications outlive the GUI and worker. `STOP [<app>]`, `RUNNING`; installing over a running application stops its host, health-checks the candidate as the worker's child, then restarts it in a host. Applications get `NBT_LAUNCH=user` from a host and `NBT_LAUNCH=check` during a health check. Tests: `tests/test_app_host.py`, `tests/manager_probe.cpp`. |
| Generic install session | `SESSION <interface> <port>`: upload staged in `state/manager/session.upload`; `nbt_package_peek_app` reads the application id unverified, which must be provisioned, then the package is verified with that application's keys. Test: `test_transaction.py`. |
| Application provisioning | Launcher `--provision-app <app> <compat> <key-bytes> <pkg-bytes> <sha256>` (`src/lifecycle/app_provisioner.cpp`): verifies the package, refuses existing paths, writes executable, record, key, settings declaration and finally the profile; discards only what it created on failure. Test: `tests/test_app_provision.py`. |
| Worker/application OTA | C++11 core in `src/`: signed executable packages, local IPC, temporary browser upload sessions, digest-bound approval, lifecycle, install/rollback and recovery. See `README.md` and `PROTOCOL.md`. |
| Filesystem policy | `src/storage/path_policy.cpp`: central application and manager setup/update path protection, fixed roles/paths, private directories, type/link/device checks and serialized changes. |
| Manager releases | `src/package/release.cpp`, `tools/package_release.py`, `tools/verify_release.cpp`, `desktop/src/release.rs`; RSA-3072/SHA-256 signed fixed-role container with strict bounds and payload digests. See `docs/manager-release.md`. |
| Manager installation/recovery | `apps/manager/launcher.cpp`, `src/launcher/`: signed initial enrollment, resumable setup, HMI preflight/registration, versioned slots, activation journal, health confirmation and rollback. |
| Desktop installer | `desktop/src/main.rs`, `ssh.rs`: background SSH, password authentication, every host key accepted (fingerprint logged; owner's decision), Install/Update, progress and reboot request. Uses SSH stdin streams, without requiring SCP/SFTP. Connection fields collapsed under **Connection settings**. Two bars over the log: operation steps (`Event::Phase`) and the current step's bytes, counted on the SSH socket. A **Debug log** checkbox (off by default) shows each remote step's command and, on failure, exit status, signal, stdout and stderr (`Event::Detail`, always recorded) and writes the log to `Documents\NBT Manager logs\installer-<time>.log`; output is read while input is sent, so an early exit is reported. eframe's `clipboard` feature backs **Copy log** and Ctrl+C. |
| SSH activation | `desktop/src/activation.rs`, `discovery.rs`, `relay_codec.rs`: when Connect cannot open the SSH connection (refused, unreachable or timed out), a confirmation offers to enable SSH: it finds the vehicle gateway by HSFZ discovery and relays it (port 6801) to the external service, as `../nbt_ssh` does on the car, logs the service's status texts and reconnects. The gateway is not the head unit's SSH address. |
| Test application | `apps/test/main.cpp`: "Hello world" on the background colour from its declared setting (`NBT_SETTING_BACKGROUND`, drawn by `nbt_frame_hello` in `src/gui/frame.cpp`, shared with the simulator). Started from the menu (`NBT_LAUNCH=user`) it reads the controller and exits on BACK or MENU; during a health check (`NBT_LAUNCH=check`) it shows itself for five seconds and leaves the controller alone. Readiness and heartbeat lifecycle; `NBT_TEST_FAIL_AFTER` builds the broken v3. |
| Manager settings | `src/lifecycle/manager_settings.cpp`: worker `SETTINGS manager`/`SET manager <key> <value>`. `debug_log` creates `manager/debug.log` or moves it to `debug.log.1` (worker re-opens its log at once); `invert`/`release` rewrite `controller.conf` through `controller.conf.next` with every key in effect (`nbt_controller_config_format`). Tests: `tests/test_app_settings.py` (`test_manager_settings`), `tests/test_controller_config.cpp`, `tests/test_ui.cpp`. See PROTOCOL.md "Manager settings". |
| Application settings | `src/sdk/settings.cpp` (declaration and values format), `src/lifecycle/app_settings.cpp` (`state/<app>/settings.schema` and `settings`, environment), manifest `setting=` lines in `src/package/package.cpp` and `tools/package.py --setting`, worker `SETTINGS`/`SET`, GUI: hold on the application → setting → choice (the page ends with "Stop <app>" while it runs). Tests: `tests/test_package.py`, `tests/test_app_settings.py`, `tests/test_ui.cpp`. See PROTOCOL.md. |
| Full release build | Root `Makefile` and `tools/build_release.py`: native tools, optimized QNX components, plugin/resources, verified signed manager bundle and embedded Rust release installer. |

Initial installation verifies on desktop and target, checks platform/storage and
existing files before provisioning, and registers only its own plugin directory.
Conflicting existing files fail closed. Plugin registration binds to the exact
release digest and can retry interrupted staging. Existing keys, launcher and
activation records are checked before changing layout.

An update transfers a verified release to the installed launcher. Normal updates
require an increasing release sequence. Staging success means **pending**, not
healthy/confirmed: the subsequent supervised GUI/worker launch must be ready and
healthy for 10 seconds. Failure or an interrupted trial restores the known-good
slot on recovery. Explicit older-release recovery retains the high-water mark.

## Important unfinished work and limitations

- **No head-unit runtime validation.** Screen handoff/cleanup, iDrive ABI/input,
  platform probing, SSH negotiation, Wi-Fi routing, filesystem persistence and
  executable storage still require the car (in-car kit) or a bench unit. The
  Windows installer `.exe` has not been launched on Windows yet. The session
  port is let through the packet filter by `src/worker/firewall.cpp` (DIOCCHANGERULE,
  label `nbt-manager`, only while the session listens); unvalidated on the car.
- The Windows kit last built is **dev34** (`dist/nbt-manager-windows-kit-dev34.zip`,
  release sequence 34, release SHA-256 `d6f05bea…e3cb591e0e`),
  from the C++ `main`, with every shipped QNX binary stripped (unstripped
  links in `build/qnx-unstripped/`). dev20 ran in the car: it installed, but
  the entry opened nothing and Android Auto stopped working until the manager
  was removed; dev22 did the same. HmiMain3D's app numbering ignores the
  manifest's connector `<id>` (one counter from 21), but sharing AAD's 210 is
  the likely cause all the same: BMW's plugins each have their own (17, 200),
  and with ours our focus events did nothing. dev23 uses 220 (plugin/README.md).
  Installer commands run in a login shell on the unit SSH lands on, as typed
  in a session (dev24); the shell sshd starts for a command reads no profile.
  dev25: Android Auto works beside the manager; the GUI failed to spawn with
  EBADF (close-on-exec descriptors in spawn file actions), fixed in dev26.
  dev26 still did not start. dev27 starts every hop as NBT-AAD starts its daemon
  (`src/process/child_spawn.cpp`, HMI environment) and logs each step.
  dev28: the launcher starts the worker, then the GUI (src/launcher/manager_worker.cpp);
  the GUI is only the worker's client and a failure of either restarts both.
  In the car dev28 started all three, and the GUI drew and took controller
  input, but nothing appeared: LayerManagerService composes the panel and
  draws only windows bound to a numbered surface. dev29 makes the GUI's window
  surface 1290 and shows it in place of iDrive's surface 256 as NBT-AAD shows
  Android Auto (docs/head-unit-interfaces.md, "LayerManagerService composes
  the panel"). Provisioning an app while the manager is open fails with
  `manager operation is in progress` (the launcher holds the operation lock);
  close the manager first.
  dev30 was checked against NBT-AAD's source and the dev28 log before any car
  test (docs/controller-input.md, "iDrive focus hold";
  docs/head-unit-interfaces.md): the hold now opens state 15 before 14 (in
  dev28 it never took), the GUI takes the panel only once iDrive is held and
  only from iDrive alone, its window is panel-sized with the frame scaled ×2
  by the GUI, and the worker no longer holds the manager transaction lock,
  which blocked the launcher's trial confirmation.
  dev31 shows an application the manager starts: a `show` application gets
  surface 1291 (`NBT_SURFACE`, one at a time), and the GUI hands it the panel
  while iDrive stays held, closing when its window goes (PROTOCOL.md, "The
  application surface"). Once on the panel the GUI checks the layer every
  second and steps aside if anything else (Android Auto) takes it; it takes
  knob input only while its own surface is up. An application that shows
  itself without the manager still needs component 5 of docs/app-bridge.md.
  dev31 in the car: the manager appeared, held iDrive and took the knob (the
  first time on screen); the test app's host failed to start it and the QR
  code was missing. The QR was a build bug: qcc ignores -MMD, so QNX objects
  were never rebuilt for header changes and the GUI mixed two layouts of
  UiController. dev32 passes the flags to the preprocessor, makes BUILD_DIR
  absolute (so dependency targets always match), and qnx-check refuses a
  QNX object without a dependency file or older than a header it includes.
  The host failure's step is unknown (one message covered four); dev32 logs
  each with errno and no longer lets a status channel stop the launch.
  dev32 in the car: QR shown, manager update trial confirmed; the test app
  started but never drew (apps had no PATH/LD_LIBRARY_PATH, so the car's
  tools could not load); closing the manager restarted it hidden (pipe closed
  before the exit was reaped). dev33 fixes both (docs/install-flow.md).
  dev34 moves phone installs to the car's Wi-Fi Direct group (wfd0) and shows
  a Wi-Fi QR code for it first (name and passphrase read as NBT-AAD does,
  src/worker/wifi_direct.cpp), so no Android Auto is needed to join. Which
  name source this car has is unverified: the log says `Wi-Fi Direct group
  <name> (name from <file>)` or why it found none.
  dev35 adds Uninstall to every application's settings (hold its name):
  asked first, refused while it runs or anything is being installed, then
  the worker removes its profile, keys, executable, state and data inside the
  managed root (src/lifecycle/app_remover.cpp, src/storage/app_removal.cpp;
  links unlinked, never followed; no remount, /var/opt is writable). The
  installer's "already installed" now points there.
  dev35 in the car: the phone joined Wi-Fi Direct from the code and uploaded,
  but installs failed and uninstalls were "busy": the launcher holds the
  manager operation lock while the manager runs, and the worker asked for it
  again. dev36: nbt_fs_manager_operation_parent() lets the worker use its own
  launcher's hold (src/storage/managed_root.cpp); host tests hold the lock in
  the test process, the worker's parent there. dev35 also showed BACK in the
  test app leaving the panel black: its surface stayed on the layer after it
  exited. dev36's GUI takes the panel back when `RUNNING` stops listing the
  application it handed the panel (or its surface leaves), showing its list.
  The GUI's 120 s idle close stays, as the user prefers; closing during a
  manager trial restarts the GUI hidden by design until the trial confirms.
  dev37 in the car: phone install, health check and BACK from the app worked.
  dev38: no idle close while updating() (session to installation), a health
  check bar (kAppHealthMs), direct surface swaps (show() replaces 256 or our
  other surface; Placement::place keeps ours up), and a stay-connected hint:
  Android drops a network without internet, and passing its check would need
  the car's DNS changed.
  dev38 also moves to two keys (the user's design): the manager key for
  manager releases, one apps key for every application, bundled with the
  manager (tools/embed_key.py -> build/apps-key.inc -> src/package/apps_key.cpp;
  the kit reuses keys/test-app and refuses a worker without it). A phone
  package for an application not installed here installs it after approval
  (Worker::bindNewApp / installNew, nbt_app_install_new). Per-app keys in
  trust/keys/ are no longer written or read. The review screen shows only the
  application and versions.
  dev39: staging a manager update drops a candidate that never confirmed
  (nbt_manager_discard_candidate: dev37's car had one staged and never
  opened, which blocked every later update); the installer names launcher
  refusals; one health window, NBT_HEALTHY_MS = 10 s (apps, manager trial,
  boot guard). dev39 in the car: update, new-app install and the surface
  swaps worked. Its connectivity-check responder did nothing (wfd0's DHCP
  gives no DNS server or router; dnsmasq kept port 53) and dev40 removes
  it. The Wi-Fi problem is how the phone joins: from the camera Android
  keeps mobile data as default and the page times out; from Wi-Fi settings
  (explicitly selected) it asks "stay connected" and then routes to the car.
  dev40 tells the user so on the Wi-Fi code screen.
  Results of every
  kit tried in the car are in [docs/install-flow.md](docs/install-flow.md#test-history).
  The kit names each build `dev<sequence>`; the sequence file is in the keys
  directory and must keep increasing so a newer kit updates an older install.
- The test application has a second setting, **Use the controller** (on/off,
  default on, applies at the next start). Off, a menu launch stays on screen
  until MENU makes the manager step aside, then keeps running (without a
  surface, as in host tests, it shows for five seconds and hides): that is how
  `docs/in-car-procedure.md` steps 4.8-4.11 reach the status label
  (`test  (Hello v1)`), show and live settings, since with the controller the
  app ends on BACK or MENU and the MOST client cannot restart in-process.
- The test application now takes MOST controller input itself when started from
  the menu (same unvalidated client as the manager). Hard keys other than
  BACK/MENU (NAV, MEDIA) move iDrive off the plugin's blank state: the
  manager GUI then takes the application's surface off the panel and closes;
  the application keeps running in the background.
- Manager settings: the debug log toggle applies to the worker at once, the
  plugin at its next selection and the GUI at its next start; controller
  changes apply when the manager next opens. "Hand back controller" is the
  unvalidated `release=1` and stays off in the in-car procedure.
- Hold for settings: 1.5 s (`NBT_UI_HOLD_MS`) was the agent's choice and is
  unvalidated with the real knob; the user may want it changed. "Stop <app>"
  acts at once, without confirmation, and shows only while `RUNNING` (asked on
  returning to the main menu and after a stop) lists the application.
- Settings: choice-type settings (max 12 per app, 6 choices). A failure to
  save the declaration after a confirmed install reports `Update installed;
  settings not updated`. Live snapshots are eventual; see the continuation
  section for retry/start repair after partial writes.
- The GUI controller implements navigation, sessions, review and approval and is
  tested against the real worker on the host. MOST input is tested only against
  a fake DSI library; the DSI ABI, controller layouts, instances and key slots
  come from NBT-AAD's published RE and are unverified here (now configurable).
  The focus hold is host-tested logic plus framework symbols that exist in the
  local `libAppFramework`/`libCoreFramework`; whether two PluginApps can each
  hold a state, and the exact XML property semantics, need a bench. Hard keys
  still act in iDrive. Screen size 640x240, LVGL's 256 KB heap and font sizes
  are placeholders. Worker `APPROVE`/`STOP` are non-blocking, so they do not
  stall the supervised GUI's 3 s heartbeat. Executable copy time on target
  storage is unmeasured. Session defaults `uap0`/`8080` and `/dev/urandom` on
  the unit are unvalidated.
- The debug log is host-tested only. The plugin writes it from the HMI
  process, which must be able to write the manager's file; the plugin does not
  rotate and stops logging if it hits the instant of another process's rotation
  (until the entry is selected again).
- Install and removal remount the plugin partition read-write themselves
  (`mount -uw /net/hu-omap/fs/sda0`) and read-only again at the end, whatever
  the outcome (`mount -ur`). The launcher uses the fixed paths as given and
  checks the folders: the car's plugin folder and `/var/opt` are root:root
  0775, so group write is accepted only for root's group (dev9 and dev13
  failed on that). Its refusals name the failed check (`HMI layout: ...`,
  `manager storage: ...`). Remote commands follow the QNX 6.5 utility
  reference; none depends on `rm`'s exit status, which it leaves undocumented
  for missing files.
- Ordinary manager updates replace GUI and worker only. Keep releases compatible
  with the installed launcher; launcher/plugin replacement is separate setup.
- AAD adoption/migration is unimplemented. See `AAD-IMPORT.md`; rebuilt AAD must
  use managed paths and lifecycle coordination. Preserve legacy settings/OEM
  files; ordinary updates remain within `/var/opt/nbt-manager`. No firewall
  file changes or compatibility symlinks were added (the session rule lives
  only in the running filter).
- Production manager signing keys and a real target compatibility identifier
  have not been provisioned in this session. Temporary test keys are not release
  authority. Do not assume a production signed installer is ready to distribute.
- Power-loss durability/full-storage behavior needs target-specific validation
  and broader fault injection. QNX6 Power-Safe synchronization is an assumption
  to validate, not a guarantee established by host tests.
- The path policy trusts other processes with the same effective UID; QNX 6.5
  lacks directory-relative primitives needed to eliminate every pathname race
  against a malicious same-UID process. Local HTTP is unencrypted; signed
  packages and local approval remain authoritative.

## Build and verification

`make` and `make release` now require explicit release metadata, keys and target
libraries. `make host` and `make test` remain independent of QNX/signing material.
The full documented command is in `README.md`. Output defaults:
`dist/manager.nbtr` and `dist/nbt-manager-installer`; `build/` holds only
intermediate output and `make clean` leaves `dist/` alone.
`make desktop-release` reuses an existing signed bundle, checking the requested
version/sequence/compatibility and public key before embedding it.

Local paths used successfully:

```sh
cd /home/mobyfab/src/reverse/nbt-apps-manager
source /home/mobyfab/src/tools/qnx-6.5-tools/env.sh
make release \
  QNX_SCREEN_LIBDIR=/home/mobyfab/src/reverse/Supra-iDrive6-Android-Auto/milestone-7/lib \
  RHMI_LIBDIR=/home/mobyfab/src/reverse/Supra-iDrive6-Android-Auto/milestone-7/rhmiapp/lib \
  MANAGER_VERSION=v1 MANAGER_SEQUENCE=1 MANAGER_COMPAT=your-target-id \
  MANAGER_SIGNING_KEY=/secure/manager-private.pem \
  MANAGER_PUBLIC_KEY=/secure/manager-public.pem
```

The key paths/metadata above are placeholders. Build with the **QNX 6.5**
toolchain at `/home/mobyfab/src/tools/qnx-6.5-tools` (GCC 4.8.3, 6.5 sysroot;
assembled toolchain, see its README), not `/home/mobyfab/qnx660`. Successful
compilation does not establish runtime/ABI compatibility on the head unit.
BMW/Screen libraries are local external inputs and are not distributed here.
Sourcing a QNX SDK may put an ARM `cc` wrapper first on PATH: Make now defaults host
`CC` to `gcc` and `CXX` to `g++`, keeps `QCC` separate and selects the native Rust
linker explicitly. Each build produces the current host's desktop executable, not
all OS binaries.

### C++ rewrite (2026-11-12)

The manager, SDK, tools and test application now use C++11 without exceptions or
RTTI; LVGL remains C99. The pre-rewrite source is tagged `c99-final`. QNX targets
use the `_gpp` compiler variant and require the device's dynamic
`libstdc++.so.6`; the MOST client loads `libsys_dsi_colibry.gpp.so.8`. These ABI
and runtime assumptions still require bench validation.

Rewrite verification passed `make test check-static-init` with **175 Python
tests** and three optional QR skips, `check-cxx`, and static initialization checks
across 361 objects. The full ASan/UBSan suite passed, as did all 16 deterministic
fuzz targets for five seconds each, QNX artifact and stack checks, signed host and
desktop releases, 25 desktop tests plus native SSH bootstrap, a clean-checkout
launcher build, and an Xvfb simulator smoke test.

Checks for live settings and per-app data (this commit):

- `make test`: all C checks passed; **151 Python tests**, 3 optional QR checks
  skipped. Full ASan/UBSan suite passed the same 151 tests (3 skipped), using
  the documented non-PIE flags and leak detection disabled.
- `make simulator` and QNX 6.5 `make qnx qnx-plugin` passed. This verifies
  build/link compatibility, not target filesystem/display/runtime behavior.
- Desktop: 20 tests passed, 1 optional integration ignored normally; Clippy
  for all targets passed with warnings denied. The explicit native SSH bootstrap
  integration passed against the rebuilt launcher. Only Linux was built locally.
- The Windows kit source declares `show,status,live-settings`; its artifact was
  not rebuilt. No signing keys or release sequences changed. Rebuild before
  the first installation and use the new live-settings checks in the in-car
  procedure. Hardware validation and destructive power-loss tests remain pending.

Checks for status lines and AAD findings (`680991f`):

- `make test`: all C checks passed; 142 Python tests, 3 optional QR checks
  skipped, all executed checks passed. The final full ASan/UBSan suite passed
  the same tests (`build/asan`, leak detection disabled, non-PIE as above).
- Tests cover status readiness/grants, native descriptor transfer and shorter
  writes, malformed/oversized records, symlink/hard-link/special-file rejection,
  protected-byte preservation, old-run reset, worker restart, withdrawal before
  slow shutdown and the complete native test-app → worker → GUI path.
  GUI tests cover full-size 16-app status replies, fallback, live refresh and
  a push/release while refresh is pending. Candidate environments omit the
  user status channel while preserving signed requirements across recovery.
- `make simulator` and QNX 6.5 `make qnx qnx-plugin` passed. Cross-compilation
  does not validate shared-memory I/O, descriptor/lock behavior or display
  handoff on the car.
- Desktop tests: 20 passed, 1 optional test ignored normally; Clippy for all
  targets passed with warnings denied. The native SSH bootstrap test passed
  when run separately with the rebuilt launcher. Only Linux desktop compilation was
  checked locally; the normal cross-platform CI remains separate.
- The Windows kit source now requires `show,status` for its test packages;
  the artifact was not rebuilt and no signing key/release sequence changed.
  The new bridge and inherited-SIGCHLD changes belong to the first release;
  no installed manager needs migration.

Checks for bring-to-front (`c549dbf`):

- `make test`: all C checks and **137 Python tests** successful, with 3 optional
  QR reference checks skipped. The same full suite passed under ASan/UBSan
  (`build/asan`, `-fno-pie` / `-no-pie`, leak detection disabled as before).
- Tests cover repeated show requests without another child, required/optional
  grants, requests before readiness, no-grant/default-fatal-signal apps,
  mismatched lock owners, whole-file lock rejection, capability withdrawal
  before slow shutdown, worker restarts, GUI staying open on `ERR running`, Screen re-show and uninterrupted heartbeats.
- `make host simulator` and `make qnx qnx-plugin` passed with the QNX 6.5
  environment. Actual Screen stacking and byte-range locks still need the car.
- Desktop `cargo test --locked`: 20 passed, 1 optional integration test ignored.
  The release parser fixtures now match the signed `bridge=1.0|show` default.
  Clippy (all targets, warnings denied) passed, and the ignored
  `native_bootstrap_over_ssh` test passed when run explicitly with launcher-host.
- The Windows kit's source packaging now declares the test app's `show`
  requirement, but the kit artifact was **not rebuilt** in this step. No private
  keys or release sequences were changed. Rebuild it before the first install.

Checks for the bridge versioning step (`0a31eae`):

- `make test`: C checks (including `bridge-test`) and **129 Python tests**,
  all successful, with 3 optional QR reference tests skipped.
- The same full suite passed under ASan/UBSan (`build/asan`, README flags plus
  `-fno-pie` / `-no-pie`; `ASAN_OPTIONS=detect_leaks=0`).
- Desktop `cargo test --locked`: 20 passed, 1 explicit integration test ignored;
  `cargo clippy --locked --all-targets -- -D warnings` passed. The ignored
  `native_bootstrap_over_ssh` test was then run explicitly against the rebuilt
  `build/launcher-host` and passed.
- `make qnx qnx-plugin` passed using the QNX 6.5 toolchain above, for the final
  sources. This is build/link validation; no new head-unit runtime results.
- The Windows in-car kit was **not rebuilt** in this step; no signed distributable
  release was produced. Cross-platform CI results must be checked after push.

Checks for `ae369cc` (hold for settings): `make test` (118 Python tests, C
checks), ASan/UBSan suite and `make qnx qnx-plugin` passed; simulator driven
headless: hold on `media-player` → dialog → its settings → Stop → "stopped",
Settings → manager settings.

Checks for `1c23bbf` (manager settings):

- `make test`: C checks and **118 Python tests passed**, 3 optional skipped;
  the full suite passed under ASan/UBSan (README flags, `build/asan`).
- `make qnx qnx-plugin` passed with the QNX 6.5 toolchain.
- Simulator driven headless: Settings → Manager → Debug log → on, saved value
  shown; Manager drawn in the action colour above the applications.

Checks for the 2026-11-10 commits (through `cf610a1`):

- `make test`: C checks and **117 Python tests passed**, 3 optional QR reference
  tests skipped. The full suite also passed under ASan/UBSan with the README
  flags as written (`build/asan`; a stale `build/asan/liblvgl.a` built without
  PIE broke linking once: delete `build/asan` and rebuild).
- `make qnx qnx-plugin` passed with the QNX 6.5 toolchain (worker, launcher,
  GUI, test app v1/v2/broken v3 now linking the MOST input and `-lcrypto`).
- The simulator was driven headless (Xvfb + python-xlib XTest): Settings → test
  → Background color → red, start `test` (red hello world), BACK returns to the
  menu. Rendering of the scroll bar and dialog was checked from rendered frames.

Checks for the debug log, phone status and controller release commits:

- `make test`: C checks (including `log-test`, `hold-test`,
  `controller-config-test`) and **102 Python tests passed**, 3 optional QR
  reference tests skipped (the page-script test runs when Node is installed);
  the suite also passed under ASan/UBSan (`build/asan`, with
  `-fno-pie`/`-no-pie` added).
- `make qnx qnx-plugin` passed with the QNX 6.5 toolchain; the plugin still
  exports only `pluginAppInfo`.

Checks for the GUI controller commit:

- `make test`: C checks including `ui-test`, and **90 Python tests passed**;
  3 optional QR reference tests skip without segno/zxing-cpp/Pillow and passed
  when run in a scratch virtualenv with segno 1.6.1 and zxing-cpp 2.2.0. The full suite also passed under
  ASan/UBSan (`build/asan`, with `-fno-pie`/`-no-pie` added).
- `make qnx qnx-plugin` passed with the QNX 6.5 toolchain above.

Earlier checks at the previous baseline:

- `make test BUILD_DIR=build/initial-recovery`: C checks and **80 Python tests
  passed**. The new release regression checks repeat packaging, preservation of
  an existing bundle when verification fails, and stale sequence rejection.
- Complete default `make` with QNX inputs and a temporary RSA-3072 key passed;
  `make desktop-release` passed again. The optimized desktop installer stayed
  open during a four-second Xvfb smoke test. Disposable test release/executable
  outputs and the temporary key directory were removed afterward.
- Earlier C implementation milestone passed host and ASan/UBSan suites (79
  Python tests before the final build regression). On this host the sanitizer
  run used `-fno-pie` in CFLAGS and `-no-pie` in LDFLAGS in addition to the README
  flags. Do not describe the final 80-test suite as sanitizer-tested without
  rerunning it.
- Earlier Rust checks: 14 tests, clippy with warnings denied, Windows cross-test
  compilation, and the explicit ignored SSH/native-bootstrap integration test.
  See `desktop/README.md` for the integration-test command. These were before
  the final Makefile-only integration change; do not imply a fresh run of each.
- `.github/workflows/desktop.yml` builds/tests on Linux, Windows and macOS;
  Linux also runs the signed native bootstrap through a mock SSH server.
  A prior matrix run passed at `8f87128`; check GitHub for newer run status.
  CI currently builds development installers, not production signed releases.

## Suggested continuation without hardware

1. Read `AGENTS.md`, `plugin/README.md`, `PROTOCOL.md` and
   `docs/manager-release.md`; inspect the GUI and client IPC before choosing the
   next implementation slice.
2. Done: GUI controller layer, non-blocking worker `APPROVE`/`STOP`, MOST
   controller input, LVGL rendering and simulator, QR-only pairing with a
   per-session URL and 60 s countdown, iDrive focus hold, controller
   configuration. (QR: `src/gui/qr_code.cpp`, verified against segno at capacity for all
   versions/masks and scanned with zxing-cpp from the rendered frame; segno
   1.6.1 pads byte-aligned streams with an extra zero byte, so only
   full-capacity symbols match exactly.)
3. Done: bench checklist (`docs/bench-checklist.md`) and the GUI/worker/plugin
   debug log. Run the checklist first when a bench unit is available.
4. Extend fault injection: worker crash mid-install, GUI dying while hidden,
   full disk during upload, corrupted transaction records.
5. Done: live status on the phone page; MOST unregistration on GUI exit
   (`release=1`, off by default until the bench confirms the `0x5` payload,
   which mirrors `0x1` by assumption). Fetch current library documentation
   through Context7 if adding a library.
6. Done (2026-11-10): Windows in-car kit and procedure, installer test tools,
   application provisioning, generic install session, detached hosts, menu
   rework (banner, push/hold, dialog, scroll bar, action colours), application
   settings and the hello-world test app.
7. Done (`1c23bbf`, `ae369cc`): manager settings (debug log, invert knob,
   hand back controller) behind the main menu's Settings; holding an
   application opens its settings, where it is also stopped; apps need no
   iDrive plugin of their own.
   Kit dev4 is built; next: run `docs/in-car-procedure.md` in the car
   (4.3-4.4 hold-for-settings and manager settings; 4.8-4.11 status label,
   show, live settings and the stop entry) with the debug log on, and keep the
   saved log's `MOST registry` lines.
8. The bridge (`docs/app-bridge.md`): versioning, bring-to-front, status lines and
   live settings are done (see the continuation section). Autostart worker core is
   checkpointed; GUI/launcher/plugin remain paused. Next when resumed: autostart and
   the boot guard (host-testable); then
   the read-only car inventory and discovery logging (car data, MOST) to
   find more car data; then car data publishing, focus hold for
   applications and the favourite entry (car); then NBT-AAD adoption with
   its author. Discovery revisits the early "no traffic capture" scope note:
   confirm with the user before building it.
9. Define the production signing/compatibility/release process before publishing
   distributable signed desktop installers. AAD migration remains a distinct
   feature requiring rebuilt components and settings-preservation fixtures.

The user has not authorized this document to replace the next session's task:
use these as continuation options and incorporate their latest instruction.
