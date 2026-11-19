# Install flow: desktop installer and launcher

How NBT Manager gets onto the head unit, what each side checks, what is known
about the car, and where testing stands. Details live in
[desktop/README.md](../desktop/README.md) (installer),
[manager-release.md](manager-release.md) (release format, slots, updates) and
[in-car-procedure.md](in-car-procedure.md) (what the tester does).

## Installer (Windows, `desktop/`)

Rust/egui, SSH to `169.254.199.119` (built-in user and published root
password). Every remote command runs as if typed in an SSH session: in a login
shell (`/bin/sh -l -c '…'`), which reads the profile and so has the session's
`PATH` and `LD_LIBRARY_PATH`. The shell sshd starts for a command reads no
profile (dev20 and dev22 failed on it). The commands below are shown as typed.

1. **Connect.** If SSH does not answer, offer to enable it (gateway discovery
   and the external activation relay, `desktop/core/src/activation.rs`). Then
   `uname -s` must say QNX.
2. **Install** (`ssh.rs` `install`), the same five steps every time:
   1. `mount -uw /net/hu-omap/fs/sda0 && echo OK`: the plugin partition is
      read-only at boot.
   2. Upload the launcher: `umask 077; mkdir /tmp/nbt-manager-<32 hex> && cat
      > …/launcher && chmod 700 …/launcher && echo READY`.
   3. `…/launcher --bootstrap-manager <key bytes> <release bytes> <compat>
      <sha256>` with the manager public key and the signed release on stdin;
      expects `INSTALLED`.
   4. `rm -rf <staging>; test ! -e <staging>`, also after a failure.
   5. `mount -ur /net/hu-omap/fs/sda0 && echo OK`, also after a failure. A
      refusal is reported as a note; the install result stays.
3. After success the user restarts the head unit (**Restart the head unit**:
   `OnOffDSICommander appreset`, as in AAD's guide, which restarts the whole
   system, not iDrive alone) or switches the car off and on. The restart
   takes SSH down and switches it off: after at most 20 s without an answer
   the installer counts it as under way and disconnects. Once iDrive is back,
   Connect offers to enable SSH again. An
   immediate refusal (exit status) is reported instead. The installer never
   reboots the unit.

