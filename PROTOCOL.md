<!-- SPDX-License-Identifier: MPL-2.0 -->
# Local control and application contract

The worker receives a connected private stream descriptor from the launcher, which starts it before the GUI and gives the GUI the other end (`--worker-fd 5`). The manager GUI must ensure that a local user action is the only source of approval; the GUI controller produces approval only from controller input (see docs/controller-input.md). HTTP clients have no access to this descriptor. Lines are ASCII and terminated by a newline. Responses are one line, with fields separated by tabs. A connection must first send `HELLO 1`; it receives `HELLO\t1`. Unknown protocol versions cannot issue commands. The GUI's client performs this handshake when it takes over the channel.

Commands in protocol 1:

| Command | Purpose |
| --- | --- |
| `LIST` | List provisioned application IDs: `APPS`, a count, then the names. A name followed by `\|update` is an application this manager does not run until it is updated ("Disabled until updated"). |
| `SESSION <app> <interface> <port>` | Open a local install waiting session. Production uses `wfd0:8080`, coordinating MOST `0xA0.03` (`WLANState3` / `SelectWLANState3` / `WLANConfiguration`) and verifying `wfd0` link/IP readiness before replying (`ERR network-unavailable` on failure, leaving CarPlay `0xA0.01` / `uap0` untouched and restoring `0xA0.03` on `CANCEL`, expiry or exit only when this session enabled it); tests also permit `lo` and an ephemeral port. Reply: `SESSION\t<IPv4:port>`, then `\t<ssid>\t<passphrase>` when available. No HTTP credential is issued. Clear the response after use; show the passphrase only inside the Wi-Fi QR. |
| `SESSION <interface> <port>` | Generic waiting session: the verified package names the application. A paired phone can also upload while the manager is on its main menu. |
| `STATUS` | Return manager state, selected app, installed and proposed versions, proposed sequence, package digest and status text. |
| `AUTOSTART <app>` | Manager-owned boot preference and guard: `AUTOSTART\t<off|on>\t<ready|blocked>`. Missing records default to off/ready; malformed or unsafe records return `ERR settings`. |
| `AUTOSTART <app> <off|on>` | Persist the boot preference; replies `OK` or `ERR settings`. Toggling does not reset the guard. Separate from signed app settings and their keys. |
| `START <app>` | Explicitly re-arm the boot guard, then start a provisioned application in its own detached host; it keeps running after the worker and the GUI exit. Returns `ERR quarantined` if `state/<app>/fault-guard` has reached 3 faults (or is malformed/unreadable), `ERR incompatible` if this manager does not run it ("Disabled until updated"). A ready running app granted `show` receives a bring-to-front request; otherwise `ERR running` keeps the GUI open. `OK screen` when the app is granted `show` and its host has the application surface (the GUI hands it the panel), `ERR screen` when another app's host has that surface, else `OK`. |
| `STOP [<app>] [fault]` | Ask the application's host to stop it (without an argument: the last selected application); replies `OK` before it has exited. Passing `fault` increments `state/<app>/fault-guard` under the application's transaction lock (used when the GUI aborts a hung or non-appearing surface). |
| `UNBLOCK <app>` | Clear `state/<app>/fault-guard` after three consecutive application faults; replies `OK` or `ERR unblock`. |
| `REMOVE <app>` | Uninstall a stopped application, as confirmed in the GUI: its profile (it leaves `LIST` at once), keys, executable, state and data, all within the managed root (`src/lifecycle/app_remover.cpp`, `src/storage/app_removal.cpp`). Replies `OK`; `ERR running` while its host, lease or lock shows it running; `ERR busy` during a session, an installation, a stop or a manager update; `ERR missing` when nothing of it is left; `ERR remove` when a removal fails part-way (run it again: it completes). Never `manager`. Links in its data are removed, never followed. |
| `RUNNING [status]` | List the provisioned applications whose host runs: `RUNNING`, a count, then the names. With `status`, each name may be followed by `\|<text>` for a valid ready/granted status label. Split only at the first pipe; text can contain pipes. Missing status leaves a plain name. Maximum response is 2048 bytes including newline and terminator. |
| `SETTINGS <app>` | The settings the application's installed package declares, with their current values: `SETTINGS`, a count (0-12), then one `key\|label\|value\|choice,choice` field each. `ERR settings` when the stored declaration is unreadable. The reserved name `manager` has the manager's own settings (below). |
| `SET <app> <key> <value>` | Choose one of a declared setting's values; startup-only applications read it when they next start; live-settings apps can re-read the snapshot. `ERR settings` for an undeclared key or a value that is not one of its choices. |
| `APPROVE <package-sha256> normal` | Approve the exact verified package, subject to the release counter. Replies `OK installing` once accepted. |
| `APPROVE <package-sha256> recovery` | Approve an older signed release. |
| `CANCEL` | Close the session, abort an upload in progress and discard its staged upload. |
| `QUIT` | Stop the worker. |
| `SETUP` | Bring the stable launcher and the iDrive plugin to the active, confirmed manager release (see "The manager's own installation"). It runs on the worker's job: the reply comes when it ends, and later commands wait for it. `SETUP\tcurrent`, `SETUP\tlauncher`, `SETUP\tplugin` or `SETUP\tboth` (what was replaced); `SETUP\ttrial` while an update is on trial and `SETUP\tbusy` during an application update, a verification or a copy (ask again later); `ERR setup`, with the reason in the debug log. |
| `IDRIVE RESTART` | Restart iDrive (`OnOffDSICommander appreset`), which restarts this manager with it. Sent only when the driver chose Restart iDrive on the car. `OK` once started, `ERR restart`. |

Private phone commands (after `HELLO 1`):

| Command | Reply / effect |
| --- | --- |
| `PHONE OPEN` | `OK`; pairing permitted for 60 seconds. Only Pair a phone emits this. |
| `PHONE CLOSE` | `OK`; close pairing and reject any pending pairing/removal. `CANCEL` also does this. |
| `PHONE STATUS` | `PHONE\tnone`, or `PHONE\t<pair\|remove>\t<action-id>\t<phone-name>\t<digits\|app>`. |
| `PHONE YES <action-id>` | Confirm the immutable pending action; `OK paired`, `OK removed`, or error. |
| `PHONE NO <action-id>` | Reject that action; `OK cancelled`, or error. |
| `PHONE LIST` | `PHONES\t<count>`, then `\t<key-id>\|<name>` for each (0-4). |
| `PHONE FORGET <key-id>` | Remove the phone's trust record; its next signed request fails. |

Action IDs are fresh random 16-byte lowercase hex strings. They expire with the
pending action and are never exposed over HTTP. A stale confirmation cannot
approve a replacement request from the same phone.

