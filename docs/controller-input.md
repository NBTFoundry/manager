# iDrive controller input

The manager GUI reads the iDrive controller directly from the MOST bus. An
iDrive PluginApp does not receive knob turns or pushes; it receives only
selection actions and state focus changes. The manager therefore registers its
own MOST client and does not route input through the HMI.

Nothing in this path has run on a head unit. It is built from reconstructed
interfaces and must be validated on a bench before use in a vehicle.

## Provenance

The service names, request and information ids, payload layouts, controller
message formats and DSI library ABI below are protocol facts. They were
published by the NBT-AAD project (Supra-iDrive6-Android-Auto), which
reverse-engineered them from the head unit and observed controller messages on
cars. NBT-AAD's code is under the PolyForm Noncommercial license. No code was
copied; `src/most/codec.cpp`, `src/most/ccs.cpp` and `src/most/input.cpp` are
independent C++11 implementations. The interface version, request and
information ids, the message layout and the client's virtual table slots have
since been checked against the vendor libraries of firmware NBTevo_Y21432J; see
[head-unit-interfaces.md](head-unit-interfaces.md). The controller message
formats remain NBT-AAD's observations.

## Components

| File | Role | Host tests |
| --- | --- | --- |
| `src/most/codec.cpp` | Bounded parsers for DSYSMOSTServer `MessageType` and Registry payloads; registration payload builders. | `tests/test_most_input.py` |
| `src/most/ccs.cpp` | Controller decoder: MOST status messages to `nbt_input` (up, down, select, back, home). | `tests/test_most_input.py` |
| `src/most/input.cpp` and `src/most/dsi_client.cpp` | Load the DSI library with `dlopen`, register the client, decode messages on the library's connection thread and write one byte per input to a non-blocking pipe. | Against `tests/fake_dsi.c`, a host library that exports the same symbol names. |
| `apps/manager/main.cpp` | Reads the pipe and applies input to the GUI controller. | `tests/test_manager_gui.py` |

## Sequence

1. `dlopen` `/opt/sys/lib/libsys_dsi_colibry.gpp.so.8` (or the `/fs/sda0`
   copy) and resolve `CDSIConnection`, `CDSIClient` and `CDSIOStream` members.
2. Construct a connection and a client for `MOSTSubSystem.DSYSMOSTServer` 4.5.
   Copy the client virtual table and replace slots 3 (attached), 4 (detached)
   and 7 (data). Add the client and run `CDSIConnection::start` on a detached
   thread. All callbacks and requests happen on that thread.
3. On attach, send request `0xF` (register client, name `NBTMGR_INPUT`) and
   subscribe to the attributes and information ids the HMI's MOST client uses.
4. On information `0x80000000` with an accepted client id, subscribe to
   `0xC0000000` and send request `0x1` (register device controllers) for FBlock
   `0xE5` instances `01 02 03 05 10 12 14`.
5. Information `0x80000001` carries MOST messages. The decoder uses status
   messages (op `0xC`) from FBlock `0xE5`.
6. Attribute `0xC0000001` (Registry, subscribed in step 3) lists the FBlocks
   present in the car. It does not affect input; with the debug log on it is
   logged as `MOST registry` lines (layout in `head-unit-interfaces.md`).

## Controller mapping

| Controller | Input |
| --- | --- |
| Rotate clockwise, per detent | Down |
| Rotate counter-clockwise, per detent | Up |
| Push (on press) | Select |
| Tilt up / down | Up / Down |
| Tilt left, BACK key | Back |
| MENU key | Leave the manager (refused while installing) |
| Tilt right, other keys | Ignored |

CCS35up controllers (default instance `0x03`) send rotary/push/tilt state in
function `0xC46` and hard keys in `0xC47`/`0xC4F`. In `0xC46`, byte 4 is the
tilt as a 12-step clock starting at 1: 0 centred, 1 up, 4 right, 7 down, 10
left, diagonals 2, 5, 8, 11, held until it returns to 0 (car-verified by
NBT-AAD, `milestone-7/tests/fixtures/controls_2026-10-23_push.txt`; the HMI's
`rcs::rotaryDeflectionToMost` in `libhmiremoting.so` produces only these
values). The manager's ranges (12-2 up, 3-5 right, 6-8 down, 9-11 left) take
them as such; Doom up to 1.0.6 read 12/3/6/9 and turned every tilt into a
diagonal. Older CCS controllers
(default instance `0x01`) send rotation in `0xC10` and buttons in `0xC20`.
A rotary jump of more than 16 detents is treated as a counter re-base. Losing
the link forgets positions and held keys.

## Configuration