**Update** (`ssh.rs` `update`, since kit dev114) runs the release's own
launcher, uploaded as for Install: remount, upload, `…/launcher --stage-update
<bytes> <sha256>` with the release on stdin (expects `PENDING`), `…/launcher
--refresh-setup` (replaces the installed launcher and the iDrive plugin;
`REFRESHED plugin` asks for a head unit restart), remove the staging folder,
remount read-only. The installed launcher is never run: a launcher older than
`NBTRLS2` needs no `NBTRLS1` release first, so a kit carries one release (dev90
to dev113 also carried an `NBTRLS1` compatibility release, staged first by the
installed launcher's `--capabilities` answer). Update on a head unit without
NBT Manager says to use Install (the launcher's "managed root must be an owned,
private directory"). **Test tools**: collect everything (one read-only report:
both logs, `audioconn -l`, `pidin arg`, the process manager's overview, the
shim's state and the manager's files; desktop README), debug log on/fetch/off,
save the system log (`sloginfo` over SSH, no USB key), provision the test
application (`--provision-app`), write/remove `controller.conf`, restart
iDrive, and remove everything (remount rw, `rm -rf` of the manager root and
both plugin folders, checked with `test ! -e`, remount ro).

Rules for remote commands, from in-car failures and the QNX 6.5 utility
reference: no `printf` (the host has none) and no `rmdir`; never chain on
`rm`'s exit status (undocumented for missing files); every command ends with
an `echo` acknowledgement the installer checks. `known_error` in `ssh.rs`
turns `HMI layout: …` and `manager storage: …` into readable messages.

## Installer on the phone (`desktop/mobile/`, app module `car-install`)

The same steps from the phone app, over a Wi-Fi ENET adapter. The installer is
a workspace: `desktop/core` holds everything above (SSH session, install,
update, tools, gateway search, activation relay, head-unit search, embedded
release); `desktop/` is the Windows window over it; `desktop/mobile` exposes it
to Kotlin through UniFFI (`Session.connect` with a `Listener`, then
`confirm_activation`, `install`, `update`, `tool`, `disconnect`). The app's
`scripts/build-installer-core.sh` builds `libnbt_installer.so` for arm64 with
cargo-ndk and generates the binding; the kit build passes the same signed
release and test application as the Windows installer's, and builds the phone
app after the test packages.

On the phone, `core/src/net.rs` ties every car socket to the adapter's Wi-Fi
(`android_setsocknetwork` with `Network.getNetworkHandle()`) and the activation
service's socket to mobile data, resolving its name there
(`android_getaddrinfofornetwork`). The app holds both networks while a session
runs (`InstallNetworks.kt`): the Wi-Fi the phone is on, requested without the
internet capability, and mobile data. The gateway search and the head-unit
search use only the Wi-Fi's addresses. A phone cannot reach `169.254`
addresses over Wi-Fi, so it depends on the head unit taking an address from
the adapter's DHCP server, which the captures suggest (every module asks) and
no car session has confirmed yet.

Install and Update are the core's, unchanged on the phone: a fresh car takes
Install (`--bootstrap-manager`), any installed manager takes Update. Checked
on 2026-11-18: `native_install_then_update_over_ssh` (the host launcher behind
the mock SSH server: fresh install, then an update staged and applied by the
uploaded launcher). Checked so far (2026-11-15): the workspace's tests and `native_bootstrap_over_ssh`;
the Windows build; an x86_64 emulator, where the library loads, the Wi-Fi is
held, the built-in address fails, the phone's hint and the Enable SSH question
appear, and the gateway search asks only from the Wi-Fi address and ends with
the phone's message. Not checked: a phone in a car, mobile data alongside the
adapter's Wi-Fi, and the relay over mobile data.

## Launcher (`apps/manager/launcher.cpp`, `src/launcher/`)

Modes: `--open` (iDrive menu entry), `--door`, `--capabilities`,
`--bootstrap-manager`, `--stage-update`, `--stage-recovery`, `--refresh-setup`,
`--remove-capture`, `--provision-app`.

`--bootstrap-manager` (`src/launcher/bootstrap.cpp`), in order:

1. QNX 6.5 on ARMv7, else `unsupported platform`.
2. Read key and release into unlinked temporary files (exact lengths, EOF,
   deadlines); verify the signature against that key and the SHA-256 against
   the command line.
3. Plugin folder `/opt/hmi/ID5/data/ro` (`src/storage/hmi_plugin_dir.cpp`):
   the fixed path as given, no `realpath`; a directory owned by the installing
   user, writable by nobody else except root's group, on writable qnx6, not
   noexec; device and inode are pinned. Refusals: `HMI layout: <check>`.
4. Plugin preflight: refuse a different NbtManager plugin already installed.
5. Manager root `/var/opt/nbt-manager` (`src/storage/setup.cpp`): parent
   present, same owner, group write only for root's group, writable, not
   noexec, qnx6, enough space; create the root 0700. Refusals:
   `manager storage: <check>`.
6. Manager setup (`src/launcher/manager_setup.cpp`): key, profile, launcher,
   upload, then slot 0, each verified by hash. Re-running with the same release
   is accepted; a different existing install is refused.
7. Plugin install (`src/launcher/plugin_installer.cpp`): the five files into
   `Plugins/.NbtManager.setup` (`.so` 0755, others 0644, verified), folder
   chmod 0755, sync, rename to `Plugins/NbtManager`.
8. Input-capture shim install (`src/storage/capture_install.cpp`, `NBTRLS2` only):
   verify `/opt/sys/etc/starter.cfg` (Environment 6 `System_HMI_PRELOAD` stock
   or with the shim of an older install, HmiMain3D's `<EnvFile/>`), save a
   verified backup to `state/manager/starter.cfg.orig` with `state/manager/capture.tx`,
   load a copy of the shim into the launcher's `--capabilities` with
   `LD_PRELOAD` and `LD_BIND_NOW=1` and refuse unless it exits 0
   (`src/storage/capture_probe.cpp`; nothing on `/fs/sda0` has changed yet),
   then drive it live through the launcher's `--capture-selftest` (capture on
   and off, heartbeat, the 5 s escape, a GUI that is gone; private files under
   `state/manager`) and refuse unless every step behaves as the build must,
   then write and verify the shim next to iDrive's environment file,
   `state/manager/libnbt-capture.so` (`0700`, on `/var/opt`, no remount),
   then, with `/fs/sda0` writable only for this, atomically set HmiMain3D's `<EnvFile/>` to
   `<EnvFile>/var/opt/nbt-manager/state/manager/hmi.env</EnvFile>` (an older
   install's Environment 6 entry goes back to stock in the same write;
   all other bytes, CRLF line endings, permissions and ownership preserved)
   and remove the folder dev91-dev103 kept the shim in
   (`/opt/hmi/ID5/data/ro/NbtCapture`, only when it holds nothing else).
   A different shim deletes the environment file and the shim's state
   before it is written; once verified, a new shim is armed (the file
   written, state `armed`) for iDrive's next start, and the manager keeps it
   from then on by itself (docs/hmi-restart.md: the shim deletes the file
   when it loads, the plugin's 60 s check writes it again).
   On downgrade to `NBTRLS1` or `nbt-manager-launcher --remove-capture`,
   the environment file is deleted first, then `starter.cfg` is restored and
   verified **before** deleting the shim (and the old folder). If
   `starter.cfg` has unrelated external edits, removal refuses auto-restore and
   retains the shim and backup.
9. Print `INSTALLED`.

### Capture shim recovery over SSH and firmware-update checks

- **Before a BMW firmware update, remove NBT Manager** (installer **Remove**,
  or `nbt-manager-launcher --remove-capture` over SSH) and install it again
  afterwards. The shim (`state/manager/libnbt-capture.so`) and the
  environment file that names it are side by side on `/var/opt`, so an update
  of `/opt` cannot remove one and keep the other; without the file, iDrive
  starts as stock whatever `starter.cfg` names.
- **Remove and older launchers:** the installer asks the launcher for
  `--remove-capture` only when a shim exists (the old folder or
  `state/manager/libnbt-capture.so`); a launcher from before NBTRLS2 does not
  know the mode. Deleting `/var/opt/nbt-manager` deletes the environment file
  too, so iDrive starts as stock even if `starter.cfg` still names it.
- **A missing `starter.cfg` backup** (an older installer's Remove deleted
  `/var/opt/nbt-manager`) is rebuilt from the current file when it holds the
  exact stock value plus our entry; anything else is refused.
- **Firmware updates overwriting `/opt/sys/etc/starter.cfg` or `/opt/hmi/ID5/data/ro`:**
  `SETUP` and `--refresh-setup` compare both `starter.cfg`'s capture entries and
  `state/manager/libnbt-capture.so` against the confirmed `NBTRLS2` release. If a
  BMW firmware update rewrote `starter.cfg` cleanly to stock (with a new stock
  layout differing from `state/manager/starter.cfg.orig`), verify/remove the old
  backup (`--remove-capture` when unmodified, or manual inspection of
  `state/manager/starter.cfg.orig` vs `/opt/sys/etc/starter.cfg`) before
  re-running setup so backups never lag behind firmware.
- **Emergency SSH recovery if iDrive fails to start with preload.** Only
  needed if the loader refuses the shim before it runs: the load test refuses
  that at install, and the shim cannot go missing without its environment
  file. Otherwise the shim deletes the file as it loads, so the next start is
  stock. This needs SSH while HmiMain3D is down; whether
  the installer's gateway activation can enable it then is **not
  established**.
  0. Quickest: `rm /var/opt/nbt-manager/state/manager/hmi.env` (or
     `nbt-manager-launcher --shim off`); iDrive's next start is stock.
  1. Remount read-write: `mount -uw /net/hu-omap/fs/sda0`
  2. Restore configuration before removing the shim:
     `/var/opt/nbt-manager/manager/nbt-manager-launcher --remove-capture`
     (or manually verify and copy `/var/opt/nbt-manager/state/manager/starter.cfg.orig`
     back over `/opt/sys/etc/starter.cfg` using a temp file and `mv`, check
     HmiMain3D's `<EnvFile/>`, then delete
     `/var/opt/nbt-manager/state/manager/libnbt-capture.so`).
  3. Remount read-only and restart HMI: `mount -ur /net/hu-omap/fs/sda0` and
     `OnOffDSICommander appreset`.

At run time the plugin starts `--open`; the launcher verifies the active slot
and starts the worker, then the GUI, each as NBT-AAD starts its daemon (QNX
`spawn()` with a descriptor map, the HMI's environment with AAD's `PATH`), and
gives each one end of their private channel. If either ends abnormally, the
launcher stops the other and restarts both. With the debug log on, the
launcher logs each step (slot verified, worker and GUI started with their pids,
GUI ready, exits and why) and every failure with its reason; the GUI logs its
start steps and why it closes, and the worker and GUI write their early errors
to the same log.
A new selection gets fresh restart attempts unless an update is on trial. An install session opens one packet-filter rule
through `/dev/pf` for its port (`src/worker/firewall.cpp`), removed when the
session ends and at worker start. `/etc/pf.conf` is never touched.

## Car facts (NBTevo_Y21432J, inventory of 2026-11-10)

The inventory is outside the repository (`~/src/reverse/nbt-inventory-5`).

- `/opt` is a link to `/fs/sda0/opt` (qnx6, read-only at boot); `/opt/hmi/data`
  and others link into `/opt/hmi/ID5`. `/var/opt` is on `/fs/sda1` (qnx6,
  writable).
- `/opt/hmi/ID5/data/ro` and `/var/opt` are `root:root 0775`;
  `Plugins` is `root:root 0755`; NBT-AAD's plugin files are `root:root 0644`.
- `OnOffDSICommander appreset` switches SSH off again (user, 2026-11-12):
  enable it again after the restart, as after a reboot.
- QNX 6.5 `mount`: `-u` remount, `-w`/`-r`. A remount without `-r` returns to
  read/write, which is why NBT-AAD's `mount -o remount rw` also works.

## Test history

| Kit | Result in the car |
| --- | --- |
| dev6 | SSH enabled and connected; install failed with no detail |
| dev8 | `printf: cannot execute` (exit 126): the host has no `printf` |
| dev9 | launcher refused the plugin folder: group-writable 0775 |
| dev12 | `HMI layout: plugin folder resolves to an unexpected path` (realpath guess) |
| dev13 | `invalid manager root or storage`: `/var/opt` is 0775 |
| dev14 | fixes both; not run |
| dev15 | adds the read-only remount and the `rm` rule; not run |
| dev16 | not shipped: its launcher still held test-hook names (`qnx-check`) |
| dev17 | not shipped: its GUI banner said dev16 (stale versioned object) |
| dev18 | first kit from the C++ `main`; superseded by dev19 before testing |
| dev19 | dev18 with every shipped QNX binary stripped; superseded by dev20 |
| dev20 | installs; restart iDrive fails (`OnOffDSICommander: cannot execute`, no `PATH`); the entry spawns the launcher but no window opens; Android Auto stops working until the manager is removed (cause unknown) |
| dev21 | absolute restart path; launcher started like NBT-AAD's daemon, its output in the debug log; superseded by dev22 before testing |
| dev22 | same as dev20: restart fails (`Could not load library libsys_dsi_servicebroker`), the entry does nothing (the plugin's focus event 50 never reaches its state), the launcher reports `manager GUI start failed` without a reason, Android Auto stops working until removal |
| dev23 | connector id 220 instead of NBT-AAD's 210; launcher failures name their cause; superseded by dev24 before testing |
| dev24 | dev23 with every command in a login shell on the unit SSH lands on, as typed in a session (no `on -f`) |
| dev25 | the installer disconnects after Restart iDrive; in the car: Android Auto works with the manager installed (connector id 220), restart works, the manager does not start: `manager GUI start failed (Bad file descriptor)` |
| dev26 | descriptors passed to the GUI and worker are no longer close-on-exec; in the car: the manager does not start, the first logged selection already reports the restart limit, without the failure behind it |
| dev27 | every hop started as NBT-AAD starts its daemon (`spawn()` with a descriptor map, HMI environment); launcher, GUI and worker log each step and failure; fresh restart attempts per selection; superseded by dev28 before testing |
| dev28 | the launcher starts the worker and then the GUI (the GUI no longer starts or restarts the worker); either one failing restarts both; in the car: the launcher, worker and GUI start, the GUI draws, reaches its apps screen and takes controller input, but nothing appears (no LayerManager surface); provisioning an app while the manager is open fails with `manager operation is in progress` |
| dev29 | the GUI's window is LayerManager surface 1290, put on layer 0x80000000 in place of iDrive's surface 256 as NBT-AAD shows Android Auto; iDrive put back on hide, close and by the launcher after any GUI exit; superseded by dev30 before testing |
| dev30 | checked against NBT-AAD and the dev28 log before the car: the entry opens the plugin's top-level state 15 before holding iDrive on 14 (dev28's hold never took, so a knob push also hit the list underneath); the GUI covers iDrive only once iDrive is held, only in place of iDrive alone (never over Android Auto), with a panel-sized window scaled ×2 by the GUI (no LayerManager scaling); hold files rewritten in place; the worker no longer holds the manager lock, so a manager update's trial can be confirmed |
| dev31 | an application the manager starts is shown: a `show` application gets surface 1291 and the GUI hands it the panel while iDrive stays held; the GUI steps aside when anything else (Android Auto) takes the layer, and takes knob input only while its surface is up; the app environment's one-row overflow fixed ; in the car: the manager shows, holds iDrive and takes the knob, but the test app's host fails (`cannot start status channel/application`) and the pairing screen has no QR code: qcc had ignored `-MMD`, so GUI objects built before a header change kept the old layout of UiController |
| dev32 | every QNX object rebuilt with real dependency files (qnx-check refuses stale ones); the host names the step that failed with errno and starts an application without its status channel when `/dev/shmem` refuses it; in the car: the QR code shows, the manager update's trial is confirmed, the test app (reinstalled) starts but never puts a window on surface 1291, the status file is refused (EPERM), and closing the manager with BACK restarted it hidden (the worker's exit was seen before the GUI's) |
| dev33 | applications get the HMI's PATH, library path, preload and graphics root (the test app had none, so the car's tools could not load), their output goes to the debug log; a child closing its heartbeat and lease just before exiting counts as an exit, so closing the manager no longer restarts it; the status file refusal logs what QNX reported; the installer explains launcher refusals behind the login profile's noise |
| dev34 | the install session listens on the Wi-Fi Direct group `wfd0` (dev32's link on the CarPlay hotspot timed out from a phone on Wi-Fi Direct); the status file is accepted on QNX `/dev/shmem`, whose objects all report 2 or 3 links; Install or update first shows a Wi-Fi QR code for that group (name `DIRECT-03-<device name>` from the CarPlay configs or Connectivity's persistency, passphrase decoded from `WFDPass.conf`, as NBT-AAD does), so a phone joins without Android Auto, then the install link |
| dev35 | Uninstall in every application's settings (hold its name): asked first with No focused, refused while the application runs or a session, installation or manager update is under way; the worker removes the application's profile first (it leaves the list), then its keys, executable, state and data, inside `/var/opt/nbt-manager` only, never following links; an interrupted uninstall completes when run again; the installer's "already installed" points to it; Restart iDrive waits 5 s, not 20 s, for an answer the reset never lets through, and its disconnect no longer waits on the dead link; in the car: the phone joined the Wi-Fi Direct group from the code (`DIRECT-03-BMW08722`, name from `iap2_Wifi.cfg`), opened the install link over `wfd0` and uploaded; packages verified and the review showed; the test app (installed over SSH) drew on the panel in red and blue; but every install failed at once ("cannot install verified package") and every uninstall was "busy": both need the manager operation lock, which the launcher holds while the manager runs |
| dev36 | the worker counts the operation lock as held when its holder is the launcher that started it (the launcher's hold already keeps manager updates and setup out); another holder still blocks installs and uninstalls, and a pending manager trial still does; an application handed the panel that ends (BACK in it) brings the manager's list back instead of a black panel: on the car its surface stayed on the layer, so the GUI now also watches `RUNNING` |
| dev37 | in the car: the phone install of v2 completed (health check, "Update installed"), BACK in the test app brought the manager back; but Android dropped the Wi-Fi Direct network for having no internet until told to keep it, the idle timer closed the manager a second after the review appeared (the user was on the phone for two minutes), the 30 s health check showed no progress, and the swap back from the app showed the held iDrive screen for about 0.6 s |
| dev38 | no idle close from the session to the installation's end; a bar fills over the 30 s health check; the manager's and the application's surfaces swap directly, never through iDrive; the Wi-Fi code screen says to stay connected when the phone reports no internet (the car's network cannot pass Android's internet check without changing the car's DNS) |
| dev39 | in the car (dev37): a manager update staged but never opened blocked the next one ("manager state requires recovery"), so the kit had to remove and reinstall the manager; staging now drops a candidate that never confirmed for the known-good slot first, and the installer names a launcher refusal instead of calling it a broken connection; Android drops the Wi-Fi Direct network on the first join from the code, and keeps it only after a manual reconnect and "stay connected": while a session listens the worker answered Android's connectivity check on the group's address; health check (applications, manager trial, boot guard) is 10 s instead of 30 s; in the car: the manager update and a new application's install from the phone worked, no frozen iDrive screen; the Wi-Fi did not change: the car's DHCP gives phones no DNS server or router, so the phone asked nothing (0 DNS, 0 HTTP) and dnsmasq kept port 53 (`EADDRINUSE`); joined from the camera the page timed out until a reconnect from the Wi-Fi list |
| dev41 | paired P-256 phones and signed /v1/ access replace bearer/browser uploads; Wi-Fi-only pairing QR, six digits and car confirmation; signed install/update/remove, settings without a car prompt; paired-phone settings; independent known-good boot door and conservative fresh-speed/ID6-home wake (off by default). Host and QNX checks only at this milestone; App 0.1 and in-car validation follow. The stable launcher must gain --door through the setup path before boot tests. |
| dev42 | dev41 with the signed phone app (App 0.1, versionCode 42) in `phone/`; not installed |
| dev43 | in the car (Pixel 6, 2026-11-14): Connect enabled SSH, the manager update was staged and its trial confirmed (`release 43 (trial)`, then `release 43`), but **Install** was refused: "existing HMI plugin conflicts with release", because the plugin from a kit before dev41 differs and setup never overwrites a different plugin; the car kept that plugin and launcher. The test app ran and returned to the manager; Install or update → Pair a phone showed the Wi-Fi code (`DIRECT-03-BMW08722`, name from `iap2_Wifi.cfg`) and timed out after 60 s. The app refused the code ("This is not the car's Wi-Fi code"; the camera app read it): it parsed expo-camera's `data`, which for a Wi-Fi QR is ML Kit's display value, not the `WIFI:` text. The busy dots flashed top right every second during the status polls of the waiting and pairing screens |
| dev44 | the app reads the barcode's raw text (app `246cf5c`); background polls no longer show the busy dots (`f0d2eca`); a car with a plugin from before dev41 needs Remove NBT Manager, then Install |
| dev45 | dev44 with the procedure's plugin note; in the car (Pixel 6, user's report): the app scanned the code, joined, paired (confirmed on the car), installed a package and read settings; Android dropped the car's network between actions because the app released it after each one, so the next join failed for 60 s and a setting change failed; later every join to the car's group failed, from the app, from Android's Wi-Fi settings with a fresh code and for Android Auto, until Restart iDrive (`WFDPass.conf` unchanged since boot); cause unknown: candidates are the many join and release cycles of the per-action design and the manager's own parts on `wfd0` |
| dev46 | the app holds the car's network while it is open (released in the background, after 5 min idle, or when the car is forgotten), retries a failed join once (app `975a0bb`); car side as dev45 |
| dev47 | the manager brings its stable launcher and iDrive plugin to the confirmed release (`SETUP`: launcher renamed over, plugin folders swapped with `/fs/sda0` remounted rw then ro) and asks to restart iDrive when the plugin changed; a paired phone can send a newer manager release, reviewed on the car and staged by the launcher at the next opening; the kit carries the release in `phone/`; installer **Update** no longer needs a removal to change the plugin; not yet run in the car |
| dev47 | in the car: Install refused the installed manager, Update staged it, Remove and Install followed before an opening (the in-manager refresh never ran); phone pairing, list and `test-v2` install worked; a dev47 manager release from the phone was refused as not newer (the app showed "malformed"); opening from the phone did not open the manager, and opening it by hand afterwards froze iDrive until its watchdog restarted it (unlogged) |
| dev48 | installer **Update** replaces the launcher and iDrive plugin at once (`--refresh-setup`); opening from the phone off (plugin answers every wake `off`, no car-data subscriptions, setting hidden); the app shows the car's reason for a rejected package |
| dev49 | dev48 with the app joining the car's Wi-Fi when the car's page opens and showing the connection (app `7a2da43`); kit SHA-256 `9d96a930…6bbe` |
| dev50 | pairing under Settings, no Install or update, the pairing code drawn large; not used |
| dev51 | dev50 with the installer's Update asking for an iDrive restart only when the plugin's checksums changed; plugin identical to dev48's |
| dev52 | dev51 with the installer's gateway search asking from every local address and a Gateway field in Connection settings (`cfdd09e`), for a car that kept a Wi-Fi ENET adapter's address; kit SHA-256 `2af84233…02c7` |
| dev53 | dev52 with the installer finding the head unit on the car's own network when the built-in address does not answer (`388bf24`), 60 s for login and the QNX probe over a slow bridged link (`936e0c1`), and the unreachable-address advice (`4e59de7`); kit SHA-256 `b1b5383f…6e94d` |
| dev54 | dev53 with the installer split into `desktop/core`, the phone library `desktop/mobile`, and the app's Install on a car (`ad18bb5`…`b9e67b3`); APK versionCode 54 carries `libnbt_installer.so` with this release; kit SHA-256 `d1a45b6c…74fc` |
| dev55 | dev54 with the installer skipping the built-in address when this device has no `169.254` address (a phone, or behind the adapter) and, after enabling SSH, searching the gateway's network for the SSH port every 2 s for up to 60 s (`2df8bbc`); cable path unchanged; APK versionCode 55; kit SHA-256 `045e55d1…6893` |
| dev56 | dev55 with the installer asking **Restart iDrive?** in a modal after an install and after an update that replaced the iDrive plugin (`49511a9`, core `Event::RestartNeeded`), the phone app asking the same, and the phone app's text cut to instructions and short outcomes (app `ae8e2a6`, `b670599`); Rust formatted with `cargo fmt` (`216af22`); APK versionCode 56; kit SHA-256 `c04f0db4…02aa` |
| dev57 | built, not distributed (superseded by dev58 the same day) |
| dev58 | dev56 with an always-on log per application (`state/<app>/output.log`: its stdout and stderr, how it started and ended) and `POST /v1/logs/app`, `/v1/logs/system` (`sloginfo`), `/v1/logs/clear` for paired phones (`4178337`, `0507616`); the app shows, shares and clears them, and its logs scroll (app `dba65b1`, `eb5f956`); APK versionCode 58; kit SHA-256 `9f030995…02f5` |
| dev59 | dev58 with the application log synced to flash (notes at once, output within a second; `16ecac5`), which an iDrive restart lost in dev58, and the app saying on the page why a log could not be read (`70e3fa9`); APK versionCode 59; kit SHA-256 `86bb3072…7ad2` |
| dev60 | dev59 with the app's pairing going first instead of waiting silently behind other actions (app `633b64f`); built, superseded the same day by dev61 |
| dev61 | dev60 with application-log read and write failures reported (`c739a00`); in the car: "No log yet" for every application, the activation's relay error (`070c1ac`), an update launch freeze (`docs/car-issues-2026-11-16.md`) |
| dev62 | dev61 with Wi-Fi Direct prepared over MOST `A0.03` for Pair a phone (`65412c3`), sandboxed applications and fault quarantine (`663008f`); in the car: Pair a phone closed the manager (GUI heartbeat lost while the worker prepared Wi-Fi Direct) |
| dev63 | built, not distributed: the install health check's run in the application log (`7519a2a`), without the dev62 pairing fix |
| dev64 | dev62 with the install health check's run in the application log (`7519a2a`) and the GUI's heartbeat kept while a worker request waits (`97e18e8`), the dev62 pairing fix; APK versionCode 64; kit SHA-256 `079e5070…1589` |
| dev65 | built, not distributed: dev64 with the GUI's worker requests (`3f43393`) and the Wi-Fi Direct preparation (`3509641`) on their own threads |
| dev66 | dev65 with the GUI paced to 30 fps (`92ae5d6`); APK versionCode 66; kit SHA-256 `ba2dd01c…8c69`; in the car: the manager stayed open through Pair a phone and its slow-frame lines showed one frame per 10 s at most, the slowest 38 ms; but Pair a phone said there was no Wi-Fi: the car brought the Wi-Fi Direct group up 2 s after `SelectWLANState3(1)` and reported WLANState3 = 1, the worker read neither as an answer (ResultAck and ProcessingAck OpTypes swapped), timed out after 10 s and switched the group off |
| dev67 | dev66 with the MOST method report OpTypes corrected and a WLANState3 report of the requested state accepted as the switch's answer (`227e4df`); APK versionCode 67; kit SHA-256 `4a60967b…1f29`; in the car (`debug-1794850561.log`, `slog-1794850564.log`): Pair a phone works on the car: the group came up 2 s after the request (`DIRECT-03-BMW08722`, key from MOST WLANConfiguration) and the code showed twice (51 s, 60 s); the phone could not join ("Could not join the car's Wi-Fi"): the Wi-Fi driver logged the phone associating and disconnecting 4 to 8 s later, five times; whether the WPA handshake or DHCP failed is not in the car's log |
| dev68 | built (`0a59018`: the group stays on when NBT-AAD uses it), not used: superseded by dev69 |
| dev69 | dev67 with the Wi-Fi Direct group left on when NBT-AAD uses it (`0a59018`) and debug-log checks for a phone that cannot join: MOST passphrase equal to `WFDPass.conf`'s or not, `dhcpd` for `wfd0` running, leases at the start and end of the session (`4bc73de`); APK versionCode 69; kit SHA-256 `1a66b751…6701`; in the car (`debug-1794851767.log`, `slog-1794851769.log`): `MOST passphrase differs from WFDPass.conf; dhcpd for wfd0 running; leases 0` at the start and end; the phone associated and dropped eight times; Install test app refused with "manager operation is in progress" |
| dev70 | dev69 with `WFDPass.conf`'s passphrase in the Wi-Fi code, MOST's only as a fallback (`4602e28`), and Install test app explaining both refusals: App Manager open, or a manager update not yet confirmed (`ce08356`); APK versionCode 70; kit SHA-256 `cd681f87…3b22`; in the car (`debug-1794852944.log`, `slog-1794852947.log`, `installer-1794852713.log`): **pairing works** (the phone joins the group with `WFDPass.conf`'s passphrase); no application starts: `START` fails within 14 ms, no host, no application log (QNX 6.5's `fork()` returns ENOSYS in a process with threads: verified in its libc and a QNX 6.5 VM) |
| dev71 | dev70 with applications started by `spawn()` on QNX: the worker spawns the application host (`48f8f07`; it forked twice) and the host spawns the application (`ad21b23`; 663008f's sandbox forked); a failed start logs its step and errno; APK versionCode 71; kit SHA-256 `58f5bf20…c2c9`; in the car (`idrive.log`, user's report): **applications start**, the test app works; app logs stay empty (`application log open failed (errno 89)`, see dev74); sending the installed test app again fails at install as "release sequence rejected"; Doom crashes in its first frame (`run fault ... signal 11 ... ip 0x10d756c ... apps/doom/bin/app`, outside its code, right after its first `eglSwapBuffers`) and the panel stays black (first try: back to the manager) |
| dev72 | built (`6b15384`), not used: superseded by dev73 |
| dev73 | dev71 with a package that is not newer refused on arrival, naming both versions (`6b15384`), and the GUI back on the panel when the application's window is gone though its surface stays in the render order (`47e9ca2`: Doom's crash left the panel black); APK versionCode 73; kit SHA-256 `0d8b6a38…d919`; in the car (`idrive (2).log`, from the phone): the manager comes back after Doom's crash (iDrive restored at 1:08, the manager at 1:09); Doom still crashes in its first frame (`ip 0x121c56c`); the app log reads empty on the phone |
| dev74 | dev73 with the manager's debug log readable from the phone: **Manager log** on the car's page, `POST /v1/logs/manager` (`10dc76a`, app `571e501`); APK versionCode 74 (install it: the app changed); kit SHA-256 `437f5718…8414`; in the car (`manager.log`, `idrive (3).log`, both from the phone): the Manager log works; every application start logs `host <app>: application log open failed (errno 89)` (ENOSYS in QNX 6.5's `errno.h`), so the phone reads 0 bytes; Doom exits 0.5 s after start (no fault in the iDrive log) and is then "blocked after faults" |
| dev75 | dev74 with the application log's open failure explained in the debug log (path, `lstat`, a plain append `open`) and the application's exit status, signal and fault logged there too (`dd37022`); APK versionCode 75; kit SHA-256 `f47f2701…db0e7` |
| dev76 | dev75 with the application log written even when the checked open fails: plain opens follow (append, then write or read-write with a seek to the end), each accepted only for a regular file of the manager's owner, and the debug log names the one that worked (`0a36e29`); `openOwnedFile`'s identity check sets ESTALE instead of reporting a stale errno; APK versionCode 76; kit SHA-256 `acfe273e…8bca` |
| dev77 | built (`ca9389d`), not published: its APK predates `087b7d4` |
| dev78 | built (`087b7d4` in its APK), not published: superseded by dev79 |
| dev79 | dev76 with resource limits for applications on the car through the `--app-exec` wrapper (no core file, 128 files, 256 MiB data, 64 threads; `ed649d0`); no stale errno in the logs (`0c97d09`); verification, install copies and SETUP on the worker's job thread, `/v1/upload` answering `waiting` "Checking the package" and each step's duration in the debug log (`ca9389d`); the app says "The car is checking the package." (`087b7d4`); the debug log says where the manager's root is and how much space it has (`a706228`); APK versionCode 79; kit SHA-256 `253c192f…ef11` |
| dev80 | dev79 with the app keeping the car's network 2 minutes in the background (`e8bd018`) and a SETUP on trial logged as such (`8a16468`). dev79 in the car: application logs work, apps run under the limits, `/var/opt/nbt-manager` is on qnx6 `sda0t178` at `/fs/sda1` (380 of 491 MiB free), Doom 1.0.5 crashes at start in a shared library after its sound fails to load (`liba2i_client.so` not on the HMI library path); APK versionCode 80; kit SHA-256 `484e4733…e614` |
| dev81 | dev80 with no restart loop when LayerManager does not answer: the GUI that gets no panel size ends (status 0) instead of being restarted with iDrive held each time, and keeps its heartbeats while a LayerManagerControl call waits; applications' `LD_LIBRARY_PATH` ends with `/opt/mm/lib`, where `libasound` finds `liba2i_client.so`. dev79 in the car (`idrive.log`): 33 s after a boot LayerManager answered nothing for 28 s, the GUI was started 4 times (iDrive's loading animation each time) and iDrive's own processes missed watchdogs; Doom 1.0.5 runs with sound off; APK versionCode 81; kit SHA-256 `b6880fe0…23b7` |
| dev82–86 | built in another session (gamepad bridge, volume display, DSI exit fix, up to `6c2c7a8`); release 83 ran in the car on 2026-11-17 and froze iDrive at an open: the plugin's focus event 50 never returned ([car-issues-2026-11-17.md](car-issues-2026-11-17.md)) |
| dev87 | dev86 with focus events sent from a thread of the plugin's own, so the plugin's iDrive thread never waits on iDrive (`triggerEvent` waits for iDrive's main thread with no timeout); an event not answered in 3 s drops the hold and sends the GUI aside; a snapshot of every HmiMain3D thread (state, blocked object and its owner word, CPU time, stack code addresses) in the debug log at 3 s and 10 s; each focus event's time logged. A mitigation, not a proven fix: it helps only if the freeze is a cycle through the plugin's thread; APK versionCode 87; kit SHA-256 `e7e36d4c…b9b7` |
| dev88 | dev87 with an application paused when iDrive takes the screen from it (NAV, MENU, OPTION, ...): until now it kept running behind iDrive, reading the controller and playing sound (the user, 2026-11-17). Its host stops it (`SIGSTOP`), drops its MOST and gamepad input, continues it when it is selected again in App Manager, and stops it after 2 minutes paused; built (`4e5f592`), not published: superseded by dev89 |
| dev89 | dev88 with iDrive held again as soon as it reaches the parent state, from the plugin's focus callback, instead of at the next poll (up to 250 ms): in the manager log of 2026-11-17 BACK (which iDrive takes 0.1 to 0.5 s after the press) left the blank state, and a second BACK or knob turn in that window took iDrive out of the manager; the last 48 raw controller messages written to the debug log when iDrive takes the screen (`controller messages before iDrive took the screen`); APK versionCode 89; kit SHA-256 `5852b60d…8145` |
| dev90 | `NBTRLS1` compatibility release built with dev91 (same code, no capture shim), for a launcher older than `NBTRLS2`; in the dev91 kit's `phone/` and embedded in its installer |
| dev91 | **Not to be installed** (the user, 2026-11-18): superseded by dev93, which test-loads the shim first. dev89 with the input-capture shim, observer build (`NBTRLS2`, `/opt/hmi/ID5/data/ro/NbtCapture/libnbt-capture.so` SHA-256 `58b4abcf…9039`, added to `starter.cfg` Environment 6 by Install and Update; it forwards every message), the capture review fixes (`2379c5f`) and the gamepad fix (`7a5f0ae`: every silent stop logged, the car's `/lib/libusbdi.so.2` loaded, `ENOBUFS` tolerated); the app's navigation, layout and theme changes (`2c9c268`); APK versionCode 91; release SHA-256 `36ae64ef…1658`; kit SHA-256 `273b2898…7053` |
| dev92 | `NBTRLS1` compatibility release built with dev93 (same code, no capture shim), for a launcher older than `NBTRLS2`; in the dev93 kit's `phone/` and embedded in its installer |
| dev93 | **Not to be installed**: superseded by dev95, whose load test uses iDrive's environment and preload order. dev91 with the capture shim test-loaded before `starter.cfg` names it (`70f1722`: the installed launcher's `--capabilities` with the shim as `LD_PRELOAD` and `LD_BIND_NOW=1` must exit 0, or nothing in iDrive's start changes); same observer shim `58b4abcf…9039`; APK versionCode 93; release SHA-256 `763c4438…96b4`; kit SHA-256 `4820ca07…e807` |
| dev94 | `NBTRLS1` compatibility release built with dev95 (same code, no capture shim), for a launcher older than `NBTRLS2` |
| dev95 | **Not to be installed**: superseded by dev97 (live test of the shim). dev93 with the load test in HmiMain3D's environment from `starter.cfg` and the shim after BMW's preload list in iDrive's order (`a106024`; fallbacks: BMW's list bound lazily, then the shim alone; the variant is logged); `car_inventory.sh` records `/etc/inetd.conf`; same observer shim `58b4abcf…9039`; APK versionCode 95; release SHA-256 `b44ddc2f…c2f9`; kit SHA-256 `d849241e…e471` |
| dev96 | `NBTRLS1` compatibility release built with dev97 (same code, no capture shim), for a launcher older than `NBTRLS2` |
| dev97 | **Not to be installed**: superseded by dev99 (iDrive-only restart). dev95 with the shim's behaviour tested live during the install (`f262c28`: the launcher's `--capture-selftest` drives the shim through capture off and on, another client, heartbeat expiry, switched off, the 5 s escape and a GUI that is gone; logged as `live test OK observer`); observer shim `de43c44f…6976` (new: the self-test entry); APK versionCode 97; release SHA-256 `ff803456…1936`; kit SHA-256 `b312efa2…4aaa` |
| dev98 | `NBTRLS1` compatibility release built with dev99 (same code, no capture shim), for a launcher older than `NBTRLS2` |
| dev99 | **Not to be installed**: superseded by dev101 (the shim through iDrive's environment file, one start at a time). dev97 with `nbt-manager-launcher --restart-hmi` and the Windows installer's **Restart iDrive only (experimental)** (`4af9054`: package 14 `HMI` stopped and run again through `/dev/starter/start`; `docs/hmi-restart.md`); `car_inventory.sh` saves the process manager's status; same observer shim `de43c44f…6976`; APK versionCode 99; release SHA-256 `a3343d3e…5bfe`; kit SHA-256 `d912fb1c…8061` |
| dev100 | `NBTRLS1` compatibility release built with dev101 (same code, no capture shim), for a launcher older than `NBTRLS2` |
| dev101 | **Not to be installed**: superseded by dev103 (the shim tested and kept automatically). dev99 with the capture shim loaded through iDrive's environment file (`9e53331`, docs/hmi-restart.md): Install and Update set HmiMain3D's `<EnvFile/>` in `starter.cfg` to `state/manager/hmi.env` and leave Environment 6 stock (an Environment 6 install of dev91-dev99 is moved; Remove restores stock); the file is not created, so iDrive starts as stock; the Windows installer's Test tools **Try the shim (restarts iDrive)** (`--try-shim`: the file exists only around one start, then iDrive is checked for the mapped shim and 20 s of running, and started again without it otherwise), **Keep the shim on** (only for the shim that passed a trial) and **Turn the shim off**; same observer shim `de43c44f…6976`; APK versionCode 101; release SHA-256 `6e272180…5970`; kit SHA-256 `f3ea0144…9e86` |
| dev102 | `NBTRLS1` compatibility release built with dev103 (same code, no capture shim), for a launcher older than `NBTRLS2` |
| dev103 | **Not to be installed**: superseded by dev105 (the shim next to its environment file). dev101 with the shim tested and kept automatically (`55ce89f`, docs/hmi-restart.md): the shim deletes iDrive's environment file when it loads into iDrive (one start per arming); Install and Update arm a new shim; 60 s after iDrive starts the plugin runs `--shim-check`, which arms it again when iDrive has it mapped, retries once after a start that ended before 60 s and then turns it off; installer Test tools **Try the shim**, **Shim on**, **Shim off**; observer shim `c05e6804…7c49`; APK versionCode 103; release SHA-256 `02e3a8b8…4ad4`; kit SHA-256 `145ac4bb…969c` |
| dev104 | `NBTRLS1` compatibility release built with dev105 (same code, no capture shim), for a launcher older than `NBTRLS2` |
| dev105 | **Not to be installed**: superseded by dev107. In the car (2026-11-18): the shim's live test refused it at Update and at setup ("FAILED step 2 (on, knob): forwarded, record 0"), the refusal also stopped the launcher and plugin update; `--restart-hmi` left HmiMain3D running (`<Shutdown>IGNORE</Shutdown>`); gamepads: `service socket flags failed (errno 0)`, and the media launcher reset each pad, removed 2 s after insertion. dev103 with the shim next to iDrive's environment file (`3dad8e1`, docs/hmi-restart.md): `state/manager/libnbt-capture.so` instead of `/opt/hmi/ID5/data/ro/NbtCapture` (moved by Update, removed by Remove), so an update of `/opt` cannot leave an armed file naming a missing shim; `/fs/sda0` remounted only for `starter.cfg`; the Windows installer's Update tries the shim at once while connected and asks "Restart iDrive?" only when nothing restarted; same observer shim `c05e6804…7c49`; APK versionCode 105; release SHA-256 `6ea998e2…5e8e`; kit SHA-256 `a1089391…1473` |
| dev106 | `NBTRLS1` compatibility release built with dev107 (same code, no capture shim), for a launcher older than `NBTRLS2` |
| dev107 | **Not to be installed**: superseded by dev109. In the car (2026-11-18): the shim installed, loaded into iDrive and kept after 60 s (observer, nothing blocked); gamepads seen with every interface but refused (`has no matching interface`). dev105 with the shim's live test in `/dev/shmem` (`391675f`), a refused shim no longer stopping the rest of the update (the installer names it), the gamepad service's descriptor flags accepted on any result but -1 and read back, no iDrive-only restart or trial in the installer, controller settings folded, `/etc` configuration in the inventory; same observer shim `c05e6804…7c49`; APK versionCode 107; release SHA-256 `3165bf41…f006`; kit SHA-256 `c34303d5…8307` |
| dev108 | `NBTRLS1` compatibility release built with dev109 (same code, no capture shim), for a launcher older than `NBTRLS2` |
| dev109 | Superseded by dev111. dev107 with gamepad interfaces read one by one with `usbd_interface_descriptor` and logged (`5a1ff0c`), and "Restart the head unit" (asked first) wherever "Restart iDrive" ran `OnOffDSICommander appreset` (installer, phone app, the car's screen); same observer shim `c05e6804…7c49`; APK versionCode 109; release SHA-256 `ceec4d1c…a977`; kit SHA-256 `285edf0e…cba5` |
| dev110 | `NBTRLS1` compatibility release built with dev111 (same code, no capture shim), for a launcher older than `NBTRLS2` |
| dev111 | Superseded by dev113. In the car (2026-11-18): the shim held the knob and keys in the manager, Doom and the test application, except in the first capture, which escaped at its first message (a stale BACK hold), so the first OPTION reached iDrive; the DualShock 4 worked, its light bar stayed in its charging colour; the Xbox pad was ignored (taken for a failed PS4 pad on the same port). dev109 with the **active** capture shim (`2a607c4`, docs/input-capture.md section 6): while the manager or an application is shown, iDrive gets none of the knob, the controller keys or the favourite keys (FBlock 0x15, 0xC02), presses and releases alike; volume untouched; the install's live test holds a favourite key too and logs "live test OK active". Bridge 1.4 adds `controller` (`NBT_CONTROLLER_FD=8`, decoded states from the host, docs/app-bridge.md section 8); Doom 1.0.16 (`doom-1.0.16.nbt` on the NAS, SHA-256 `67ae3932…2522`) needs it and no longer reads MOST. Active shim `a472a99e…ef17`; APK versionCode 111; release SHA-256 `48c8914b…829c`; kit SHA-256 `3fb27fd8…d098` |
| dev112 | `NBTRLS1` compatibility release built with dev113 (same code, no capture shim), for a launcher older than `NBTRLS2` |
| dev113 | Superseded by dev114 (same manager code). dev111 with the shim's BACK hold dropped when capture is off or its generation changes (`b82e12f`, docs/input-capture.md), failed gamepads retried after 2 s doubling to 60 s and told apart by vendor and product, and the DualShock 4's light bar set blue (docs/gamepad-usb-research.md); same Doom 1.0.16; active shim `75bf8a16…c423`; APK versionCode 113; release SHA-256 `5c6a423c…b24b`; kit SHA-256 `c96db9d7…d498` |
| dev114 | Superseded by dev115. dev113's manager with **one release per kit**: the installers' Update (Windows and the phone app) uploads the release's launcher and runs it for `--stage-update` and `--refresh-setup` (`50a2c84`), so no `NBTRLS1` compatibility release is built or carried; Update on a head unit without NBT Manager says to use Install. Same active shim `75bf8a16…c423`; APK versionCode 114; release SHA-256 `b3f7a16c…988b`; kit SHA-256 `8e86fe70…f8ea` |
| dev115 | Superseded by dev116 (same manager code). The first kit from `make kit` (`8733861`). Capture diagnostics (`651dd00`): records carry the FBlock, an escape its BACK hold time, a dropped BACK hold its own record; the plugin re-logs the shim's state and replays its ring into every new debug log file; the manager's list says when iDrive gets the keys too. Gamepads (`0f9bbe9`): the GUI is the one USB owner for its whole life and publishes the pad state; application hosts relay it and never open USB. With Doom 1.0.17 (`doom-1.0.17.nbt`, SHA-256 `a9f0e857…1675`: a Volume setting down to 15 %). Active shim `95120064…56fc`; APK versionCode 115; release SHA-256 `2fc4f7d8…1aca`; kit SHA-256 `cd4f1d44…1c6d` |
| dev116 | dev115's manager; the kit's `usb/car_inventory.sh` also saves `audioconn -l` (`97dd490`), the car's audio connections, to look for a source without the browser source's volume floor of 25 (NBT-AAD's native audio has the same floor). With Doom 1.0.18 (`doom-1.0.18.nbt`, SHA-256 `64b00722…a47a`: no Volume setting, Doom's own Options volume instead; an `audio-conn` file in its data folder names another connection to try). Active shim `95120064…56fc`; APK versionCode 116; release SHA-256 `3b978d65…fd54`; kit SHA-256 `1f251c43…db13` |
| dev117 | dev116 with the installers' **Collect everything** (Windows Test tools, the app's More tools; `e2cec21`, app `ec0f868`): one read-only report over SSH, `collect-<time>.txt`, with both debug log files, `sloginfo`, `audioconn -l` (stopped after 10 s), `pidin arg`, the process manager's overview, the shim's state and files, and listings of the manager's folders and `/dev/shmem`. The manager's Settings > **Status** and **Test the gamepad** (`7a2c996`): whether iDrive gets the keys, the MOST controller input, the gamepad (the open pad, or why none: the USB held elsewhere, `usbd_connect`'s error, or the step a plugged-in pad failed at) and the last error; live buttons, sticks and triggers. The plugin's launcher start in its own file (`c17311c`, no change in behaviour). Doom 1.0.18 still current. Active shim `95120064…56fc` (unchanged since dev115); APK versionCode 117; release SHA-256 `ddd5136c…424c`; kit SHA-256 `16f70f75…5c0c` |
| dev40 | the connectivity answers are removed (nothing on this car's network can reach them); the Wi-Fi code screen says to scan from the phone's Wi-Fi settings, not the camera: Android makes the network the default only when Settings joined it and the user chose to stay connected |
| dev38 (cont.) | two keys: the manager key signs manager releases, one apps key (the existing test-app key) signs every application package and is bundled with the manager, compiled into its binaries; a phone package for an application not installed here installs it after approval ("Review new application"); the review shows the application and its versions only (no SHA-256 or sequence); long reasons wrap on two lines; after a result the list is read again; a generic session starts with no application selected (a second application in one manager session was refused) |

Kits are made with `make kit` (since dev115, `tools/make_kit.py`): from a
clean commit only, it runs `qnx-check stack-report` in the QNX 6.5
environment, builds the kit with the capture shim, copies the zip and the
unzipped folder to the exchange folder (`NBT_KIT_DIR`, default the NAS's
`/mnt/nas/public/nbt`; it never replaces either), checks every file there
against `SHA256SUMS.txt`, and prints the facts for the kit's row here
(`KIT_ARGS=--no-copy` builds and checks only).

dev15 was the last kit built from the C code. The kit build now runs
`qnx-check` before signing, and the GUI is rebuilt when the version changes.
Shipped QNX binaries are stripped; the unstripped links stay in
`build/qnx-unstripped/` to resolve the raw addresses in crash records. Never validated in the car: a complete install,
the remounts, the firewall rule, and everything after installation.

## Building, testing, debugging

- Kit: source `/home/mobyfab/src/tools/qnx-6.5-tools/env.sh`, set
  `QNX_SCREEN_LIBDIR` and `RHMI_LIBDIR`, run `make windows-kit` (see README).
  Each build takes the next `dev<sequence>` from the keys directory. The user
  collects kits from `/mnt/nas/public/nbt/`.
- Host tests: `make test`; installer: `cd desktop && cargo test --locked`,
  plus the end-to-end `native_bootstrap_over_ssh` test (desktop README).
  Launcher refusals are covered in `tests/test_manager_stage.py`.
- In the car: tick **Debug log** in the installer. Its log names every remote
  command with exit status and stderr; the user puts it on
  `/mnt/nas/public/nbt/installer-<time>.log`. The head unit's own debug log
  (`manager/debug.log`) is switched on and fetched from Test tools; after a
  run, **Collect everything** saves it with the system log, the audio
  connections and the process and file listings in one file
  (`collect-<time>.txt`).
