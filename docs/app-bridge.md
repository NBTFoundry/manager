# The bridge: car interface for applications

The bridge is the documented interface between managed applications and the
car. Applications use only this interface; the manager (its iDrive plugin,
worker and hosts) is its one implementation and the only code that talks to
iDrive, car data and the BMW plugin ABI. When a firmware needs different
plugin code, the manager changes and applications do not.

Managed applications ship no iDrive plugin of their own: the manager's menu
starts them, holding the push opens their settings, and they are stopped
there. For a simple application that is enough. An application that takes
over the screen for long, such as Android Auto (NBT-AAD), needs more from
iDrive than a launch. Today NBT-AAD's own PluginApp provides it. This
document designs the interface the manager must offer before such an
application can drop its plugin. It records facts about NBT-AAD's
behaviour from its published README and sources; NBT-AAD is PolyForm
Noncommercial and no code is taken from it.

The current provider is **bridge 1.4 with
`show,status,live-settings,most,gamepad,controller`**. Applications on the SDK
(`NBTFoundry/sdk`) get the application side of every implemented capability.

| Capability | Since | Status | Section |
| --- | --- | --- | --- |
| `show` (bring to front) | 1.0 | Implemented | [1](#1-bring-to-front-capability-show) |
| `status` (menu label) | 1.0 | Implemented | [2](#2-status-line-capability-status) |
| `live-settings` | 1.0 | Implemented | [3](#3-settings-for-larger-applications-live-settings) |
| `most` (MOST messages under a signed policy) | 1.1, `sub` rules 1.2 | Implemented | [most-bridge.md](most-bridge.md) |
| `gamepad` (wired USB gamepad) | 1.3 | Implemented | [gamepad-bridge.md](gamepad-bridge.md) |
| `controller` (decoded iDrive controller) | 1.4 | Implemented | [8](#8-controller-input-capability-controller-bridge-14) |
| `car-data`, `car-data-raw` | — | Planned | [4](#4-car-data-capabilities-car-data-car-data-raw) |
| `hold` (focus hold asked by the application) | — | Planned | [5](#5-focus-hold-for-applications-capability-hold) |
| Autostart | — | Worker autostart and boot guard implemented; application autostart planned | [6](#6-start-automatically) |
| Favourite entry | — | Planned | [7](#7-favourite-entry) |

Implemented capabilities are host-tested (signed requirements, dependency checks,
durable records, launch grants, `SIGUSR1` show requests, live menu labels through
detached hosts) and cross-built for QNX; the parts that run inside iDrive are
checked on the car with [bench-checklist.md](bench-checklist.md). The read-only
inventory procedure is in [car-inventory.md](car-inventory.md).

## What NBT-AAD's plugin does today

| Role | How NBT-AAD does it | Our manager today |
| --- | --- | --- |
| Reverse camera | Car data `driving.gear`, `sensors.PDCStatus` and camera graphical contexts, passed to the daemon; Android Auto steps aside for the camera | Nothing. **Safety-relevant: blocks adoption** |
| iDrive warnings, driving mode, DSC, day/night, calls | Car data `hmi.graphicalContext`, `driving.mode` / `driving.mode35up`, `driving.DSCActive`, `vehicle.dayNightChangeOver`, `communication.callStatus0` / `1` | Nothing. Warnings (Check Control, collision) are safety-relevant |
| Focus hold while Android Auto is shown | A blank state holds iDrive's focus so the knob does not drive iDrive's menus underneath; re-held after BACK/OPTIONS and around phone calls | Held while the manager's window, or an application it started, is on the panel (`docs/controller-input.md`) |
| Bring to front | A second start sends `show` to the running daemon | Granted `show`: the host requests the existing app to show; otherwise `ERR running` keeps the manager open |
| Status | Entry text "Connecting..." and an animated icon, from a state file the daemon writes | Ready/granted applications supply a 24-character status label; otherwise "(running)" |
| Favourite button | A second, one-press entry with a fixed UUID, storable on the console's numbered keys | One "App Manager" entry: two steps to reach an application |
| Leaving iDrive's menu | Once connected, iDrive is moved to its main menu; "MENU key opens iDrive" | Nothing |
| Start automatically | The daemon starts 15 s after the head unit boots, with a boot-loop guard | The manager runs only when its entry is selected |
| Settings | 7 checkboxes, several read live; persistent values (`touch-peer`, `spp-model`) and many `nbt-aad.env` options | Up to 12 choice settings; startup environment and live-settings snapshot |

## The interface at a glance

What an application sees, once implemented. This table is the contract; the
sections below say how the manager provides each part.

| Part | Direction | Form |
| --- | --- | --- |
| Lifecycle | manager -> app | `--ready-fd 3 --lease-fd 4`, readiness and heartbeats (`PROTOCOL.md`, exists) |
| Launch mode | manager -> app | `NBT_LAUNCH=user` or `check` (exists) |
| Settings | manager -> app | `NBT_SETTING_<KEY>` (exists); `NBT_SETTINGS_FILE`, re-readable (3) |
| Data directory | manager -> app | `NBT_DATA_DIR` (3) |
| Interface version and grants | manager -> app | `NBT_BRIDGE=<major>.<minor>`, `NBT_BRIDGE_CAPS=<granted capabilities>` |
| Bring to front | manager -> app | `SIGUSR1` (1, implemented) |
| Window on the car | manager -> app | `NBT_SURFACE=1291` for `show`: bind the panel-sized window to it (1, implemented) |
| Status line | app -> manager | `/dev/shmem/nbt-bridge1-<app>.status` (2) |
| Car data | car -> app | `/dev/shmem/nbt-bridge1-car-<name>`: documented fields; `...-raw-<source>` unstable (4) |
| Screen and focus | app <-> manager | `/dev/shmem/nbt-bridge1-hmi` requests, `/dev/shmem/nbt-bridge1-<app>.hmi` `aside` (5) |
| Start with the car | manager | per-application setting (6) |
| Favourite entry | manager | one fixed iDrive entry, target chosen in Settings (7) |
| MOST | car <-> app | `most`: datagram socket `NBT_MOST_FD=6`, functions limited by the signed `bridge-most` policy ([most-bridge.md](most-bridge.md)) |
| Controller input | car -> app | `controller` (bridge 1.4): decoded states on `NBT_CONTROLLER_FD=8` (8); raw messages remain possible through `most` |

Every name, path and format here belongs to one interface version; see
"Versioning and dependencies".

## Versioning and dependencies

The bridge has its own version, `<major>.<minor>`, independent of the
manager's release version. Applications depend on the bridge, never on a
manager release.

- **Minor** changes only add: a capability, a car-data field, an optional
  line or value. An application built for 1.2 runs unchanged on 1.5.
- **Major** changes anything else: a removed or renamed capability or
  field, a changed format or meaning, a changed signal. Each major has its
  own shared-memory names (`nbt-bridge1-...`, `nbt-bridge2-...`), so two
  majors can be served side by side.
- A manager serves the current major and the previous one for at least one
  release, so applications can be rebuilt in their own time. The GUI marks
  an application that uses a major due to be dropped.

What an application declares, in its signed package (new manifest lines; a
worker without bridge support rejects them, so only packages that use the
bridge carry them):

```
bridge=1.2
bridge-uses=show,status,car-data
bridge-optional=hold
```

- `bridge`: the major it is built for and the lowest minor it needs.
- `bridge-uses`: capabilities it cannot run without.
- `bridge-optional`: capabilities it uses when present and copes without.

Checks, so a dependency is never broken silently:

- **Installing.** The worker checks the declaration when it verifies the
  package, before the review screen: the major must be served, the minor
  at most the manager's, every `bridge-uses` capability implemented. A
  failure is refused like a bad signature, with the reason ("needs bridge
  1.3; this manager has 1.2"), on the phone page and the GUI. The accepted
  declaration is stored **inside** `state/<app>/installed` (see implementation
  notes below), so confirmation and recovery keep it with its executable.
- **Updating the manager.** A manager release lists the bridge versions and
  capabilities it serves. Staging refuses a manager update that would drop
  a major or a required capability that an installed application uses,
  naming the application; uninstalling or updating that application first
  is the way through. Recovery to an older manager release is checked the
  same way and warns instead of refusing, since recovery must stay
  possible.
- **Running.** The host passes `NBT_BRIDGE=<major>.<minor>` (the version
  served for the application's major) and `NBT_BRIDGE_CAPS`: the declared
  capabilities that are both implemented and available on this car. A
  capability can be implemented but unavailable (for example car data
  that this firmware does not publish); a `bridge-uses` capability that is
  unavailable stops the start with "<app> needs <capability>, not available
  on this car".
- **Car side.** Firmware differences are the bridge's problem, not the
  application's: documented car-data fields keep their meaning across
  firmwares, and a field the car does not provide is absent, never
  repurposed.

Dependencies between applications (one using another's service) are out of
scope for now; a later `requires=<app>` line would follow the same
install-time and update-time checks.

## Implemented versioning contract

Application signing: `tools/package.py --bridge 1.0 --bridge-optional show`.
The three ordered fields appear after `sha256`, before any `setting=` lines.
When `bridge` is present, both capability fields are mandatory and may be empty.
Versions are canonical decimal `major.minor`, major 1-65535 and minor 0-65535
(no leading zeros). Each capability list is at most 255 ASCII bytes; names are
1-24 `[a-z0-9-]` characters beginning with a letter. Names are unique and
required/optional lists must not overlap. Unknown optional names are accepted
and omitted from grants. Unknown required names fail the implementation check.
The signed manifest remains bounded at 2048 bytes, including settings.

`src/model/bridge.cpp` provides the allocation-free parser, compatibility check
and intersection of declared, implemented and available capabilities. The C++11
API is in `include/nbt/model/bridge.hpp`; `src/model/bridge_api.cpp` retains the
C ABI. The worker checks dependencies after signature verification and again at
installation. Reasons reach the existing GUI and phone status messages. The host
checks them before starting the application. Both user launches and candidate
health checks receive only `NBT_BRIDGE=<served major.minor>` and
`NBT_BRIDGE_CAPS=<grants>`; legacy applications receive neither variable. Of
the HMI's environment an application gets only NBT-AAD's `PATH` and, when set,
`LD_LIBRARY_PATH`, `LD_PRELOAD`, `GRAPHICS_ROOT` and `TZ`
(`nbt/process/hmi_environment.hpp`): without them the car's libraries and
tools (LayerManagerControl, the DSI client) do not load and the application
cannot reach the panel. An application's stdout and stderr go to its own
log, `state/<app>/output.log`, always (PROTOCOL.md, "Logs"). Today `show` and `status` are implemented
without car-data dependencies; using either is gated by the child's readiness.
When the status channel cannot be created (an unsafe leftover, or
`/dev/shmem` refusing the checks), the host logs `no status channel` with
errno and starts the application without `NBT_STATUS_FD`: a label is not
worth the launch. Adding
car-dependent capabilities must also add runtime availability probing; the
checker already accepts a separate available-capability list.

QNX applications using the C++ SDK compile with
`qcc -Vgcc_ntoarmv7le_gpp -lang-c++` and require the device's
`libstdc++.so.6`. The pre-rewrite source is tagged `c99-final`; existing signed
packages and the C ABI remain compatible.

**Persistence:** `NBTSTATE2` adds the three requirement fields to the installed
record after `sha256`. `NBTJ2` carries corresponding `old_`/`new_` fields in the
transaction journal. The candidate uses its journal declaration during health
checks. Confirmation and confirmed-crash recovery write the declaration with
the new record; unconfirmed recovery restores the executable with the old
record. A separate `state/<app>/bridge` file is deliberately not used: it could
otherwise lag behind the executable after a crash. `NBTSTATE1`/`NBTJ1` remain
valid for no-bridge applications; a successful update may remove a dependency.
The offline bundle tool also preserves signed bridge requirements.

Manager releases may contain up to two `bridge=<major>.<minor>|<capabilities>`
lines after `compat`, ordered by increasing major. The packager defaults to
`bridge=1.0|show,status,live-settings`; repeated `--bridge` overrides are for a publisher describing the
actual worker, and `--no-bridge` describes a legacy provider. Absent lines mean
no bridge. C++ and Rust verifiers enforce the same bounds and syntax.
The publisher must keep the metadata and `nbt_bridge_current()` implementation
in agreement; metadata does not enable capabilities in an executable.

Staging holds the manager operation lock and each installed application's
transaction lock until activation is durable. It refuses busy/interrupted apps,
unreadable records and incompatible normal updates before clearing a slot,
naming the affected app. Explicit older-manager recovery permits a dependency
mismatch and emits a bounded warning on stderr; invalid storage/records still
fail closed. New app provisioning/updates are blocked while manager activation
is pending or on trial, preserving the dependency set checked at staging.
The stable launcher's provisioning code reads the **active signed release's**
provider rather than assuming its own compiled capabilities match the worker.

**Release status:** bridge 1.0 provides `show,status,live-settings`. Dev41's
signed phone API and boot door await in-car validation. Initial setup installs
the current stable launcher and plugin; ordinary slot updates replace the
GUI/worker. The new `--door` launcher mode requires the setup path on an older
installation. No-bridge app packages and records remain valid.

## Principles

- **The plugin stays small** (`AGENTS.md`). It does only what needs the HMI
  process: holding focus, reading car data, its entries. Everything else
  stays in the worker and the GUI.
- **One generic bridge, not per-application code.** An application declares
  what it uses in its signed package; nothing in the plugin or worker names
  an application.
- **Declared, then granted**, against a version (above). Packages without
  bridge lines stay valid and get no bridge.
- **Everything crossing the bridge is untrusted.** Files in `/dev/shmem`
  can be written by any local process with the same user; parse them
  strictly, bound their size, and never let them grant more than the
  declared capabilities. QNX `/dev/shmem` has no `rename()`, so bridge
  files are written in place with a single `write()` of a whole
  line that carries a sequence number, and readers ignore lines that do not
  parse (as the existing `nbt-manager-hmi` signal file does).

## Components

### 1. Bring to front (capability `show`)

`START <app>` on a running application asks it to come to the front instead
of starting a second host:

- The worker finds the host's lifetime write lock on byte 0 of
  `state/<app>/host.lock`. A read lock on byte 1, owned by the same PID,
  advertises `show` only after its child is ready and has that grant.
  The worker rechecks the owner and sends it `SIGUSR1`. The host forwards
  from its main loop only to its own unreaped child, whose PID cannot have
  been recycled, and only while not stopping. The worker-to-host lookup uses
  the same POSIX lock-holder addressing as stop; this is not a portable
  pidfd guarantee against every host-exit/PID-reuse race.
- An application that did not declare it is left alone (`SIGUSR1` would
  terminate it by default). `START` then answers `ERR running`, and the GUI
  says "<app> is already running" instead of closing. A host awaiting
  readiness answers the same way; a failed lookup answers `ERR start`.
- The app installs its `SIGUSR1` handler before readiness. Signal handlers
  only set flags; window operations run in its main loop. Requests may
  coalesce. `OK` acknowledges acceptance, not proven display success.
- Plain `RUNNING` returns names; `RUNNING status` also supplies valid status labels (component 2).

Implemented in `src/process/app_host.cpp`, `src/worker/worker.cpp`,
`src/worker/daemon.cpp`, `src/gui/ui_controller.cpp` and `apps/test/main.cpp`.
The test app reposts its existing Screen buffer and makes the window visible,
resetting its no-input hide timer; a health-check launch ignores show. This uses
the existing
[QNX Screen visibility API](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.screen/topic/screen_8h_1Screen_Property_Types.html).
Actual stacking, display handoff and focus behavior require the car.

On the car, LayerManagerService draws only windows bound to a numbered
surface (`head-unit-interfaces.md`), and nothing may cover iDrive unless
iDrive is held on an inert state (`controller-input.md`). So a user launch
granted `show` gets `NBT_SURFACE=1291`, the one application surface, held by
one host at a time (`PROTOCOL.md`); a second `show` application is refused
with "another app has the screen". The application binds a panel-sized window
to it (Screen id string; the SDK's `ScreenWindow::open(w, h, surface)` does
this) and never places it itself: the manager GUI that started it puts it on
the panel while the plugin keeps iDrive held, and takes it off. An
application cannot hide its window from the panel; the test app ends where it
used to hide. Without `NBT_SURFACE` (host tests, a health check, the surface
taken) it runs undrawn on the car. This is the first half of step 9; an
application that shows itself without the manager (Android Auto when a phone
connects) still needs component 5.
The Windows kit's test packages now require `show`. The simulator returns
`ERR running` for its media app (no grant) and supports showing its test app.
Host tests cover forwarding, grants, readiness, same-PID restarts, whole-file
lock rejection, GUI behavior, Screen restoration and health-check isolation.

### 2. Status line (capability `status`)

The host creates a fresh `/dev/shmem/nbt-bridge1-<app>.status` before each
user launch granted `status`. The application receives `NBT_STATUS_FD=5`;
write through that descriptor, not a caller-selected path. Candidate health
checks receive no status descriptor. The file contains one line
`<sequence> <text>`, the text at most 24 printable ASCII characters. While
it runs, the manager's main menu shows `<app>  (<text>)` instead of
"(running)". The worker reads it on the GUI's periodic `RUNNING status` request. Missing, stale (no
running host) or malformed: plain "(running)".

`nbt_bridge_status_format` formats a bounded line; `nbt_fs_status_write`
writes it at offset zero and truncates to its new length, with no rename or
fsync. Sequence is canonical uint64 decimal (zero allowed), text is 1-24
printable ASCII bytes. The sequence aids producers/readers; it cannot make
in-place writes atomic. Malformed/partial reads fall back and are retried.
The host's byte-3 readiness/grant marker gates reads; byte 2 stays free so it
cannot merge with the show marker. Missing, stopped, unready and ungranted
applications cannot supply a status label, and each new host resets old bytes.

The GUI requests `RUNNING status`, polling the menu once per second. The
worker reads/validates files and live-host markers; rendering remains I/O-free.
Plain `RUNNING` still returns names only. Status is untrusted display text,
never an authorization or health signal. Local apps share a UID and are not
isolated from one another.

Host-testable; target shared-memory behavior still needs validation.

### 3. Settings for larger applications (`live-settings`)

Implemented in the initial bridge **1.0**, alongside `show,status`.

- Applications may declare **12** choice settings, six choices each. The GUI
  parses two-digit counts and scrolls through all settings plus Stop. The
  signed manifest still has a **2048-byte** total limit: long declarations,
  identity fields and bridge lists must fit together. Package tooling rejects
  overflow before signing; older workers reject more than four settings.
- Provisioning creates private `data/<app>` and passes its validated absolute
  path as **`NBT_DATA_DIR`** on user and health-check launches. Offline bundles
  include the directory, signed schema and default snapshot too. Ordinary
  executable updates/recovery preserve app-owned data. Persistent non-choice
  values and advanced options belong there; `settings` is reserved for the
  manager. Existing data at provisioning is a conflict, never overwritten.
- `SET` saves the authoritative `state/<app>/settings` atomically, then writes
  all effective values to **`data/<app>/settings`** (`NBTVALUES1`) using one
  bounded write, truncate and sync, preserving its inode. Provisioning, schema
  confirmation and a live app's launch publish the snapshot too. The existing
  `NBT_SETTING_*` environment remains the startup contract.
- Apps granted **`live-settings`**, required or optional in their signed
  declaration, get **`NBT_SETTINGS_FILE`**. They re-read on demand; the reusable
  `src/sdk/settings_file.cpp` reader opens afresh, validates a bounded private
  regular file and applies allowed choices only after complete parsing. Failed
  reads leave the caller's defaults or last valid values unchanged. The test app
  refreshes its background colour once per heartbeat without restarting.
- The GUI asks `SETTINGS <app> live`, whose reply adds `live` or `restart`
  after the count, based on the installed declaration and provider grants.
  A live app's page says **"Changes apply at once"**. Plain `SETTINGS <app>`
  keeps its existing response. The simulator advertises live settings for test.

Durability and safety: snapshots are not atomic to concurrent readers. A read
may see a partial or mixed write; this is eventual settings delivery, not an
authorization channel. A failed save/snapshot sync returns `ERR settings`; a
late failure may already have saved the durable choice. Retrying `SET` or
restarting repairs the snapshot from authoritative values. An unsafe file or
directory alias is refused; symlinks, hard links, special files, wrong owners,
public snapshot permissions and filesystem changes cannot be write targets.
All manager mutations pass `src/storage/`. Settings writes serialize against app
startup/updates; the same worker rejects writes while holding its update lock
(POSIX locks belong to a process). Schema publication retains that update lock.

### 4. Car data (capabilities `car-data`, `car-data-raw`)

Two levels:

- **Documented fields** (`car-data`), the stable part. The manager reads a
  car signal and publishes a normalised value under a bridge name, one
  file each, `/dev/shmem/nbt-bridge1-car-<name>`, one line
  `<sequence> <value>`. Names, values and units are part of the bridge
  version: `gear` is `park|reverse|neutral|drive|unknown`, not iDrive's
  number. The table below is the candidate list for 1.0; each field is
  added only once its value format is known from the car (see
  "Discovering more car data").
- **Raw values** (`car-data-raw`), for experimenting and discovery: the
  value as the car gives it (iDrive's JSON text converted to ASCII, at most
  200 bytes; MOST payloads as hex), in
  `/dev/shmem/nbt-bridge1-raw-<source>`. No compatibility promise across
  firmwares or bridge versions; the GUI marks an application that requires
  it as experimental.

Known sources, with where the fact comes from:

| Bridge field (proposed) | Car source | Evidence |
| --- | --- | --- |
| `gear` | iDrive car data `driving.gear` | NBT-AAD, car-verified (reverse camera) |
| `park_assist` | `sensors.PDCStatus` (`{"PDCStatus":n}`) | NBT-AAD |
| `idrive_context` | `hmi.graphicalContext` (an iDrive screen came up by itself) | NBT-AAD, car-verified |
| `drive_mode` | `driving.mode`, `driving.mode35up` | NBT-AAD, car-verified on a Supra |
| `dsc` | `driving.DSCActive` | NBT-AAD |
| `night` | `vehicle.dayNightChangeOver` (`{"dayNightChangeOver":n}`) | NBT-AAD |
| `call_line0`, `call_line1` | `communication.callStatus0` / `1` (`{"callStatus":{"id":n,"state":n}}`) | NBT-AAD, car-verified |
| (unknown) | `specialvehicle.menucontrol` | used by BMW's SecurityMenu plugin; meaning unknown |
| `volume` | MOST AudioMaster FBlock 0x20, fkt 0x0C08 (master volume; `VisibleVolumeIndicator`, while `VolumeActual` is 0x200) | NBT-AAD; names from HmiMain3D |
| `position` | MOST FBlock 0x52 inst 00, fkt 0x440 (navigation GNSS fix, about 1/s; not among HmiMain3D's Navigation functions) | NBT-AAD, car-verified |
| `gnss_status` | MOST FBlock 0xCD inst 01, fkt 0xC00 (receiver status; not among HmiMain3D's NavigationInfo functions) | NBT-AAD |
| `gnss_extended` | iDrive car data `navigation.GPSExtendedInfo`, from MOST 0xCD inst 01 fkt 0x203 (`GPSPositionExtension`) | NBT-AAD (from `libhmiremoting.so`) |
| `position_matched` | MOST 0x52 fkt 0x243 and 0xCD inst 01 fkt 0x20B (`PositionHeadingSpeed`; map-matched position) | NBT-AAD, seen in captures |
| `gear` (MOST alternative) | MOST 0x05/00 fkt 0x228 (`GearBox`): raw 1 neutral, 2 reverse, 3 park, 4-14 drive; 0/15 unknown. 0x22C `ReverseGear` is a separate one-byte status, unexplored | AAD `5bbdc30`; host-tested, not car-verified |
| `parking_brake` (proposed) | MOST 0x05/00 fkt 0xCB5 (`HandBrake`): raw 1 released, 2 on; other values unknown. Also 0x409 `ParkingBrake2` | AAD `5bbdc30`; firmware may fall back to 0xD01 (`StateStandstillFunctions`), which AAD does not read; not car-verified |
| `speed` | MOST 0x05/00 fkt 0x621 (`CarSpeed2`): proposed big-endian uint16, 1/64 km/h, 0xFFFF invalid; direction via 0x626 (`DrivingCondition2`; 0 stopped, 1 forward, 2 reverse, 3 unclear) | AAD `src/car/vehicle.hpp` at `0a314d4`; scale fitted to GPS, host-tested, not car-verified |
| `camera` (proposed) | MOST 0xE2: 0x675 `RearViewStatus`, 0x6A1 `TopViewStatus`, 0x691 `SideViewStatus` (instances per camera); HMIControl 0xCF 0x200 `GraphicalContext` | names and ids from HmiMain3D (`docs/most-functions.tsv`); formats unknown |
| `panel`, `make` | `/var/opt/conn/carplay/carplay.ini` (panel size, CarPlay logo and manufacturer) | NBT-AAD |

- iDrive car data needs the plugin (only the HMI process can subscribe).
  MOST data needs a MOST client: `most` runs one per application host
  today ([most-bridge.md](most-bridge.md)); applications can read raw MOST
  fields that way before they become documented `car-data` fields. One
  shared client for every application is a later step.
- For application bridge consumers, the plugin subscribes only while an installed application uses a field
  that needs it. Deciding to step aside for the camera stays the
  application's job.
- Applications poll the files (NBT-AAD's daemon has a 250 ms tick) or are
  woken by `SIGUSR2` from their host when `/dev/shmem/nbt-bridge1-car-sequence`
  changes. Polling is enough to begin with.
- Position and call status are personal data visible to any local process
  with the same user. That matches the existing trust model; say so in the
  README, and publish them only while an application uses them.

Parsers and writers are host-testable. The subscription calls are the same
framework calls NBT-AAD uses; they need the car.

### Findings from NBT-AAD

- **Camera views.** A camera opened by its button changes neither the reverse
  gear nor `PDCStatus`, and its full-screen view can report
  `GFXIDENTIFIER_INVALID`. Re-holding focus then covers the camera within about
  two seconds. Contexts 1023 (F-series NBTevo_Y24073J) and 692 (Supra Top View)
  are camera views: step aside until another context appears and do not re-hold
  meanwhile. These ids are firmware-specific, not a universal catalogue. For
  components 4 and 5, gear and PDC alone are not enough: use the graphical
  context too, and keep a camera's step-aside distinct from going to iDrive's
  main menu.
- **MOST gear and parking-brake sensors** (mappings in the table above) are
  change-driven: assume no value before the first report (gear can fall back to
  iDrive car data until MOST supplies it), and a quiet stream does not make a
  value stale. Capture fixtures before making any of them stable bridge fields.
- **Media metadata** in `/dev/shmem/nbt-aad-media` (updated in place up to 5 Hz)
  and loopback control `media next|prev|playpause|play|pause` are NBT-AAD's own
  interface, not a bridge capability.
- **Steering-wheel media** arrives on MOST 0x15/02; the roller mappings
  0x511:2/0x511:1 are inferred, not confirmed, and kept separate from the
  controller mapping.
- **`SIGCHLD`.** The HMI can pass on an ignored `SIGCHLD`; the launcher, GUI and
  worker restore it to the default before spawning, and host tests start them
  with it ignored.
- **`/dev/shmem`** has no `rename()` (status files are rewritten in place), and
  `fsync()` there fails with `Function not implemented`: ephemeral bridge writes
  use neither; durable records stay on the Power-Safe file system.

### 5. Focus hold for applications (capability `hold`)

The plugin's hold (`src/gui/hmi_hold.cpp`) becomes a hold on behalf of whatever
has the screen, the manager or one application:

- The application writes `/dev/shmem/nbt-bridge1-hmi`, one line
  `<sequence> <app> hold|release|release-menu`. The plugin polls it (it
  already polls every 250 ms) and accepts it only from an application that
  declared `hold` and whose host currently holds its lock.
- `hold` fires focus event 50 (the blank state). `release` returns iDrive to
  the entry's list; `release-menu` to iDrive's main menu (NBT-AAD's
  "leaving the menu" and "MENU key opens iDrive").
- BACK belongs to the application: the plugin re-holds, as for the manager.
  Other exits (MENU, NAV, MEDIA, a warning, a camera) are iDrive's: the
  plugin stops holding and writes `/dev/shmem/nbt-bridge1-<app>.hmi`,
  `<sequence> aside`, which the application reads, as the manager's GUI
  reads `aside` today. Phone calls: NBT-AAD re-holds after a call screen;
  the rule needs `communication.callStatus*` from component 4.
- The manager's own hold keeps priority: opening the manager while an
  application holds hands the hold to the manager, and the application is
  told `aside`.
- A wrongly written file can at worst hold iDrive's focus until the next
  exit key; MENU still frees iDrive.

Decision logic host-testable (`tests/test_hmi_hold.cpp`); behaviour needs the
car. Whether two PluginApps can each hold a state stops mattering once
NBT-AAD's plugin is retired, but matters during migration.

### 6. Start automatically

**Partial: worker core implemented; boot and GUI integration paused.**
`src/lifecycle/autostart.cpp` stores the preference and pessimistic attempt count
using the central path policy; detached hosts clear it only after the existing
30-second health check. Private IPC has `AUTOSTART <app> [off|on]`; app setting
keys and all twelve signed declarations remain independent. `--autostart` on the
worker is a one-shot dispatcher that skips existing hosts without showing them.
Manual START re-arms before attempting start/show. Malformed records fail
closed; unsafe paths cannot be repaired by overwriting their targets.
See PROTOCOL.md for exact records and interruption semantics.

Still to implement: manager-provided GUI choice and guard reason, stable launcher
selection of a verified known-good worker (including pending manager updates),
and the plugin's one-shot delayed invocation below. The current plugin and
launcher do not invoke autostart. Host tests exercise the actual 30-second
window, repeated failures, killed hosts, path aliases and record boundaries;
hardware boot timing and filesystem durability remain unverified.

Every application's settings page gets a manager-provided last setting,
"Start with the car" (off/on), stored by the worker in
`state/<app>/autostart`. When the plugin is loaded at boot it waits 15 s,
then runs the launcher with `--autostart`, which starts each application
that has it on, as `START` does, without opening the GUI.

Boot guard: the launcher counts autostarts that did not reach 10 s of
healthy running (`state/<app>/autostart-guard`). After three in a row it
skips the autostart until the application is started by hand once, and
the settings page says so. Host-testable apart from the boot timing.

### 7. Favourite entry

iDrive only stores plugin entries with a fixed UUID on the numbered keys,
and entries come from the plugin's XML at boot, so they cannot follow the
installed applications. Add one fixed second entry, "Favourite app", whose
target is chosen in the manager's Settings (one choice per installed
application). Selecting it runs the launcher with `--start <app>`: start, or
bring to front (component 1), without the GUI. Its text is static (texts
come from the plugin's text database), so it cannot show the application's
name. Needs the car.

### 8. Controller input (capability `controller`, bridge 1.4)

The manager decodes the iDrive controller and the favourite keys once, in the
application's host, and hands the application the result: it never parses
MOST, needs no signed policy and sees nothing else of the bus.

- **Channel.** `NBT_CONTROLLER_FD=8`, a non-blocking Unix datagram socket,
  user launches only. One 16-byte little-endian state per datagram
  (`include/nbt/controller_wire.h`): sequence, keys held, rotation (detents
  clockwise since the feed started, wrapping) and whether the manager's MOST
  link is up.
- **Keys.** `PUSH`, `MENU`, `BACK`, `OPTION`, `COM`, `MEDIA`, `NAV`, `MAP`,
  the four tilts (a diagonal holds two) and `FAVOURITE(0..7)`. Hard key slots
  follow BMW's order (0 MENU, 1 BACK, 2 OPTION, 3 COM, 4 MEDIA, 5 NAV, 6 MAP;
  NBT-AAD's car-tested decoder); the older CCS controller (0xC10/0xC20) is
  decoded too. The favourite keys are FBlock 0x15 instance 0x0C function
  0xC02, one state byte per key (HmiMain3D's `CPxyCECenterStack`,
  [input-capture.md](input-capture.md)); the volume button next to them
  (0xC01) and the master volume (AudioMaster 0x20) are not part of the feed.
- **Reader.** `nbt::controller::Input` (`include/nbt/controller/input.hpp`,
  `src/sdk/controller_input.cpp`, portable C++11, no allocation): `poll(fd,
  deliver, context)` delivers each change as the state and an `Event` with
  the keys pressed and released and the detents turned. Keys count only once
  every key was seen up, so the push that started or resumed the application
  is never a press.
- **Who gets it.** Only an application that has a surface and is not paused
  (`HostChannels::spawned`, `pause`). Going quiet (paused, hidden) first sends
  a released, disconnected state, so nothing stays held. The host's one MOST
  client registers the controller instances and 0x15.0C as device
  controllers; an application that also has `most` keeps its own policy for
  the raw channel.
- **iDrive.** While the manager or the application is on screen, the active
  capture shim keeps these keys from iDrive (presses and releases alike).

`NBTFoundry/doom` reads it through the SDK's reader (`src/input_idrive.cpp`).
Host tests: `tests/test_controller_feed.cpp` (decoder, wire, reader) and
`test_controller_states_reach_a_shown_application` in `tests/test_app_host.py`.
The favourite keys reach the manager when the MOST server delivers 0x15.0C.C02 to
a client registered for it, as it does to iDrive's own proxy; the debug log shows
the messages received.

## Discovering more car data and interfaces

The known list is short because the library holding iDrive's car-data
property table (`libhmiremoting.so`) and the MOST function catalogues are
only on the head unit. Discovery finds what exists, then what it means; a
field enters the bridge only after both. A firmware dump (NBTevo_Y21432J) has
given the "what exists" part: the car-data names and JSON formats, the MOST
FBlock and function names and the DSYSMOSTServer interface are in
[head-unit-interfaces.md](head-unit-interfaces.md). Meanings, units and MOST
function ids still need the steps below.

### Step 1: inventory (read-only, the laptop does the work)

Copy files from the head unit to the laptop and study them there; nothing is
written on the head unit. [car-inventory.md](car-inventory.md) describes
`tools/car_inventory.sh`, run from a USB stick, which copies everything below
to the stick. It can also be done by hand over SSH (the image has `cat`, so
`ssh <unit> cat <path> > <file>` works where SCP does not); a desktop
installer's **Collect everything** gathers the part that changes from run to
run. The locations below are those of NBTevo_Y21432J; "check" marks an inferred
one.

First, listings (small; they tell where the rest is):

- `ls -lR /opt/hmi /opt/sys/etc /opt/sys/lib /opt/car /fs/sda0/opt/car /pps /dev/shmem`
- `ls -l /opt/sys/bin /opt/core/bin /opt/os/bin /opt/conn/bin /opt/mm/bin /dev`
- `pidin arg` (every running process with its arguments) and `sloginfo`
  (the system log; may contain personal data, review before sharing)

Then, by value:

| File | Why | Evidence |
| --- | --- | --- |
| `/opt/hmi/ID5/data/ro/hmiplugins/libhmiremoting.so` | iDrive's car-data catalogue: every property name, the MOST FBlock/function or CAN signal behind it (`CCdsProperty*::onStatus`) and the JSON format it publishes | NBT-AAD found all its names and formats here |
| `/opt/sys/etc/nbt_version.txt` | firmware version, to tag every finding | NBT-AAD reads it |
| `/opt/sys/etc/starter.cfg` | every service the head unit starts, with arguments: a map of the interfaces | NBT-AAD names it |
| `/opt/sys/lib/libimp_sys_most_base_nbt.cpp-ne.so.17` | MOST message types and the FBlock/function tables | NBT-AAD's MOST client is based on it |
| `/opt/sys/lib/libsys_dsi_colibry.gpp.so.8` | the DSI library our C++ MOST client loads | used by `src/most/dsi_client.cpp` |
| `/opt/hmi/ID5/lib/libhmiplugin.so`, `libhmicom.so` | the plugin framework and its car-data and focus calls, for our plugin's bridge side | NBT-AAD's plugin ABI notes |
| `/opt/hmi/ID5/bin/HmiMain3D` (runs as `/opt/hmi/bin/zipped/HmiMain3D`, a `ziploader` path) | iDrive itself: states, contexts, which car data it uses | NBT-AAD reversed parts of it |
| `/opt/hmi/ID5/data/ro/Plugins/*/` and `PluginsCoded/*/` (manifest, XML, library such as `libSecurityMenu.so`; never the `.p12`/`.p7b` files), `libhmia4a.so` | BMW's own plugins: entries, states and the car data they bind (`specialvehicle.menucontrol`) | NBT-AAD's PluginApp is modelled on them |
| `/opt/nav/eceusa/bin/NavNBTEvoController` (runs as `./NavNBTEvoController`) | sender of the GNSS messages, with field names in its log strings | NBT-AAD's GPS notes |
| `/fs/sda0/opt/car/bin/NBTCarHU` | the head unit's car service: likely where vehicle signals enter | known path |
| `/var/opt/conn/carplay/carplay.ini` | panel size and car make | NBT-AAD reads it |
| `/opt/car/bin` contents (check) | name suggests car services | listing first |

Leave alone: `/var/opt/conn/WFDPass.conf`, `WFDWps.conf`, `dhcpd.*.leases`
and `/var/opt/sys/persistence` (Wi-Fi credentials and personal data), phone
book and navigation history databases.

A host script (planned) then lists from the copies:

- car-data property names (dotted names in the library's strings and the
  XML), with the JSON format strings found next to them, which give the
  value's shape (NBT-AAD found `{"dayNightChangeOver":%u}` this way);
- MOST FBlock and function names and numbers;
- other interfaces worth a look: PPS objects (`/pps` exists:
  NBT-AAD uses `/pps/services/multimedia/renderer`; on the first car run it
  held only iAP2 and renderer objects, no car data), `/dev/shmem` objects of
  BMW services, command-line tools such as `audioconn`.

The copied files are BMW's and the car owner's: they stay on the laptop, never in
this repository (as the framework libraries today), and are used for
interoperability only.

### Step 2: observation in the car (planned)

With the debug log on, a manager setting "Car data discovery" (off by
default, reset at every boot) makes the bridge log what it sees:

- iDrive car data: the plugin subscribes to the names from the inventory,
  given as `manager/discover.list` (written by the installer, at most 64
  names, each checked against the dotted-name pattern), and logs each value
  change, `cds <name> = <value>`, at most one line per name per second.
  Subscribing to a name the HMI does not know is the risk to check first:
  begin with a handful of names and grow the list.
- MOST: the manager's observe-only client registers for a given list of
  FBlocks and logs `most <fblock>.<inst> <fkt> <op> <length> <first bytes>`,
  rate-limited. It only listens; it sends nothing but its registration.

The tester then follows a script in `docs/in-car-procedure.md`, parked:
ignition and engine, lights, reverse and park assist, doors, drive modes and
DSC, a phone call, a navigation route, volume. The saved log shows which
names and messages change with which action.

Discovery is observation for interoperability: local to one car, off by
default and recorded only in the debug log. The logs can contain position and
call details; the procedure says so, and they are shared only after review.

### Step 3: promotion

A field is added to the bridge, as a minor version, once its meaning, value
format, units and the firmwares it was seen on are written down in this
document, with a test fixture from a real log. Until then an application
can only see it as a raw value (`car-data-raw`).

## Adopting the bridge: NBT-AAD

NBT-AAD's own plugin launches `/opt/aa/nbt-aad`, reads `/opt/aa/nbt-aad.env`,
and recognizes persistent options under `/var/opt/aa` (some with manual flags
under `/opt/aa`); its firewall helper changes the OEM firewall configuration.
Ordinary manager updates touch none of these. Moving it under the manager takes
changes in NBT-AAD's code, for its author to make or agree to (PolyForm
Noncommercial): a rebuilt release kept in NBT-AAD's repository, packaged and
signed for the manager.

- Lifecycle: `--ready-fd`/`--lease-fd`, readiness and heartbeats, the
  managed executable and data paths.
- `NBT_LAUNCH=check`: during an update's 30-second health check, start but
  do not take the screen or the controller (the phone may connect meanwhile).
- Use only the bridge for the car: declare `bridge=1.1` with
  `bridge-uses=show,status,most,car-data,hold`,
  `bridge-optional=live-settings` and its MOST policy
  ([most-bridge.md](most-bridge.md)); handle `SIGUSR1` as its current `show`;
  write the status file instead of its state file; read MOST through
  `NBT_MOST_FD` instead of its own DSI client and the documented car
  fields instead of its plugin's car data; request and release the hold.
- Its 7 checkboxes become declared settings. `nbt-aad.env` options and its
  persistent values move to `NBT_DATA_DIR`. Autostart moves to component 6.
- Its plugin is retired by an explicit setup step, after the rebuilt
  release runs healthy (the legacy plugin must not launch the managed
  executable). No compatibility symlinks are used.
- A migration reads only recognized legacy sources, each with a fixed allowlist,
  regular-file and ownership checks and size limits: `/opt/aa/nbt-aad.env`
  through a documented allowlist of fields; the presence flags `autostart`,
  `usblog`, `menu-idrive`, `show-drivemode`, `kombi-nav`, `show-warnings` and
  `usb-aa` under `/var/opt/aa` and their manual flags under `/opt/aa`; the
  values `/var/opt/aa/touch-peer` and `/var/opt/aa/spp-model`. The boot guard is
  operational state and needs its own decision. The legacy files and recovery
  material stay until adoption succeeds.

## Design notes

- Shared-memory names are flat (`nbt-bridge1-...`), without subdirectories in
  `/dev/shmem`.
- Car-data names are checked with the framework's `cdsIsPropertyAvailable(name)`
  before subscribing ([head-unit-interfaces.md](head-unit-interfaces.md)).
- Files in `/dev/shmem` are rewritten in place with one `write()` of a short
  line; the sequence number lets a reader retry a torn read.
- The focus hold does not cover touch: iDrive still reacts to touches while
  another surface is shown.
- Hosts forward requests as `SIGUSR1`/`SIGUSR2`; a control pipe from the host
  would replace them if requests multiply.


## The manager's own car data

The manager itself reads two car-data names through its plugin,
`driving.speedActual` and `hmi.graphicalContext`, with change subscriptions plus
one-second asynchronous reads and bounded UTF-16 parsing, for the phone's wake
gate ([phone-access.md](phone-access.md)). This is not the `car-data` capability:
applications get no car data yet.