Instances and key slots vary by car. The defaults above can be overridden with
`/var/opt/nbt-manager/manager/controller.conf`; `config/controller.conf.example`
lists every key with its default and the format is described in
`include/nbt/controller_config.h`. The file must be a regular file owned by the
manager root's owner and not writable by group or others. It is read when the
GUI starts:

| Key | Meaning | Default |
| --- | --- | --- |
| `knob_instance` | CCS35up controller instance | `03` |
| `ccs_instance` | Older CCS controller instance | `01` |
| `instances` | FBlock `0xE5` instances registered for (1-8, must include both) | `01,02,03,05,10,12,14` |
| `invert` | `1` reverses the rotation | `0` |
| `knob_menu_slot`, `knob_back_slot` | CCS35up hard key byte index (0-6) | `0`, `1` |
| `ccs_menu_slot`, `ccs_back_slot` | Older CCS key sensor bit (0-6) | `0`, `1` |
| `release` | `1` unregisters the controllers when the GUI exits (request `0x5`) | `0` |

An invalid file (unknown or repeated key, bad value, MENU equal to BACK, a
controller instance not registered) is ignored as a whole: the defaults apply
and the application list shows "Controller config invalid; defaults used".

The GUI stays open while input is available and closes after two minutes
without input. Without the DSI library it remains a preview that closes by
itself. Neither the idle timeout nor MENU closes the GUI while an installation
is in progress.

## iDrive focus hold

The HMI receives the same controller input as the manager. While the manager
window is shown, the plugin therefore holds iDrive on an inert state of its own,
so a knob turn or push selects nothing in the iDrive menu underneath. This
follows the approach NBT-AAD validated on cars for Android Auto, itself modelled
on how iDrive holds its native CarPlay state; no NBT-AAD code was copied.

- `plugin/NbtManager.xml` adds a blank state 14 with a parent state 15. Neither
  has a selectable component. Focus event 52 opens 15, event 50 enters 14 and
  event 51 returns to the list holding the entry (native state 20, its
  `insertStateID`).
- iDrive takes event 50 only while 15 is on top. In dev28 the plugin fired it
  from the Media list and iDrive ignored all three tries: no `blank state focus`
  line followed, and a knob push meant for the manager also selected the
  manager's entry underneath. NBT-AAD fires its blank-state event only from its
  own menu, and its one-press entry first opens that top-level menu (event 53).
  So the entry fires event 52, and once 15 has focus the plugin fires event 50
  at once.
- The plugin registers focus handlers (type 1, one bool) for both states and
  polls every 250 ms on its own thread with a posted `bmw::Runnable`. The
  decisions are in `src/gui/hmi_hold.cpp` (host tested in
  `tests/test_hmi_hold.cpp`).
- Focus events go out from a sender thread of the plugin's own (dev87):
  `triggerEvent` waits for iDrive's main thread with no timeout, and on
  2026-11-17 one never returned and iDrive froze
  ([car-issues-2026-11-17.md](car-issues-2026-11-17.md)). An event not
  answered within 3 s drops the hold, sends the GUI aside, and logs a
  snapshot of iDrive's threads (again at 10 s).
- Selecting the entry starts the launcher and fires event 52. The GUI counts as
  running while `manager/gui.lock` is write-locked and its liveness file is
  fresh.
- **The GUI covers iDrive only while iDrive is held.** It starts hidden, its
  window posted (so LayerManager makes the surface) but off the panel and its
  controller input ignored. The plugin writes `show` once iDrive is on 14 with
  the GUI running. If the hold never takes, the manager never covers iDrive's
  menus, a camera view or a warning, and the debug log says so (no `iDrive
  signal show` after `shown once iDrive holds its focus`).
- The GUI takes controller input only while its surface is on the panel, not
  merely once told `show`: before LayerManager has put it there, or after
  Android Auto has taken the layer (checked once a second), nobody sees it.
- **An application keeps the hold.** When `START` answers `OK screen`
  (`PROTOCOL.md`), the GUI puts the application's surface 1291 on the panel in
  place of its own once LayerManager has made it (a direct swap: iDrive does
  not show in between). The GUI stays running, hidden and
  ignoring input, so the plugin keeps iDrive on 14 and the knob drives only the
  application, which reads the controller itself. When the application ends
  (BACK in it: `RUNNING` stops listing it, even if its surface stays on the
  layer undrawn, as on the car), its window goes, or it never appears within
  10 s ("<app> did not appear", which also sends `STOP <app> fault` to release
  `surface.lock`), the manager comes back on the panel with its
  list. Across both the manager and a foreground application, continuously
  holding the hard `BACK` button (excluding rotary left tilt) for 5 seconds
  triggers the forced escape (`5 s BACK escape triggered; stopping` in the GUI;
  during a package installation the GUI ignores it and keeps installing, but the
  capture record keeps the 1.5 s watchdog and the shim's escape, so iDrive still
  gets its controller back): the shim (when active) latches capture off
  for that session generation, and the GUI flushes `STOP <app> fault` if an
  application is running, writes `off` to capture control, and exits cleanly.
  When any surface but ours takes the layer (Android Auto), the GUI
  exits and the plugin returns iDrive to its list.
