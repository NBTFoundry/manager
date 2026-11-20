<!-- SPDX-License-Identifier: MPL-2.0 -->
# Bridge extensions: designs

Designs for capabilities beyond bridge 1.4 ([app-bridge.md](app-bridge.md)):
car data, a focus hold requested by the application, automatic start and a
favourite entry, and how car data is found and promoted into the bridge. A
design becomes part of the bridge as a new minor version once it is built and
checked on the car; an application cannot declare it before then.

## What a full-screen application needs

Android Auto (NBT-AAD) takes over the screen for long and needs more from iDrive
than a launch; its own iDrive plugin provides it. The table compares that plugin
with bridge 1.4. The facts come from NBT-AAD's published README and sources;
NBT-AAD is PolyForm Noncommercial and no code is taken from it.

| Role | How NBT-AAD does it | Bridge 1.4 |
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

## Car data (capabilities `car-data`, `car-data-raw`)

Two levels:

- **Documented fields** (`car-data`), the stable part. The manager reads a
  car signal and publishes a normalised value under a bridge name, one
  file each, `/dev/shmem/nbt-bridge1-car-<name>`, one line
  `<sequence> <value>`. Names, values and units are part of the bridge
  version: `gear` is `park|reverse|neutral|drive|unknown`, not iDrive's
  number. The table below is the candidate list; each field is
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
  ([most-bridge.md](most-bridge.md)); applications can read raw MOST
  fields that way before they become documented `car-data` fields. A
  shared client for every application would replace them.
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

### Car facts from NBT-AAD

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

## Focus hold for applications (capability `hold`)

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
  reads `aside`. Phone calls: NBT-AAD re-holds after a call screen;
  the rule needs `communication.callStatus*` from component 4.
- The manager's own hold keeps priority: opening the manager while an
  application holds hands the hold to the manager, and the application is
  told `aside`.
- A wrongly written file can at worst hold iDrive's focus until the next
  exit key; MENU still frees iDrive.

Decision logic host-testable (`tests/test_hmi_hold.cpp`); behaviour needs the
car. Whether two PluginApps can each hold a state stops mattering once
NBT-AAD's plugin is retired, but matters during migration.

## Start automatically

The worker side exists: `src/lifecycle/autostart.cpp` stores the preference and pessimistic attempt count
using the central path policy; detached hosts clear it only after the existing
30-second health check. Private IPC has `AUTOSTART <app> [off|on]`; app setting
keys and all twelve signed declarations remain independent. `--autostart` on the
worker is a one-shot dispatcher that skips existing hosts without showing them.
Manual START re-arms before attempting start/show. Malformed records fail
closed; unsafe paths cannot be repaired by overwriting their targets.
See PROTOCOL.md for exact records and interruption semantics.

Built on that: every application's settings page has a manager-provided
"Start with the car" (off/on) after its own settings, stored by the worker in
`state/<app>/autostart`. Once per boot, 25 s after iDrive loads the plugin,
the plugin runs the launcher with `--autostart`, which runs the verified
known-good worker's dispatch under the manager operation lock: each
application that has it on starts as `START` does, without the GUI
(PROTOCOL.md, "Autostart"). Host tests cover the GUI row, the settings flags,
the launcher's slot choice and lock, and the once-per-boot record; the boot
timing needs the car.

Boot guard: the host counts autostarts that did not reach 10 s of healthy
running (`state/<app>/autostart-guard`). After three in a row automatic
starts pause until the application is started by hand once, and the settings
page says so.

## Favourite entry

iDrive only stores plugin entries with a fixed UUID on the numbered keys,
and entries come from the plugin's XML at boot, so they cannot follow the
installed applications. Add one fixed second entry, "Favourite app", whose
target is chosen in the manager's Settings (one choice per installed
application). Selecting it runs the launcher with `--start <app>`: start, or
bring to front (component 1), without the GUI. Its text is static (texts
come from the plugin's text database), so it cannot show the application's
name. Needs the car.

## Discovering more car data and interfaces

The known list is short because the library holding iDrive's car-data
property table (`libhmiremoting.so`) and the MOST function catalogues are
only on the head unit. Discovery finds what exists, then what it means; a
field enters the bridge only after both. A firmware dump (NBTevo_Y21432J) has
given the "what exists" part: the car-data names and JSON formats, the MOST
FBlock and function names and the DSYSMOSTServer interface are in
[head-unit-interfaces.md](head-unit-interfaces.md). Meanings, units and MOST
function ids come from the steps below.

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

A host script then lists from the copies:

- car-data property names (dotted names in the library's strings and the
  XML), with the JSON format strings found next to them, which give the
  value's shape (NBT-AAD found `{"dayNightChangeOver":%u}` this way);
- MOST FBlock and function names and numbers;
- other interfaces worth a look: PPS objects (`/pps` exists:
  NBT-AAD uses `/pps/services/multimedia/renderer`; on NBTevo_Y21432J it
  holds only iAP2 and renderer objects, no car data), `/dev/shmem` objects of
  BMW services, command-line tools such as `audioconn`.

The copied files are BMW's and the car owner's: they stay on the laptop, never in
this repository (as the framework libraries), and are used for
interoperability only.

### Step 2: observation in the car

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
Ordinary manager updates touch none of these.

`NBTFoundry/nf-android-auto` packages it for the manager without changing it
(PolyForm Noncommercial; no NBT-AAD code enters the manager): NBT-AAD's daemon
is pinned as a submodule and appended to an SDK adapter, which the manager
starts like any application and which drives the daemon through its control
port and records. NBT-AAD's iDrive plugin is retired; what it did becomes
generic bridge capabilities, none of which names Android Auto. The few changes
the daemon needs (its folders from `NBT_DATA_DIR`, ...) go to NBT-AAD as pull
requests. The adapter's view of the daemon is in that repository's
`docs/upstream-interface.md`; the list below is what the bridge still needs.

- Done in the adapter, on bridge 1.0: lifecycle and heartbeats, the health
  check (`nbt-aad --license`, nothing else touched), `show`, the status label,
  twelve declared settings given to the daemon as its `NBT_*` variables and
  flag files in `NBT_DATA_DIR`. The daemon keeps its own MOST client.
- Bridge 1.6, all generic: car data as a signed allowlist of iDrive car-data
  names the plugin relays as the car gives them (`bridge-cds`), so a new
  signal NBT-AAD wants needs no manager release; the focus hold for an
  application that places its own surface (`hold`, with `aside` back); a
  signed firewall rule the host adds and removes (`bridge-net`), replacing
  the daemon's `pfctl -d` and `src/worker/android_auto_wifi.cpp`'s reading of
  `/opt/aa`. Autostart and the favourite entry as designed above.
- Its plugin is retired by an explicit setup step, after the managed
  package runs healthy (the legacy plugin must not launch the managed
  executable). No compatibility symlinks are used.
- A migration reads only recognized legacy sources, each with a fixed allowlist,
  regular-file and ownership checks and size limits: `/opt/aa/nbt-aad.env`
  through a documented allowlist of fields; the presence flags `autostart`,
  `usblog`, `menu-idrive`, `show-drivemode`, `kombi-nav`, `show-warnings` and
  `usb-aa` under `/var/opt/aa` and their manual flags under `/opt/aa`; the
  values `/var/opt/aa/touch-peer` and `/var/opt/aa/spp-model`. The boot guard is
  operational state and needs its own decision. The legacy files and recovery
  material stay until adoption succeeds.