`STATUS` replies with `STATUS`, then tab-separated state, application, installed
version, proposed version, proposed sequence, package SHA-256 and message.
Absent text is `-`; the absent sequence is `0`. States are `0` idle, `1`
waiting, `2` verified, `3` installing, `4` installed and `5` failed. Proposed
version, sequence and digest exist only in states 2 and 3. Sessions are local
transaction deadlines (ten minutes), and carry no bearer credential.

The signed HTTP listener remains available for the worker's lifetime. Closing
the GUI closes that worker and listener. The independent door remains available
on 8081. The phone follows a result on the worker for five minutes after its
request; polling extends that period while installation is running.

In a generic session the upload is staged in `state/manager/session.upload`. When it is complete, the worker reads the application id from the manifest (without trusting it yet; never `manager`). For an installed application (it has a profile) it selects and recovers it as for an application-bound session, moves the upload to that application's staging path and verifies it against the apps key bundled with the manager and the application's profile. For an application not installed here, it verifies the staged upload against the apps key and the package's own compatibility id and keeps it staged: `STATUS` reports the application with installed version `-`, and the GUI reviews it as a new application. `APPROVE` answers `OK installing` and then installs it from the staged upload on the worker's job (state `3`; `nbt_app_install_new`: executable, record, settings declaration, then the profile with the package's compatibility id), removing first anything an interrupted install or uninstall left of it; the outcome is state `4` "Application installed" or `5`. There is no health check for a first install: nothing older exists to restore. Otherwise the session fails with `Package rejected: not an NBT Manager package`, `Package rejected: not an application package`, a verification reason or the selection's reason, and the staging file is removed. A package never selects a destination or a key. A generic session starts with no application selected.

`APPROVE` is meaningful only after a session has verified a package, and is bound to that package's SHA-256. A client must not expose this command to HTTP. The worker recovers an application before selecting it for a session or starting it; startup does not currently scan and recover all provisioned applications.

Running applications outlive the worker. `START` launches the worker binary as `--host <app>`, detached from the worker (double fork, own session). The host takes an exclusive lock on `state/<app>/host.lock` for its lifetime (a second host for the same application exits with status 3), recovers an interrupted transaction under the application's transaction lock, starts the application with the contract below and watches its heartbeats. It ends when the application exits or breaks the contract; on `SIGTERM` it first stops the application (`SIGTERM`, then `SIGKILL` after five seconds). "Running" means the host lock is held, and the worker signals only the pid the kernel reports as its holder. If a host is killed, its application sees the heartbeat pipe close and exits.

Commands do not wait for an application to exit, so the control channel stays responsive. `STOP` sends `SIGTERM` to the host; the worker's loop sends `SIGKILL` to a host still running after eight seconds and reports failure after fifteen. `STATUS` reports `Stopping application` and then `Application stopped`. Installing over a running application stops its host first, checks the candidate's health as the worker's own child, then starts a new host for it; a failed candidate is restored and the previous version restarted in a host. After `OK installing`, the state is `3` (installing) while the worker stops a running application, installs the package, starts the candidate and checks its health. A failed candidate start or health check also stays in state `3` while the previous executable is restored. The outcome is state `4` or `5` with a message. `APPROVE` returns `ERR install` only when it is not accepted: wrong digest, no verified package, an expired session, or while the worker is still stopping an application that failed its lifecycle contract. Approval during a requested `STOP` is accepted, and the application stays stopped after installation. `START`, `STOP` and new sessions are refused while an installation is in progress. Worker shutdown still stops a child synchronously.

## The manager's own installation

First setup writes the stable launcher (`manager/nbt-manager-launcher`) and the
iDrive plugin (`Plugins/NbtManager`) once; a manager update stages only the GUI
and worker in a slot. Once its list shows, the GUI sends `SETUP`, and again 12 s
later while the update is on trial. The worker compares both, file by file by
SHA-256, with the active release, verified against the installed manager key.
When all match (an update that changed only the GUI and worker; the build is
reproducible, so an unchanged plugin keeps its checksums) nothing is written or
remounted and no restart is asked. Otherwise it replaces what differs:
the launcher through `manager/nbt-manager-launcher.next`, checked and renamed
over it (a running launcher keeps its file); the plugin by remounting `/fs/sda0`
read-write (`mount -uw`), writing and checking the new set in
`Plugins/.NbtManager.setup`, renaming `NbtManager` to `.NbtManager.old` and the
setup folder to `NbtManager`, removing the old folder, and remounting read-only
(`mount -ur`, also after a failure). No file iDrive has loaded is rewritten. A
swap interrupted between the renames leaves no plugin until the next `SETUP`
completes it. When the plugin was replaced the GUI asks "Restart iDrive?" with
Later focused; only Restart iDrive sends `IDRIVE RESTART`.

## Manager updates from a phone

A paired phone uploads a manager release (`NBTRLS1`) like a package. In the
session the worker verifies it against `trust/manager.pem` and the profile's
compatibility, and refuses it (`Package rejected: <reason>`) when it is not
newer than the high-water mark, while another update is pending, or while the
stable launcher is not the active release's (`update NBT Manager once with the
installer first`: an older launcher cannot stage it). `STATUS` then reports
application `manager` with the running and the new version; the GUI reviews it
as "NBT Manager", without "Install, even if older". `APPROVE` re-verifies it
under the operation lock, moves it to `state/manager/release.upload`, writes
`state/manager/stage.request` (`NBTSTAGE1\nsha256=<hex>\n`) and reports state
`4` "NBT Manager <version> starts when App Manager opens again". The worker
does not stage it: the launcher holds the staging locks while the manager runs.
At the next opening the launcher removes the request, stages the upload as
`--stage-update` would, and runs it on trial; once confirmed, `SETUP` brings the
launcher and the plugin to it.

## Bringing a running application to the front

A package opts in with `bridge=1.0` and `show` in `bridge-uses` or
`bridge-optional`. The application installs a `SIGUSR1` handler **before its first
readiness heartbeat** and restores its own window from the main loop when
requested. The handler should only set a `volatile sig_atomic_t` flag; rendering
and allocation stay outside signal handlers. Repeated requests may coalesce.
The launch grant `NBT_BRIDGE_CAPS` determines which capabilities the instance
actually has; an undeclared capability is never granted.

The host holds a lifetime write lock on byte 0 of `state/<app>/host.lock`.
After its child is ready, a host granted `show` additionally takes a read lock on
byte 1. The worker checks that both locks belong to the same kernel-reported
PID before sending that host `SIGUSR1`. The host forwards only to its own
unreaped child and only with the grant; it withdraws the marker before stopping
the child and does not forward while stopping.
Locks disappear on exit, so no persisted PID/capability file can become stale.
A whole-file writer is not a show marker.

`START <app>` replies `OK` once a start or show request is accepted. Display
success is asynchronous and needs hardware validation.

**The application surface.** A user host granted `show` takes a write lock
on byte 0 of `state/manager/surface.lock` before it starts the application
and passes `NBT_SURFACE=1291`, the one LayerManager surface for applications
(`docs/head-unit-interfaces.md`); the lock goes with the host. A second
application granted `show` is refused while it is held: `START` replies
`ERR screen` and starts nothing (a host that still loses the race starts its
application without `NBT_SURFACE`). For an application granted `show`,
`START` replies `OK screen` when it starts one, and when it forwards a show
to a host holding the surface. The GUI then hands that surface the panel
while the plugin keeps iDrive held (`docs/controller-input.md`). When the
application ends (`RUNNING` no longer lists it after it was seen running; its
surface can stay on the layer, drawn by nobody), leaves the panel or never
appears within 10 s, the manager takes the panel back and shows its list;
after a plain `OK` it closes as before. Health checks never get a surface. A running host without
the grant, or still awaiting readiness, gives `ERR running`; the GUI displays
"<app> is already running" and stays open. Transport/host lookup failures give
`ERR start`. `RUNNING`, `STOP` and the readiness/health contract are unchanged.
The test app restores its existing Screen window on a granted user show request;
health-check launches ignore show requests and keep the controller untouched.

**Paused while iDrive is shown.** When iDrive takes the screen from an
application the GUI showed (NAV, MENU, OPTION and the other hard keys, a
call, a warning), the GUI sends its host `SIGUSR2` and the host stops the
application's process group with `SIGSTOP`. Meanwhile the host discards MOST
messages and gamepad input for it (they are not queued for later) and does
not count its missing heartbeats. A show request (the user selects it in
App Manager: `START` replies `OK screen`) continues it with `SIGCONT` before
forwarding `SIGUSR1`; its heartbeats are due again within 3 s. After 2
minutes paused the host stops it as `STOP` does (`SIGTERM`, then `SIGCONT`
so the stopped application receives it). An application needs no code for
this, but should expect time to jump across a pause and its audio device to
underrun.

## Application status lines

A user host granted `status` creates a fresh private regular file
`/dev/shmem/nbt-bridge1-<app>.status` and passes it as descriptor 5 with
`NBT_STATUS_FD=5`. The descriptor is absent for undeclared capabilities and
candidate health checks, and when the file cannot be created safely (the host
logs `no status channel`). The app owns its descriptor and closes it on exit.
Likewise a user host granted `most` passes its MOST socket as descriptor 6 with
`NBT_MOST_FD=6` ([MOST bridge](docs/most-bridge.md)).
Write one whole `<sequence> <text>\n` at offset zero and truncate to its new
length, without rename or fsync (`nbt_bridge_status_format` and
`nbt_fs_status_write` are reusable helpers). Sequence is canonical unsigned
64-bit decimal; text is 1-24 printable ASCII bytes, including spaces and pipes.
A single write does not guarantee an atomic record; malformed/torn reads fall
back until the next refresh. Status is display-only and never proves health.

A ready host holds a read lock on byte 3 of `host.lock` while its child is
running and granted `status`; byte 2 stays free to prevent merging with the
byte-1 show marker. The worker checks the matching lifetime owner before and
after its bounded read. Missing, malformed, symlinked, multiply linked,
non-private and special files are ignored. The marker is withdrawn before
child shutdown, and new hosts reset the old status bytes before launching.
Host tests use a fixed root-local equivalent rather than `/dev/shmem`.

## Application bridge requirements

Packages can declare three ordered fields after `sha256`, before settings:

```text
bridge=1.5
bridge-uses=
bridge-optional=show
```

`bridge` names the required major and minimum minor. Required capabilities must
be implemented; optional capabilities are granted only when available. The
current bridge is **1.5 with `show,status,live-settings,most,gamepad,controller,files`**, so the example grants `show`.
This manager runs bridge 1.5 and later only (`NBT_BRIDGE_OLDEST_MINOR`): an
application declaring an older minor is disabled until updated (below).
An application declaring `most` adds a fourth field, `bridge-most=<policy>`, the
MOST functions it may use ([MOST bridge](docs/most-bridge.md)). One declaring
`files` (bridge 1.5 or later) adds `bridge-files=<policy>` after it, the files it
accepts ([files bridge](docs/files-bridge.md)).
Without these fields an application gets no bridge. All three fields are
required together, with canonical version numbers and bounded, unique,
disjoint capability lists. See [the bridge contract](docs/app-bridge.md) for
syntax, limits and the roadmap.

The worker checks dependencies after verifying the package and before review;
`STATUS` and the phone page report incompatibility through their existing failure
messages. It rechecks before installation. Installed requirements are part of
`NBTSTATE2` and its recovery journal (`NBTJ2`), paired with the executable across
confirmation and rollback. A MOST policy makes them `NBTSTATE3`/`NBTJ3`, which add
`bridge-most=` (`old_`/`new_` prefixed in the journal) after `bridge-optional=`;
a files policy makes them `NBTSTATE4`/`NBTJ4`, which add `bridge-files=` last.
Each version is written exactly when needed and a record of another version is
rejected, so an older manager never misreads a policy. No-bridge apps keep the
existing record formats.
The host refuses incompatible starts; user launches and candidate health checks
receive `NBT_BRIDGE=<served major.minor>` and `NBT_BRIDGE_CAPS=<granted names>`.
Absent optional capabilities are omitted. Legacy apps get neither variable.

### Disabled until updated

An installed application whose declaration this manager does not meet (a major
it does not serve, a minor newer than it serves or older than the oldest it
runs, a required capability it does not implement) is disabled, never started,
so a manager update cannot leave behind an application that crashes
(`include/nbt/app_compat.h`). Nothing is stored: each check reads the installed
record, so updating the application, or going back to a manager that runs it,
enables it again. An application declaring no bridge still runs.
- `LIST` marks it `<app>|update`; the GUI's menu shows "<app>  (needs an update)".
- `START <app>` replies `ERR incompatible`; the GUI says "<app> needs an update
  for this manager"; the worker's log has the reason.
- `SETTINGS <app> live` adds `+update` (and grants neither `live` nor `+files`):
  its page still opens, so it can be uninstalled.
- Automatic starts skip it without counting a failure.
- A package with such a declaration is refused at installation, with the reason
  ("declares bridge 1.4; this manager runs 1.5 and later").

Manager staging does not refuse an update that drops an installed app's required
bridge version or capability: it stages it and names the first application it
disables (`Disabled until updated: <app>: <reason>` on standard error); explicit
recovery writes `Recovery warning: ...` the same way. Release lines cannot state
the oldest minor the worker runs, so staging cannot see that one; the launcher's
provisioning adds it to the active release's lines. New app provisioning or
updates wait until pending manager activation is confirmed or rolled back. This
is the manager's first installable release; initial setup installs the current
launcher and ordinary later updates replace GUI/worker.

## USB gamepad bridge

Bridge **1.3** adds gamepad. Declared optional, it keeps an application
compatible with managers that lack it (they omit it); this manager runs bridge
1.5 and later, so applications declare 1.5. User launches granted the
capability receive NBT_GAMEPAD_FD=7, a nonblocking Unix datagram input channel.
Health checks never receive that descriptor or open USB. The channel remains
available without a controller; its state is disconnected while it has no
foreground entitlement. The manager GUI optionally uses the same service.
See [the gamepad contract](docs/gamepad-bridge.md) for framing, controller
support, ownership, and required car validation.

## iDrive controller bridge

Bridge **1.4** adds `controller`: the iDrive controller (knob turn, push,
tilt, MENU, BACK, OPTION, COM, MEDIA, NAV, MAP) and the centre console's
favourite keys 1-8, decoded by the manager. User launches granted it receive
NBT_CONTROLLER_FD=8, a Unix datagram socket carrying one 16-byte state per
change (`include/nbt/controller_wire.h`; reader `nbt::controller::Input`,
`src/sdk/controller_input.cpp`). States reach only an application with a
surface that is not paused; going quiet sends a released, disconnected state
first. No signed policy is needed and no raw MOST is exposed. See
[the bridge contract](docs/app-bridge.md), section 4.

## Applications' files

Bridge **1.5** adds `files`: a folder of files the user brings to the car, on
the head unit's hard disk, bounded by the signed `bridge-files=` policy. User
launches granted it receive `NBT_FILES_DIR`, read only for the application.
Bridge 1.5 also puts the gamepad's family in byte 3 of its state, for
applications declaring 1.5 or later. See
[the files contract](docs/files-bridge.md) for the policy, storage and checks.

A paired phone uses `/v1/files/list`, `put`, `data` and `remove` (routes
below). The GUI's Settings > Files page uses these commands; replies are one
line, files are addressed by their index in the worker's own listing, so names
never cross the command line:

| Command | Reply |
| --- | --- |
| `FILES <app> first` or `next` | `FILES\t<count>\t<used>\t<quota>\t<free>\t<more>`, then `\t<name>\t<size>` for up to 12 files, the first page or the next one |
| `FILES DELETE <app> <i>` | `OK`: file `i` of the last page deleted |
| `FILES SCAN <app>` | `OK`: the USB sticks are scanned on the worker's job |
| `FILES USB <app> <offset>` | `USB\t<phase>\t<reason>\t<done>\t<total>\t<percent>\t<count>`, then `\t<name>\t<size>\t<0 or 1: stored>` for up to 12 files from `offset`; phase `idle`, `scanning`, `scanned`, `importing`, `imported` or `failed`, reason `-` or a word |
| `FILES IMPORT <app> all` or `<i>` | `OK`: every file found, or one, copied on the worker's job |

Refusals are `ERR <reason>`: `app`, `busy`, `unavailable`, `invalid-command`
or a files reason (`quota`, `space`, `count`, `size`, `type`, `name`,
`missing`). `SETTINGS <app> live` answers `live+files` or `restart+files` (and
`+autostart` or `+autostart-paused`, "Autostart") for
an application granted `files`.

## Application settings

A package may declare up to twelve settings, each a choice among a few values, with optional manifest lines after `sha256` and any bridge declaration:

```
setting=<key>|<label>|<default>|<choice>,<choice>...
```

The key is 1-16 of `[a-z0-9_]` and starts with a letter; the label is 1-24 printable ASCII characters other than `|`, without leading or trailing spaces; there are 1-6 distinct choices of 1-12 `[a-z0-9_-]`, and the default is one of them. Keys are distinct. Any other line, or a malformed setting, makes the manifest invalid; workers older than this format reject packages that declare settings. `tools/package.py --setting '<key>|<label>|<default>|<choices>'` adds one (repeatable).

The worker stores the declaration of the installed package in `state/<app>/settings.schema` (`NBTSETTINGS1`, then the `setting=` lines) when the application is provisioned and when an installation is confirmed healthy; a failed or rolled-back update keeps the previous declaration. If saving it fails after confirmation, the update stays installed and reports `Update installed; settings not updated`. The user's choices are in `state/<app>/settings` (`NBTVALUES1`, then `key=value` lines). A value applies only while the installed declaration still has its key and allows the value; otherwise the declared default applies, so values survive an update that keeps the setting. Both files are replaced through a `.next` file, synced and renamed. Offline bundles (`tools/prepare_bundle.py`) include the verified signed schema and default live snapshot.

`SETTINGS <app> live` is the optional annotated query: its reply is
`SETTINGS\t<count>\t<live|restart>` followed by the same setting fields. It
reports `live` only when the installed signed declaration grants `live-settings`
with the current provider; the manager's own settings return `restart`. An
application disabled until updated gets `restart`, with `+update` ("Disabled until updated"). Plain
`SETTINGS <app>` is unchanged. GUI settings counts use canonical decimal 0-12.

Provisioning creates private `data/<app>`; **`NBT_DATA_DIR`** is its validated
absolute path for every app launch, including health checks. App-owned data
survives executable updates and rollback. The reserved `settings` file is a
manager-written `NBTVALUES1` snapshot of all effective settings. `SET` first
persists authoritative choices, then updates the snapshot with one bounded
write, truncate and sync, keeping the inode. The snapshot is also refreshed at
provisioning, schema confirmation and live-app launch. Settings writes hold the
application transaction lock; the same worker refuses them during its update.

Bridge **1.0** apps granted **`live-settings`** receive **`NBT_SETTINGS_FILE`**,
the absolute snapshot path. `NBT_SETTING_*` still carries startup values. Apps
re-read the file on demand, keeping defaults or last valid values on failed or
malformed reads; `nbt_settings_values_read` in `src/sdk/settings_file.cpp`
provides this bounded reader. Snapshots are eventual: concurrent reads can see partial
or mixed records. A failed write/sync returns `ERR settings`; a late failure
may already have saved the durable choice, repaired on retry or next launch.
Snapshot targets must be private owned regular files with one link on the
managed filesystem. Package metadata selects neither snapshot nor data paths.
The signed manifest's total limit remains 2048 bytes, including all settings.

In the GUI, holding the push on an application opens its settings (`SETTINGS <app> live`), which end with "Stop <app>" while it runs; the main menu's Settings opens the manager's own (`SETTINGS manager live`). A live app's page says "Changes apply at once". Choosing a setting lists its values (the current one marked), of which one is saved with `SET`.

Applications therefore need no iDrive plugin of their own: the manager's main menu starts them, and their settings, and stopping them, live in the manager.

### Autostart

The worker accepts `--autostart` as a one-shot dispatcher and `--host-auto <app>`
as its guarded detached host. It starts enabled applications through the same
recovery, user environment, readiness, lease and heartbeat contract as menu
starts. Repeated dispatch skips existing hosts without sending show. Dispatch
acceptance is asynchronous, like START; the host may subsequently reject a
busy, guarded or damaged application.

`state/<app>/autostart` is exactly `NBTAUTO1\nenabled=<0|1>\n`;
`state/<app>/autostart-guard` is exactly `NBTAUTOGUARD1\nfailures=<0..3>\n`.
Both use fixed path-policy kinds and durable `.next` write/sync/rename/barrier,
serialized by the app transaction lock. The host holds its lifetime lock and
increments the failure count **before** recovery and application launch. Only
10 seconds of readiness, heartbeats and the application's lease clear the count.
Failure, stop or host/power interruption before that point leaves the attempt
counted. Three consecutive unconfirmed attempts pause automatic starts.
Unreadable/malformed records fail closed; a manual START can repair a malformed
regular guard, while aliases and unsafe files remain refused. Manual START
resets the guard before attempting start/show; even an unsuccessful manual
attempt re-arms it. A late persistence error can already have saved the record;
retry and read it back. Settings and application data are untouched.

Boot: once per boot (the record `/dev/shmem/nbt-manager.autostart`, which an
iDrive restart keeps), 25 s after iDrive loaded it and after the door's job,
the plugin runs `nbt-manager-launcher --autostart` with iDrive's environment
(its preloads included: applications draw). The launcher takes the manager
operation lock (retrying for up to 10 s), verifies the known-good worker (the
active slot, or the previous one while a manager update is pending, so a boot
job never confirms a candidate), runs its `--autostart` and holds the lock
until that dispatch has ended (at most 15 s). No GUI opens.

GUI: an application's settings page shows **Start with the car** after the
settings its package declares (`include/nbt/gui/autostart_ui.hpp`).
`SETTINGS <app> live` adds `+autostart` to its mode field when the preference
is on, or `+autostart-paused` when it is on but three unconfirmed attempts
paused it; selecting the row sends `AUTOSTART <app> on|off` and reads the page
again. While paused the page says to start the application once by hand, which
resets the guard. A paired phone reads and switches the same preference with
`POST /v1/autostart` (the worker and the door), through the same code
(`src/worker/autostart_request.cpp`).

### Managed application isolation and fault quarantine

Active application executables started through `ProcessGroup::start` run inside
a per-child OS sandbox (`src/process/child_sandbox.cpp`): own process group
(`setpgid(0, 0)`, signaled as `-pid` when stopping), reset signal handlers and
signal mask, lowered scheduling priority (`setpriority`, plus QNX
`sched_setscheduler(SCHED_OTHER)` and QNX APS partition `"nbt_apps"` lookup/join
when configured on the target), and bounded `setrlimit` caps (`RLIMIT_CORE=0`,
`RLIMIT_NOFILE=128`, `RLIMIT_FSIZE=128 MiB`, `RLIMIT_DATA=256 MiB`, plus QNX
`RLIMIT_AS=512 MiB` and `RLIMIT_NTHR=64`). Launcher, worker, GUI and
`LayerManagerControl` remain outside this sandbox. Privilege-drop (`dropUid`/
`dropGid`) and `chrootDir` fields are prepared in `ChildSandbox` (defaulting to
`-1` and `NULL`) until the car's `HmiMain3D` UID, `/dev/screen` ACLs, `0700`
storage directory permissions and `/opt/sys/lib` GLES2 DSO dependencies are
verified on hardware. Centralizing per-application `MostService` DSI clients into
a single worker broker and enabling `kOpenFromPhone`/CDS car-data preemption in
`plugin/phone_runtime.cpp` likewise remain target-gated.

`state/<app>/fault-guard` is `NBTFAULT1\nfaults=<0..3>\n` (`NBT_APP_FAULT_LIMIT = 3`,
`src/lifecycle/app_faults.cpp`). A host clears it once the application reaches
30 seconds of healthy operation, and `Worker::installNew`, `InstallSupervisor`
update confirmation, or `UNBLOCK <app>` clear it explicitly. Contract faults
(`result != 0`), crashes or early failures before 30 seconds of health, and
GUI-initiated `STOP <app> fault` aborts (when `surface.lock` fails to appear or
the user holds `BACK` for 2 seconds to escape) increment it under the application
transaction lock. Once 3 faults are reached (or if the record is unreadable or
malformed), `START <app>` replies `ERR quarantined` and the Settings page offers
`Unblock <app>` (`UNBLOCK <app>`).

## Manager settings

`SETTINGS manager` and `SET manager <key> <value>` use the same formats for the manager's own settings (`src/lifecycle/manager_settings.cpp`). They are not stored as a declaration: each one reflects, and changes, a file the manager already uses.

| Key | Label | Choices | Meaning |
| --- | --- | --- | --- |
| `debug_log` | Debug log | `off`, `on` | `manager/debug.log` exists. `on` creates it empty (mode 0600) and the worker logs from then on; `off` moves it to `manager/debug.log.1`, replacing the previous copy, so the last log can still be fetched, and every writer stops at its next line. A log file the writers would refuse (wrong owner, mode or type) makes both commands answer `ERR settings`. |
| `invert` | Invert knob | `off`, `on` | `invert=` in `manager/controller.conf` |
| `language` | Language | `auto`, `en`, `fr`, `de`, `it`, `es`, `zh`, `ja` | The language of the manager's screen, one line in `manager/language` (mode 0600, replaced through `manager/language.next`); absent or invalid is `auto`. |
| `phone_open` | Open from phone | `off`, `on` | Enable stationary, home-screen wake requests; defaults off. |
| `release` | Hand back controller | `off`, `on` | `release=` in `manager/controller.conf` |

`SETTINGS manager` lists `debug_log`, `invert` and `release` only. `SET` also
takes `phone_open` and `language`: `language` has more choices than a
declaration allows (six), so listing it would make phone apps that check that
limit reject the whole reply. The GUI has its own Language screen for it.

The controller values reported are those in effect: the defaults when `controller.conf` is absent or invalid. Changing one writes the complete configuration in effect, every key included, through `manager/controller.conf.next` (synced, then renamed), so keys set with the desktop installer keep their values and an invalid file is replaced by the defaults plus the change. The GUI reads the controller configuration when it starts, so changes apply the next time the manager opens. Instances and key slots stay with the desktop installer.

Managed executables are invoked with `--ready-fd 3 --lease-fd 4`. Before signalling readiness, an executable takes a shared POSIX read lock on descriptor 4 and keeps it for its lifetime. It writes the byte `R` to descriptor 3 at least once every three seconds, sends its first `R` within five seconds, and exits if the pipe closes. The environment holds `NBT_LAUNCH`, declared bridge grants (above) and the settings: `NBT_LAUNCH=user` when its host started it from the menu, when it may take the screen and the controller until the user leaves it, and `NBT_LAUNCH=check` for the health check of an update, when it must leave the controller to the manager (input it took would end the check). Each declared setting is `NBT_SETTING_<KEY>=<value>`, the key in capitals; a setting whose files cannot be read is left out and the application uses its own default. The worker verifies that the child owns the lock and receives heartbeats. Candidate confirmation requires 10 seconds of continuous healthy operation (`NBT_HEALTHY_MS`). The `NBTFoundry/sdk` library implements this contract for applications.

## Manager GUI controller

`src/gui/ui_controller.cpp` and the phone helpers are I/O-free controllers.
The GUI host passes controller events and private worker replies, then renders
the view. Push on an application starts it; hold the push for 1.5 seconds to
open its settings (the hold dialog appears after 0.5 seconds). Applications
keep running after the manager closes. Settings expose declared choices,
Stop while running, and Uninstall. Local and phone-requested uninstall both
default to No. Phone requests name the requesting phone.

The main menu lists the applications, then Settings; a paired phone's upload is
reviewed from it (there is no separate Install or update entry). The manager's
Settings end with Language, Pair a phone, Paired phones (remove one with No
focused) and
Status (local only, no worker request: the capture shim, the MOST controller
input, the gamepad and the last error; Test the gamepad shows its buttons,
sticks and triggers and leaves on the iDrive controller's BACK only).
Pair a phone opens a session for the car's Wi-Fi details, then a 60-second
window, and shows only the Wi-Fi QR. A signed pairing request replaces that code
with the phone name and the six digits, drawn large on a highlighted panel, with
No focused. Opening the manager from the phone is off.

The screen shows English, French, German, Italian, Spanish, Simplified Chinese
or Japanese (`src/gui/text/<code>.txt`, `nbt/model/language.hpp`). Language
lists Automatic, then each language by its own name; choosing one sends `SET
manager language <code>` and, on `OK`, shows it at once. Automatic follows
iDrive: the iDrive plugin reads car data `vehicle.language` and writes its
number to `/dev/shmem/nbt-manager.language` (`NBTLANG1\nvalue=<n>\n`, a private
record as the wake records); the GUI reads the setting when it starts and the
record once a second. iDrive's numbers are mapped as 1 German, 2 and 3 English,
4 and 16 Spanish, 5 Italian, 6 and 15 French, 10 and 11 Chinese (the manager
has Simplified Chinese only), 13 Japanese; any other language (Dutch, Korean,
...) or no record shows English. The worker keeps speaking English: the GUI
translates its known STATUS messages (the `w_` texts of `src/gui/text/en.txt`)
and shows others as sent. Application names, their declared setting labels and
status labels are shown as the application gives them. The debug log is in
English, but for the message logged with a screen change, which is as shown.

A signed upload is reviewed regardless of whether the driver selected the
waiting screen. Review defaults to Reject; Install requires a second Yes with
No focused. Recovery is separate and explicitly labelled. `APPROVE` is emitted
only from controller input, bound to the digest in the latest verified STATUS.
Package changes require a new review. Only the private channel can confirm
pairing or uninstall. `src/gui/phone_ui.cpp` keeps background phone queries in
the same poll cycle as the existing one-second application refresh.

The GUI reads the iDrive controller over MOST ([docs/controller-input.md](docs/controller-input.md)). Without that input it remains a preview that lists applications and closes by itself. `--open` always runs the GUI with a heartbeat deadline of three seconds. GUI commands normally use a five-second reply timeout (`SESSION` uses 15 seconds while the worker coordinates MOST `0xA0.03` Wi-Fi Direct readiness with a 10-second internal deadline), and the worker answers lifecycle requests without waiting for applications to exit; installation still copies the executable within the worker loop, and its duration on target storage needs validation. The session listens on `wfd0`, the car's Wi-Fi Direct group (172.16.222.1), port `8080` (`apps/manager/main.cpp`): before returning pairing details on `wfd0`, `WifiDirectController` verifies `WLANState3` (`0xC10`), enables `0xA0.03` via `SelectWLANState3` (`0xC11`) only if disabled (restoring it on `CANCEL`, expiry or shutdown), verifies a visible SSID in `WLANConfiguration` (`0xC12`) with passphrase fallback from `WFDPass.conf`, and checks that `wfd0` is up with an IPv4 address. CarPlay (`0xA0.01` / `uap0`) is never modified. Pair a phone shows a Wi-Fi-only code for that group; packages and management requests use signed /v1/ routes.

The manager GUI accepts `--ready-fd`, `--lease-fd` and `--worker-fd` from the
launcher. It holds the lease and emits a heartbeat only after the worker
handshake and a successful `LIST` request, repeating the request before each
heartbeat. It never starts or restarts a worker: when the channel fails (no
reply, a malformed reply, end of file) it closes it and exits with status 1, and
the launcher stops the worker and restarts both. It also exits when the readiness
pipe closes. The ordinary unsupervised preview closes after 15
seconds; the supervised preview currently closes after 45 seconds.

The launcher restarts an abnormally exiting or unresponsive GUI after 0.5, 1 and
2 seconds. Three consecutive restarts exhaust the restart budget; during a
candidate trial this restores the known-good slot. The budget is recorded in the
`NBTMSTATE2` manager state as `restarts` and `restart_epoch`, expires after 60
seconds, and clears after 60 seconds of ready stable operation or candidate
confirmation. `NBTMSTATE1` remains readable and gains zeroed restart fields when
rewritten. Fatal GUI signals append a bounded record to `manager/crash.gui` and,
when enabled, the debug log; the launcher reports the marker before restarting,
and the next GUI removes it and displays a recovery notice.

A profile at `trust/apps/<id>.conf` contains exactly:

```text
NBTAPP1
compat=demo-1
```

Application packages are verified against the apps key bundled with the manager (`nbt/apps_key.h`); the manager never writes `trust/keys/`; keys found there, from older installations, are ignored and removed with their application. The active executable is `apps/<id>/bin/app`. The `state/<id>/installed` record tracks the active digest, version, sequence, highest accepted sequence and any bridge requirement. Profiles, keys and records are provisioned locally and are never supplied as package destinations.


## Paired-phone HTTP protocol 1

The worker serves port 8080 while its GUI runs; `nbt-managerd --door` serves
8081 from boot. Both bind to the address on `wfd0`. The host-test build permits
loopback. Every response is `text/plain`, `Cache-Control: no-store`, and closes
the connection. No HTTP route approves an action, starts/stops an application,
or serves a web page. Previous bearer routes are rejected.

### Keys, encoding and request signatures

A phone uses a separate P-256 key for each car. A public key is the 65-byte
SEC1 uncompressed point (`04 || X || Y`), encoded as 130 lowercase hex digits.
The key id is SHA-256 of those 65 bytes, encoded as 64 lowercase hex digits.
Phone names are printable ASCII, 1-32 bytes, without tabs, `|`, or leading or
trailing spaces. The car stores at most four phones.

`GET /v1/nonce` is the only unsigned request. It returns `NBT-REQ-1\n` and
`X-NBT-Nonce: <64 lowercase hex digits>`. All other requests carry exactly one
of each header:

```
X-NBT-Key: <key id>
X-NBT-Nonce: <nonce>
X-NBT-Signature: <DER ECDSA signature as lowercase hex, 8-72 bytes>
```

The phone signs the SHA-256 of this exact ASCII text with ECDSA P-256/SHA-256
(including its final LF):

```
NBT-REQ-1\nMETHOD\n/v1/path\nIPv4:port\nnonce\ncontent-length\nsha256(body)\n
```

METHOD is uppercase GET or POST; the path is the literal route without a query,
escaping or trailing slash. Host is the listener's exact numeric IPv4 and port,
and must match the HTTP Host header. Length is unsigned decimal with no leading
zero except `0`; body SHA-256 is lowercase hex. GET signs the empty body's hash
and length zero. POST requires Content-Length; upload uses
`application/octet-stream`, and all other POST routes require `text/plain`.
There is no chunking, compression, Origin or Authorization header. Duplicate
recognized headers, folded fields, overlong fields and encoded routes fail.

Every reply supplies the next nonce when the pool has capacity. Use that nonce
for the next request and serialize requests on each connection/action. The car
keeps at most 32 outstanding nonces, valid for 60 seconds. A nonce is consumed
before body reception and cannot be reused, even after a failed signature.
The worker and door have independent pools; signatures bind their host/port.
At most 120 signature attempts per minute are admitted by each listener. A full
nonce pool returns 503 without a replacement nonce; retry after expiry.

The worker's slow steps run on its job thread, one at a time, so that it keeps
answering the GUI (which pings it every second with a 2 s timeout) and the
phones: a package's verification, an install's copy (a new application's and an
update's) and `SETUP`. Meanwhile `STATUS` reports state `6` "Checking the
package" or `3`, and new sessions, uploads, `START`, `STOP` and removals are
refused. The debug log records each step's duration ("verification test: 812
ms").

The worker hashes an upload while receiving it into bounded private staging.
Only after its exact length and signature verify does it enter the install
session and verify the signed package. An invalid signature cannot replace an
existing reviewed package or alter installed state. The staging cap is
64 MiB + 4096 bytes; all other bodies are at most 2048 bytes. There are four
connections, a 5-second header deadline, a 15-second inactivity timeout and a
10-minute total request deadline. Each request owns its connection; no pipelining.

### Routes and replies

| Route | Body | Success body / behavior | Door |
| --- | --- | --- | --- |
| `GET /v1/nonce` | empty | `NBT-REQ-1\n`, next nonce in header | Yes |
| `GET /v1/apps` | empty | `APPS\t<count>\n`, then `id\tversion\tsequence\t<0\|1 running>\n` per app | Yes |
| `POST /v1/settings/read` | `app\n` | `SETTINGS\t<count>\n`, then `key\|label\|value\|choice,choice\n` per setting | Yes |
| `POST /v1/settings/set` | `app\tkey\tvalue\n` | `OK\n`; declared choice applied under the app transaction lock, without a car prompt | Yes |
| `POST /v1/autostart` | `app\n`, or `app\t<on\|off>\n` | `AUTOSTART\t<off\|on>\t<ready\|blocked>\n`, or `OK\n` once saved (the pause stays until a start by hand); 400 for anything but an installed application and `on`/`off`, 409 while it is being updated ("Autostart") | Yes |
| `POST /v1/wake` | empty | `WAKE\t<opening\|open\|busy\|moving\|off>\n` | Only |
| `POST /v1/pair` | below | HTTP 202 `PAIR\tpending\t<6 digits>\n`; local confirmation required | No |
| `POST /v1/upload` | complete `.nbt` package | HTTP 202 `STATUS\twaiting\tChecking the package\n` while the worker verifies it on its job, then `verified` or `failed` through `/v1/status` (`STATUS\tverified\tReview and confirm on the car\n` when it was verified at once); HTTP 400 for an upload that is no application package | No |
| `POST /v1/remove` | `app\n` | HTTP 202 `STATUS\twaiting\tConfirm uninstall on the car\n`; local No/Yes prompt | No |
| `GET /v1/status` | empty | `STATUS\t<state>\t<message>\n`, scoped to this key's last action | No |
| `POST /v1/logs/app` | `app\n` | The application's log as raw bytes (below); empty when it has none | Yes |
| `POST /v1/logs/system` | empty | `sloginfo` output, its newest 8 MiB as raw bytes; one at a time (409) | Yes |
| `POST /v1/logs/manager` | empty | NBT Manager's debug log, `debug.log.1` then `debug.log`, the newest 4 MiB of each, raw; empty while the debug log is off | Yes |
| `POST /v1/logs/clear` | `app\n` | `OK\n`; the application's log emptied, `output.log.1` removed | Yes |
| `POST /v1/files/list` | `app\t<after>\n` | `FILES\t<count>\t<used>\t<quota>\t<largest>\t<free>\t<more>\t<extensions>\n`, then up to 20 `name\tsize\n` after `<after>` | No |
| `POST /v1/files/put` | `app\tname\tsize\tsha256\n` | `OK\n`; the file is admitted and this phone holds a 60-second ticket for its bytes | No |
| `POST /v1/files/data` | the file announced, `application/octet-stream` | `OK\n` once stored; from the ticket's phone, of its size and SHA-256 | No |
| `POST /v1/files/remove` | `app\tname\n` | `OK\n`; deleted at once, without a car prompt (the phone app asks) | No |

The files routes refuse with `ERR <reason>\n` and an HTTP status that says
which ([docs/files-bridge.md](docs/files-bridge.md)).

Application sequences are decimal uint64 values; preserve them without floating
point conversion on the phone. Lists have at most 16 installed applications and
settings have at most 12 entries, each with at most six choices. The reserved
application `manager` is unavailable through the HTTP settings/remove routes.

Pairing body (exact ordered fields, final LF required):

```
NBTPAIR1
name=Pixel
key=<public key hex>
```

It is signed by the proposed key and accepted only while Pair a phone is open.
Both displays derive the six digits as
`uint32_be(SHA256(nonce_bytes || public_key_bytes)[0:4]) mod 1000000`, padded
with leading zeroes. The pairing window and pending removal expire after
60 seconds. Pairing status can be read with the proposed key while awaiting or
reporting refusal; successful pairing immediately requires the stored key, so
forgetting it revokes status access too.

After car confirmation, pairing status is:

```
STATUS\tpaired\tPhone paired\nCAR\t<bssid>\t<IPv4:port>\n
```

BSSID is the six-byte Wi-Fi group interface MAC, lower-case colon-separated.
The phone already has the SSID/passphrase from the scanned Wi-Fi QR; it stores
those plus this identity/address only after success. Missing interface identity
prevents pairing. The loopback fixture uses `02:11:22:33:44:55`.

Other status states are `waiting`, `verified`, `installing`, `installed`,
`removed`, `failed`, `cancelled`, and `expired`. A result is retained for five
minutes, extended while installation runs. Messages are bounded display text;
the phone must decide behavior from the state, not message wording. A key
cannot inspect another key's action. A later accepted action replaces the
previous result. A reviewed or installing package and pending confirmations
block a competing upload/removal.

### Logs

An application host makes the application's standard output and error a
pipe and copies it into `state/<app>/output.log`, always, not only while
the debug log is on. At 64 KiB the file moves to `output.log.1`, so an
application keeps at most 128 KiB. The host adds its own lines:

```
--- 2026-11-16 10:42:03 started v1.2 (pid 412)
--- 2026-11-16 10:42:05 ended by signal 11 (SIGSEGV, invalid memory access)
```

Other endings are `exited with status <n>`, `stopped by the manager`,
`stopped: it did not signal ready (write 'R' to fd 3) within 5 s`,
`stopped: no heartbeat ('R' on fd 3) for 3 s`, `stopped: it wrote something
other than 'R' to fd 3`, `stopped: it closed fd 3 or released its lease on
fd 4 but kept running`, and a start the host refused (`cannot start: bridge
grant refused` and the like).

An install's health check (the worker runs the new version for up to 45 s
before confirming it, without a host) writes to the same log: its
output, then `install check of <version> started (pid <n>)`, how it ended if
it ended as above, and `install check passed` or `install check failed`
(after `not healthy within 45 s` when it ran too long, or `install check of
<version>: cannot start (error <n>)`). So an application that was installed
but never started already has a log.

Times are the car's local clock. Output an
application buffered but had not written when it crashed is lost: write
diagnostics to stderr, or flush stdout. The host syncs its notes to flash at once
and output within a second: the car's file system otherwise loses recent writes
when iDrive restarts. Applications keep no log files of their
own: the manager rotates, serves and clears this one (nbt-doom logs to stderr
only since its commit `c1e446a`).

`POST /v1/logs/clear` empties `output.log` in place, so the running host keeps
appending to it, and removes `output.log.1`.

`POST /v1/logs/app` replies with `output.log.1` then `output.log`, raw (any
bytes; not limited to printable ASCII), at most 256 KiB. `POST
/v1/logs/system` runs `sloginfo` into an unlinked file in `/dev/shmem` while
the worker carries on; the reply waits for it (at most 10 s) and carries
its newest 8 MiB. The car's slogger keeps a 1 MiB ring, about 5 MB of text.
Both replies are streamed, so their `Content-Length` exceeds the 4 KiB of
other replies; a phone saves them to a file instead of reading them as text.
Failures to inspect or seek an opened log return 503. With the manager's debug
log on, each application-log request records missing files or the byte counts
to send, and read failures with errno. The host reports open, stat, write,
sync and rotation failures once per operation per host in the same debug log.
If rotation fails, it drops the new chunk and retains the existing log within
its size limit.

HTTP errors: 400 malformed route/body/package, 403 authentication or unavailable
door route, 409 busy/invalid choice/capacity, 429 wake rate limit, 500 local I/O,
503 temporary network/nonce/store unavailability. Error bodies are fixed codes;
clients must not display raw native errors or log signed requests/credentials.
Some failures close the socket without a reply. Reconnect, obtain a nonce and
read status before proposing another action. The phone exposes no approval UI.

### Door and wake

The plugin asks the stable launcher for `--door` 15 seconds after initialization.
The launcher verifies the known-good slot (previous while a trial is pending)
under its operation lock, starts that worker and waits for a private READY
handshake before releasing the lock. The door itself holds no manager operation
lock; a separate shared-memory lifetime lock prevents duplicates. It waits for
wfd0 addressing and refreshes only its `nbt-door` packet-filter rule every ten
seconds. Three unsuccessful starts exhaust the per-boot shared-memory guard;
60 seconds of healthy liveness re-arms it. No trial is confirmed by this mode.

A signed wake is limited to one new marker per 30 seconds. Repeating it during
that interval reads the same marker's outcome. The plugin polls the fixed
private shared-memory marker every second, reads the off-by-default preference,
and uses the normal menu-entry opening path only if speed is known, zero, and
no older than three seconds; iDrive surface 256 is alone on the HMI layer; and
the graphical context is known safe and fresh. Missing/stale speed returns
moving; unavailable/camera/Android Auto contexts return busy.

The initial safe context is the car-observed ID6 home pair `visibleContext=3`,
`widgetType=LT_Table_ID6L_Pad_Title`. Other contexts, including navigation, ask
the driver to open App Manager manually until car evidence establishes them.
CDS subscriptions are change-driven, so the plugin also calls
`cdsGetPropertyAsync` once a second with the same delegates. Speed changes
are included in the optional debug log; no on-road wake test is required.

Shared signing examples are `tests/phone-signing-vectors.tsv`, checked by the
C++ `phone-test` and intended for the mobile Jest suite. `tests/phone_client.py`
is a software test phone for the real host worker, not a car approval channel.