- BACK pops iDrive from 14 to 15; the manager uses BACK itself, so the plugin
  returns iDrive to 14. Since dev89 it does so from the parent state's focus
  callback, at once; before, at the next poll, up to 250 ms later, and a
  second BACK or a knob turn in that window took iDrive to its list (the
  manager log of 2026-11-17: iDrive takes a BACK 0.1 to 0.5 s after the
  press).
- When iDrive takes the screen, the GUI writes the last 48 controller
  messages its MOST client received, raw and oldest first, to the debug log
  (`controller messages before iDrive took the screen`, then lines like
  `-120 ms 05.03 fkt C46 op C len 6: ... -> 1`, the last number being the
  inputs decoded; `src/most/input_trace.cpp`).
- Keeping iDrive from receiving the controller requires the early-boot
  `DSIClientDataResponse` preload shim (`/var/opt/nbt-manager/state/manager/libnbt-capture.so`,
  loaded through iDrive's environment file, which `NBTRLS2` setup names in
  `starter.cfg` and the manager arms and keeps by itself; see
  [input-capture.md](input-capture.md)). Since kit dev111 the car build is
  **active** (`-DNBT_CAPTURE_OBSERVER=0`, the user's decision of 2026-11-18
  after dev107-dev109 showed OPTION still reaching iDrive in Doom): while the
  manager or an application is on the panel, iDrive gets none of the knob
  (0xC46), the controller keys (0xC47/0xC4F), the older controller
  (0xC10/0xC20) or the favourite keys (FBlock 0x15, 0xC02), presses and
  releases alike (a favourite's release would run it). Volume stays with
  iDrive: AudioMaster 0x20, the centre stack's volume button 0xC01 and the
  rear volume functions are never inspected. Capture is off whenever iDrive
  takes the screen (`aside`: warnings, cameras), within 1.5 s of a hung or
  dead GUI, and after the 5 s BACK escape. Known effects, not yet seen in the
  car: iDrive's first knob turn after the manager closes moves by the detents
  turned inside it (its rotary counter missed them), and a key pressed before
  the manager showed and released inside it stays pressed in iDrive until
  pressed again (a favourite key held that way could store a favourite).
  The manager and its applications are unaffected: they read the controller
  through their own MOST clients, not through iDrive.
  Stopping HmiMain3D is not an option: on 2026-11-17 a stuck HMI made other
  processes miss their heartbeats within about 20 s and the watchdog reset
  iDrive.
- Any other exit from 14 is iDrive's: MENU, NAV, MEDIA and the other hard keys,
  Check Control warnings, camera views. After 500 ms the plugin writes `aside`
  to `/dev/shmem/nbt-manager-hmi` and stops holding. The GUI exits and gives
  iDrive the panel back; during an installation it only does the latter,
  ignores controller input and keeps the worker running. Selecting the entry
  again holds again and then writes `show`. Not re-holding here is deliberate:
  NBT-AAD found that doing so hid warnings.
- An application the GUI showed is paused there (dev88; until dev87 it kept
  running in the background, reading the controller and playing sound): the
  GUI sends its host `SIGUSR2`, and the host stops the application's process
  group (`SIGSTOP`), drops MOST messages and gamepad input for it instead of
  queueing them, and holds its heartbeat check. Selecting the application in
  App Manager again continues it (the show request) with a fresh heartbeat
  window; after 2 minutes paused the host stops it (`SIGTERM`, then
  `SIGCONT` so it handles it). `src/process/app_pause.cpp`,
  `src/gui/app_aside.cpp`, `tests/test_app_pause.cpp`.
- When the GUI exits while iDrive is on 14 or 15, the plugin fires event 51.
- iDrive refuses focus events while a popup is open; the plugin retries up to
  four times, then leaves iDrive alone for that hold.

The signal file holds one line, `<sequence> aside|show`. The GUI takes the
sequence already there as stale before it first writes its liveness file, so a
`show` the plugin sends as soon as it sees the GUI alive is never missed, and
ignores lines that do not parse. Both files are rewritten in place (one
`write()`, then `ftruncate()`): truncating first let a reader find them empty,
which NBT-AAD hit with its own state file. Both `/dev/shmem/nbt-manager-hmi` and
`nbt-manager-hmi.alive` are opened with `O_NOFOLLOW`, `FD_CLOEXEC`, `0600` on
creation, owner (`st_uid == geteuid()`) and `lstat`/`fstat` inode verification
so foreign or symlinked shared-memory entries are rejected (`src/gui/hmi_hold.cpp`).

## Updated camera evidence

AAD `0a314d4` reports button-opened camera views with neither reverse nor a
PDC change, and a `GFXIDENTIFIER_INVALID` screen that its generic watcher
skipped. Re-holding can cover the camera. Contexts 1023 (F-series
NBTevo_Y24073J) and 692 (Supra Top View) are source-specific findings; see
`app-bridge.md`. Include the camera button and closing/return behavior in the
car checks. Our manager's focus handling remains unvalidated on hardware.

## Known gaps

- The focus hold has not run on a head unit. The state and event numbering, the
  focus handler type, `triggerEvent`, `post` and the `Runnable` layout are
  taken from NBT-AAD and the framework's exported symbols. Whether two
  PluginApps can each hold a state is unverified.
- Hard keys acted in iDrive until dev109 (MENU also opened iDrive's main
  menu, OPTION its options menu). The active capture shim (dev111) holds them
  back while the manager or an application is shown; NAV, MEDIA, COM and MAP
  then do nothing in the manager, and MENU closes it with iDrive returned to
  the entry's list (event 51).
- Unregistration is off by default. With `release=1` the GUI asks the
  library thread to send request `0x5` (`unregisterDevCtrls`) at its next
  callback when it exits (also on SIGTERM), then stops delivering input, and
  waits at most 300 ms. The payload mirrors request `0x1` (client id, count,
  keys): the library declares the same arguments for both
  (`head-unit-interfaces.md`). NBT-AAD lists the request but never sends it,
  so whether the server accepts it is still a bench check. Requests are only ever sent
  from the library's thread, so a release needs some MOST traffic after the
  exit request; without it the log shows `controller release timed out` and
  registration is left to end with the process.
- Object sizes, member offsets and the stream constructor signature are
  firmware-specific assumptions. The virtual table slots (3, 4, 7) match the
  `CDSIClient` of NBTevo_Y21432J, whose table has 8 entries, the number
  `src/most/dsi_client.cpp` copies. FBlock 0xE5 and its functions are confirmed as
  `CentralControlSystem` (0xC10 `ccsRotaryEncoder`, 0xC20 `ccsPushButton`)
  and `CentralControlSystem35up` (0xC46 `RotaryEncoderState`, 0xC47
  `PushButtonsState`, 0xC4F `PushButtonsState2`).

## Bench validation

Run these as part of [bench-checklist.md](bench-checklist.md), with the debug log
on: it records MOST registration, every decoded input and the focus hold.

1. The GUI loads the library and the log shows a registration reply with a
   valid client id. Missing symbols leave the GUI in preview mode.
2. Rotate, push, tilt, BACK and MENU produce the mapped inputs, in both
   directions and on both controller protocols where available.
3. While the manager is shown, knob turns and pushes do nothing in iDrive.
   BACK keeps iDrive on the blank state; NAV, MEDIA, a Check Control warning
   and the reverse camera close the manager (or hide it during an
   installation), and the entry brings a hidden manager back. After the manager
   exits, iDrive is on the entry's list.
4. The HMI's own controller handling is unchanged after the manager exits, and
   no MOST communication setting changes.
5. Repeated open and close, and loss and return of the MOST link, neither leak
   registrations nor stop input. Repeat with `release=1`: the log shows
   `MOST controls released` and `controller release done` at each exit, the
   HMI's own controller handling is unaffected, and the next open registers
   and receives input again.
6. The complete flow works: list, session, pairing by QR code, review, two
   confirmations, installation, health check and result. MENU during
   installation does not close the GUI.


## Phone opening (dev41)

The one-second plugin poll reads the signed door's fixed wake marker and, only
after the stationary/home/display gate passes, follows the same `HmiHold::entry`
and event 52/50 path as a menu selection. No HTTP input can synthesize a knob
press or approve an action. Pairing, install/recovery and phone-requested
uninstall default to refusal and require actual controller input in the GUI.

The plugin subscribes to driving.speedActual and hmi.graphicalContext and also
requests them each second; only callbacks update their receipt timestamps.
Speed changes enter the optional debug log for ordinary-drive observation.
The display probe accepts only surface 256 alone; Android Auto surfaces or
unavailable output are busy. Automatic opening initially permits only the
observed ID6 home context (3 / LT_Table_ID6L_Pad_Title); navigation/camera
coverage and actual CDS freshness need car evidence. See phone-access.md and
the authoritative signed/private commands in ../PROTOCOL.md.
