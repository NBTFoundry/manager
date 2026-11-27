<!-- SPDX-License-Identifier: MPL-2.0 -->
# Bench checklist

The order in which to validate NBT Manager on a bench unit: each step relies
on the ones before it. Record the
firmware version, the unit, the date and the result of every step. Stop at the
first failure; a later step's result means little when an earlier one failed.

Destructive steps (power loss, interrupted updates) run only on a bench unit or
isolated storage, never on a car someone relies on.

## Debug log

The GUI, the worker and the iDrive plugin write to one debug log when, and only
when, `/var/opt/nbt-manager/manager/debug.log` exists:

```sh
touch /var/opt/nbt-manager/manager/debug.log
chmod 600 /var/opt/nbt-manager/manager/debug.log   # not group or world writable
tail -f /var/opt/nbt-manager/manager/debug.log     # or copy it off the unit
rm /var/opt/nbt-manager/manager/debug.log           # switches logging off again
```

- Create it as the owner of `/var/opt/nbt-manager`. A file with the wrong owner,
  mode, type or link count is ignored.
- Lines look like `812.345 gui[4711] screen apps -> settings`: seconds since boot,
  component (`gui`, `worker`, `plugin`), process id, message.
- The GUI and worker open the log when they start. The plugin checks for it each
  time the menu entry is selected.
- At 128 KiB the file moves to `debug.log.1`, replacing the previous one, so at
  most about 256 KiB is kept.
- The log never contains the pairing token, a package digest being approved or
  package contents. It does contain application names, versions, worker
  messages, the session address and the Host header of refused HTTP requests.
- The plugin also reports errors to the system log (`sloginfo | grep NbtManager`).

Reset between runs with `rm debug.log debug.log.1; touch debug.log; chmod 600 debug.log`.

## 1. Platform

| Check | Expected |
| --- | --- |
| `ls -ld /var/opt/nbt-manager` after setup | Directory owned by the manager's user, mode `700`, on a `qnx6` filesystem mounted read-write and executable |
| `ls -l /dev/urandom` | Present and readable |
| `ls -l /lib/libstdc++.so.6 /usr/lib/libstdc++.so.6` | A readable GNU C++ runtime is present; record its version and checksum with `tools/car_inventory.sh` |
| Start the manager, then `pidin mem` | The manager starts and its GUI/worker mappings include `libstdc++.so.6`; stop if it is missing or incompatible |
| `ifconfig uap0` (and `wfd0`) with a phone connected to the head unit's Wi-Fi | An IPv4 address; note it |
| Screen display size and format | The GUI assumes a 640x240 RGBX8888 window; note the real size |

## 2. Plugin and launch

| Action | Expected | Log |
| --- | --- | --- |
| Boot with the plugin installed | "App Manager" entry in the iDrive menu; no HMI crash | — |
| Select the entry | The manager window appears within a few seconds | `plugin ... entry selected; manager not running`, `plugin ... hold phase 1: spawn hold`, `gui ... start slot 0`, `gui ... worker started`, `worker ... start` |
| Select the entry again while it is shown | No second window | `plugin ... entry selected; manager running` |
| `pidin` | One launcher, one GUI (`gui`) and one worker (`worker`) from the active slot | — |
| Boot, wait a minute (debug log on) | The plugin reads iDrive's language (car data `vehicle.language`); iDrive stays responsive | `plugin ... iDrive language <n>`: record `n` with iDrive's language setting |
| Select the entry | The manager shows iDrive's language when it has it (English, French, German, Italian, Spanish, Chinese, Japanese), English otherwise | `gui ... language auto, iDrive <n>, shown <code>` |
| Change iDrive's language, with the manager closed and then open | The manager follows (open: within two seconds) | `plugin ... iDrive language <m>`, `gui ... iDrive language <m>; shown <code>` |
| Settings > Language, a language; reopen; then Automatic | Shown at once and kept; Automatic follows iDrive again | `gui ... language <code>` |
| No `iDrive language` line after two minutes | Record it: car data did not answer. Automatic shows English; a language chosen in Settings > Language still applies | — |
| Open the manager (debug log on) | BMW: the selected row filled red on a black page; MINI: a solid blue bar; Supra: an orange-red glow line. Compare with iDrive's own cursor and record a mismatch | `gui ... theme auto, car <bmw\|mini\|supra\|unknown>, shown <code>`: record the make and whether `carplay.ini` exists |
| Settings > Theme, another make; then Automatic | Shown within a second and kept; Automatic gives the car's make back; the phone app follows after its next connection | `gui ... theme <code>, car ..., shown <code>` |
| `ls -l /dev/shmem/nbt-manager.language` | Owned by the HMI's user, mode `600` | — |

## 3. Controller input

| Action | Expected | Log |
| --- | --- | --- |
| Open the manager | Input works, no preview countdown | `gui ... controller config absent; defaults`, `gui ... controller input started`, `gui ... MOST attached; registering client`, `gui ... MOST client <n>; controls registered for 7 instances` |
| (same opening) | The FBlocks present in the car are logged once; keep these lines, they tell which MOST functions in `docs/most-functions.tsv` this car can answer | `gui ... MOST registry: <n> entries, FBlock key/first word in hex`, then `MOST registry 1-16: E501/xxxx ...` lines. None at all means the Registry attribute did not arrive (note it) |
| Rotate one detent each way | Focus moves down (clockwise) and up | `input down`, `input up` |
| Push, tilt up/down, tilt left, BACK | Select, up/down, back, back | `input select`, `input up`/`down`, `input back` |
| MENU | The manager closes; iDrive's main menu opens | `input menu`, `gui ... exit (closed by the user), status 0` |
| Older CCS controller (if present) | Same mapping | same |
| Wrong controller or keys | Find the instance and key slots (NBT-AAD's `NBT_MOST_LOG` capture shows them) and set them in `manager/controller.conf` (see `config/controller.conf.example`) | `controller config applied` |
| Invalid `controller.conf` | Defaults apply; the application list shows "Controller config invalid; defaults used" | `controller config invalid; defaults` |
| `DSI library not loaded` or `symbols missing` | The GUI is only a preview: record the library path and firmware | — |
| Confirm the loaded library is `libsys_dsi_colibry.gpp.so.8`, then use the controller | The `.gpp` DSI ABI loads, registration succeeds and every mapped input works | `gui ... controller input started`, `gui ... MOST attached; registering client`, input lines above |

## 4. iDrive focus hold

| Action | Expected | Log |
| --- | --- | --- |
| Turn and push the knob in the manager | Nothing happens in iDrive underneath | `plugin ... blank state focus 1` |
| BACK on the manager's top screen | The manager handles it; iDrive stays on the blank state | `plugin ... parent state focus 1`, then `hold` |
| NAV, MEDIA, a Check Control warning, reverse camera | The manager closes (or hides during an installation) and iDrive shows its screen | `plugin ... aside`, `gui ... iDrive signal aside; closing` |
| Same during an installation, then select the entry | The window hides, then comes back | `gui ... iDrive signal aside`, `plugin ... show`, `gui ... iDrive signal show` |
| Leave the manager by any means | iDrive returns to the list holding the entry | `plugin ... release` |
| Open a popup, then select the entry | At most four retries, then iDrive is left alone | repeated `hold` |

With an application declaring `hold`, `car-data` and `net` (Android Auto,
[app-bridge.md](app-bridge.md) components 5-7), the manager closed:

| Action | Expected | Log |
| --- | --- | --- |
| The application asks for the hold (Android Auto on screen) | iDrive goes to the blank state; the knob does nothing in iDrive underneath | `host <app>: hold request hold`, `plugin ... application hold <app>: open hold`, then `held` |
| Touch a favourite key (`peek`) | iDrive's favourites bar shows over the application; nothing is re-asserted | `host <app>: hold request peek` |
| MENU, NAV, reverse, a Check Control warning | The application is told `aside` (it decides what to do) | `plugin ... application hold <app>: aside` |
| Select the manager's entry while an application holds | The manager takes the hold; the application is told `aside` | `application hold <app>: aside`, then the manager's `hold` |
| The application ends or releases | iDrive returns to the list holding the entry | `plugin ... application hold <app>: release` |
| A declared car-data name (gear, day/night) changes | `/dev/shmem/nbt-bridge1-cds-<name>` gets the value | `plugin ... car data <name>` (first value) |
| `pfctl -s rules` while it runs, and after | `nbt-app-<app>` rules for its ports while it runs, none after | `host <app>: packet filter open for ...` |

## 5. Pairing

| Action | Expected | Log |
| --- | --- | --- |
| Application, then "Update from phone" | QR screen with "Closes in 60 s" counting down | `gui ... screen app -> pairing`, `worker ... session for <app> listening on <address>:8080` |
| Wait 60 s | Session cancelled: "Timed out; no package received" | `gui ... screen pairing -> app: Timed out; no package received`, `worker ... command CANCEL` |
| Scan the QR code with a phone on the head unit's Wi-Fi | The upload page opens; the head unit shows "Phone connected" and stops counting | `worker ... http upload page served`, `worker ... upload page opened`, `gui ... phone opened the upload page` |
| Open the link again after cancelling | "This link has expired" | `worker` has no listener: the phone reports a connection error, or `http page refused` while another session is open |
| Address in the QR differs from `ifconfig` | Record it; check `SESSION_INTERFACE` | `http ... refused (host ...)` shows what the phone used |

## 6. Install, update and rollback

Use the test application (`apps/test`, `TEST v1 - native` / `TEST v2 - native`) provisioned
as described in `README.md`; `test-runner` is the same application run by `nbt-runner`
(`TEST v1 - runner`).

| Action | Expected | Log |
| --- | --- | --- |
| Upload `test-v2.nbt` from the phone | Review screen: application, versions, sequence, SHA-256 | `worker ... http upload started (<n> bytes)`, `http upload of <n> bytes verified`, `state verified` |
| Compare the digest with `sha256sum test-v2.nbt` | Equal | — |
| Install, Yes | Progress, health check, "Update installed"; `TEST v2 - native` runs | `worker ... command APPROVE`, `state installing: ...`, `state installed: Update installed` |
| Upload a package signed with another key | "Upload rejected" | `http upload of <n> bytes failed verification` |
| Add a developer key from the phone (Developer keys) | "Add developer key <name>?" with the fingerprint the phone shows; Yes: "Developer key added" | `command PHONE YES ...` |
| Upload a new application signed with that key | Review shows "Developer key: <name>"; it installs | `state verified`, `state installed` |
| Remove the key from the phone, then upload an update of that application | It still runs; the update fails: "signing key removed from this car" | `state failed: Package rejected: ...` |
| Install a v3 whose executable exits at once | "Update failed; previous application restored"; v2 runs again | `state failed: Update failed; previous application restored` |
| Install an older sequence | Refused unless "Install as recovery" is chosen | — |
| Reject on the review screen | Upload discarded; the phone shows "Cancelled in iDrive." | `command CANCEL`, `state idle: Session closed` |
| Watch the phone during each of the above | It follows: "Verified. Review and approve it in iDrive.", "Installing: ...", then "Installed." or "Failed: ..." | — |
| Let a verified session reach 10 minutes | The phone shows "Session expired. Scan the code again." | `state idle: Session expired` |
| Update the manager to an `NBTRLS3` release (installer, once) | `manager/slots/<n>/runner` beside `gui` and `worker` | `nbt-verify-release` prints `format=NBTRLS3` |
| Install an application built without the QNX SDP (a module, then a guest) | Health check passes; it runs as before | application log: `runner` lines, then the application's own |
| Its log after a start | The runner names the payload and its ABI; no `needs runtime` line | `nbt-runner ... app` stage, `application ended, status 0` at stop |
| Install `test-runner-v1.nbt`, then `v2`, then `v3-broken` from the phone | A new application, then "Update installed" (`TEST v2 - runner`), then "Update failed; previous application restored" | application log: `[test] test v2 (runner)`; for v3 `failing on purpose after 5 s`, `application ended, status 1` |

## 7. Interruption and recovery

| Action | Expected |
| --- | --- |
| Kill the worker process during the health check | The GUI restarts it; the next start restores the previous executable if needed; the log shows the recovery |
| Kill the worker while the GUI is idle | The GUI reports the failure and starts a replacement without a new launcher |
| Kill the GUI while it holds focus | The launcher restarts it with backoff; the plugin releases the hold within its liveness bound; `manager/crash.gui` is reported after a fatal signal |
| Kill the GUI while hidden during an installation | The launcher restarts it; the installation finishes or is restored; the next open shows the result |
| Start the test application, then on the phone set its **Crash on purpose** to `fault` (and later `hang`); then the car's page, Crash reports | A report for it, newest first; opening it shows `Ended: ended by signal 11 (SIGSEGV, ...)`, its last output, and the system log's fault lines (record whether the dumper's `run fault pid` line for it is there); Share sends the file; Clear empties the list |
| Kill the GUI with `kill -SEGV` | Also a report of NBT Manager: `Ended: manager GUI terminated by signal 11`, the GUI's crash record |
| Fill the storage, then upload | The upload fails cleanly; nothing installed; the log shows the failure |
| Manager update and interrupted manager update | As in `docs/manager-release.md`: the known-good slot stays active until the new one is healthy |
| Interrupted initial setup | Setup resumes or fails closed; OEM files unchanged |
| Power loss during an installation (isolated storage only) | After boot, either the old or the new version is installed and healthy, never neither |

## 8. Repetition

| Action | Expected |
| --- | --- |
| Open and close the manager 20 times | Input keeps working; no growing number of processes; HMI controller handling normal afterwards |
| Same with `release=1` in `controller.conf` | Each exit logs `MOST controls released` and `controller release done` (not `timed out`); iDrive's own controller handling is unaffected; the next open receives input. Keep `release=0` if anything differs |
| Disconnect and reconnect the MOST link (if possible) | `MOST detached`, then `MOST attached` and registration again |
| Leave the manager idle for two minutes | It closes: `gui ... exit (idle)` |

## 9. Applications' files

With an application declaring `files` ([files-bridge.md](files-bridge.md)) and
the debug log on:

| Action | Expected |
| --- | --- |
| Before anything: `ls -ld /mnt/share`, `df /mnt/share`, `mount` | `/mnt/share` is a writable, persistent volume (qnx6), not used by iDrive in a way the manager's folder could disturb; record its free space. If it is not, stop and choose another folder (`FilesRoot::location`) |
| Start the application | `/mnt/share/nbt-manager` and `files/<app>` exist, mode 0700; the application's log shows its `NBT_FILES_DIR` |
| Send a file from the phone, then list it | Stored, listed on the phone and in Settings > Files; nothing left in `staging/` |
| Plug a FAT32 USB stick with matching files, with a gamepad plugged in too | It mounts as `/mnt/umass…`; Import from a USB stick lists the files; the gamepad keeps working |
| Import all, including a file of several hundred MB | The progress bar advances; the GUI stays responsive; no watchdog report (`wd_*` in `/mnt/quota/sys`); the files are complete (compare sizes) |
| Pull the stick during an import | The import stops with "the stick could not be read" or "a file changed"; no partial file in `files/<app>`; the worker keeps running |
| Fill the quota | The phone and the car both refuse with the quota reason |
| Restart the head unit | The files are still there |
| Uninstall the application | `files/<app>` is gone |

## 10. Start with the car

With the debug log on and the test application installed
([PROTOCOL.md](../PROTOCOL.md), "Autostart"):

| Action | Expected |
| --- | --- |
| OPTION on the application; select **Start with the car** | The row reads "on"; `state/<app>/autostart` holds `enabled=1` |
| Switch the car off, wait for the head unit to sleep, start it again | About 25 s after iDrive's menu appears the application runs (its status label in the manager's menu) without the manager's screen opening; the debug log shows `launcher spawn begin (--autostart)` once |
| Restart iDrive only (Settings > Status, or `OnOffDSICommander appreset`) | No second automatic start: `/dev/shmem/nbt-manager.autostart` is still there |
| Install a test package that fails its start, with the row on; reboot three times | The fourth boot starts nothing; the page says it is paused; starting it once by hand resumes it |
| Stage a manager update and reboot while it is pending | The automatic start runs with the previous, known-good worker; the update stays pending |
| Open the manager within 20 s of a boot, leave it open | The application still starts: `autostart: the manager is open; its worker starts the applications`, then the worker's `autostart: handed over` |

### App shortcut

After "Restart iDrive" for the plugin with the three entries:

| Action | Expected |
| --- | --- |
| Media list | **App shortcut 1**, **2** and **3** after **App Manager**, with its icon and preview |
| Choose **App shortcut 1** in the Media list | `App shortcut 1 pressed` in the debug log (dev126: no press reached the plugin while actions 3-5 were declared only in their own entries) |
| Highlight **App shortcut 1**, hold a favourite button | iDrive stores it (no "unable to save") |
| The button with nothing assigned | The manager opens (`App shortcut 1 pressed`, `shortcut 1: the manager opens`) |
| An application's page: **App shortcut** pressed once | The row reads 1; the note names iDrive's entry; `manager/shortcuts` holds it on line 1 |
| The button, the application stopped | The manager opens and starts it, as if chosen in its list (`App shortcut: starting <app>`) |
| The button, Android Auto running behind iDrive (after a camera, a warning, the phone's exit) | Android Auto comes back; the manager does not open (`shortcut 1: android-auto asked to come back`, the host's `show forwarded`) |
| The button with the manager already open | The same, through the open session's worker (`its worker takes the press`) |

## 11. MOST subscriptions and parameter capture (A1/A3/B3/B4)

Record the firmware, manager commit, monitor package
digest, date and operator. Use an isolated bench first. Keep raw logs in private
storage outside the repository; messages can contain VIN and positions. Publish
only reviewed facts and addresses. The recorder exits before its private log
reaches 16 MiB; retrieve and rename it before restarting for another run.

`apps/test` does not consume the raw bridge. Use `apps/most-monitor`, built as
`build/most-monitor` on the host and `build/most-monitor-qnx` by `qnx`.
Provision it as a separate application following [README](../README.md), with
its own local profile and public key. The manager gives it the usual readiness
and lease arguments. It heartbeats in `NBT_LAUNCH=check` without touching MOST;
a user launch records to `$NBT_DATA_DIR/most-capture.log` (private, append-only).
It opens no second DSI connection itself and takes no controller controls.
Logs use monotonic milliseconds, with STATE, REGISTRY, NODE, MESSAGE and RESULT
lines. MESSAGE retains source, destination, op, tag, drop count and all bytes.

Build after sourcing the QNX 6.5 environment, with the local target libraries:

```sh
nice make -j $(nproc) qnx qnx-check stack-report \
  QNX_SCREEN_LIBDIR=/path/to/screen RHMI_LIBDIR=/path/to/framework
python3 tools/package.py --app most-monitor --version passive --sequence 1 \
  --compat your-target-id --key /private/monitor.pem \
  --bridge 1.5 --bridge-uses most --bridge-most 'rx:*.*.*' \
  --output /private/monitor-passive.nbt build/most-monitor-qnx
```

Increment sequences for each package. Approve installations locally. Do not
add `sub` until the passive and one-Get runs have been recorded.

| Step | Action and evidence to retain |
| --- | --- |
| A1.1 | Launch the passive monitor. Save STATE's own address and every NODE entry. Transcribe reviewed FBlock/instance/address facts to `docs/car-registry.md`, stating firmware and capture reference. Empty or stale REGISTRY is a failure to investigate, not evidence of absent FBlocks. |
| A1.2 | Keep it running and open the manager GUI's controller client. Turn/push/back: record whether the monitor and GUI both receive E5 input. Check Vehicle 05/00 functions 621 and 228 with **no ctl**. Save both clients' own addresses from logs; compare them with the static per-client-address finding. |
| A1.3 | Stop it. Sign a second package with `rx:*.*.*,tx:05.00.621` and `--setting 'get|Get once|speed|off,speed'`. On user launch, after link and a Vehicle registry entry, the monitor sends exactly one empty op-1 Get to 05/00/621. Retain GET, RESULT and the reply's complete source/destination. `gear` selects 228 instead, requiring `tx:05.00.228` and `--setting 'get|Get once|gear|off,gear'`. No retry is automatic. |
| A1.4 | Exit the application, reopen it and reopen the GUI. Record controller operation and any change in status routing. If device-control cleanup needs testing, use a separate signed policy with one explicitly recorded ctl key; the passive run cannot establish cleanup of controls it never registered. |
| A3.1 | Choose a property from this car's registry that iDrive does not display. First trace its payload and verify that Get/notification is harmless. Sign a bridge-1.2 package with `rx:*.*.*,sub:FB.II.FFF` for that concrete property. Record Set/initial-Status timing with external instrumentation if needed; an absence of initial Status determines whether the host needs a one-Get fallback. Do not treat host SENT as FBlock acknowledgement. |
| A3.2 | Change the property's physical state. Check Status data while subscribed; stop the app and check that its per-client notification stops. A second, passive monitor can observe traffic only if A1 established that routing supports it. Keep iDrive's speed/gear displays operating during and after the test. |
| A3.3 | Reconnect the link, then remove/reintroduce the chosen FBlock on an isolated bench. Check re-subscription, no send before registry presence and recovery of Status. Repeat with an abrupt host death; record whether server cleanup removes remote entries. |

Use the same fixed B4 script for passive, Get and subscription runs. Mark each
action's time in a separate private note against the recorder's monotonic clock:

1. Stationary, parking brake on, record 10 seconds of baseline.
2. Record each available ignition/terminal state, 5 seconds per state.
3. With the vehicle stationary and secured, select park/neutral/reverse as
   available; hold each 5 seconds. Record the selected state explicitly.
4. Turn volume up three steps, down three; turn the controller three steps
   each way, push/release, BACK and MENU, with 2-second gaps.
5. Toggle available exterior lights, recording exact switch states.
6. For speed, use a simulator/roller bench, or have a passenger operate the
   recorder during a safe drive. Record stationary and known stable speeds.
   The driver must not operate test controls or logs.
7. Return to the baseline, stop the monitor, verify iDrive operation and copy
   the capture to private storage. Any dropped count qualifies the coverage.

Match actions to the reviewed layouts in [most-params.md](most-params.md).
Record byte order, offsets, invalid sentinels, scaling and enum values only
where captures establish them. Add the private capture reference and change
`car_confirmed` only for individually checked rows. Only these observations
can settle A1, A3 and B4; a host test or a QNX build cannot.

## 11b. Self test

Settings › Self test ([self-test.md](self-test.md)); the debug log has one
`self test: <group>.<check> pass|FAIL|skip <detail>` line per check.

| Action | Expected |
| --- | --- |
| First run after an update | `self test: manager-selftest... installed` three times; each form's colour bars on the panel; none listed on the menu or the phone |
| The module's run | A tone rising for 3 s, without a click or a break (with an application's service playing, both heard); TURN, then PUSH; BACK skips both |
| Results | `native`, `guest` and `module` all `start pass`, `screen` over 20 fps, `native.diag`, `native.phone` and every form's `cardata` pass with the ignition on (0.5.3: no car data in the guest and the module, their runner could not read it), `manager.audio` `level 0, 0 gaps, 0 queues grown` |
| Second run | Nothing installed again; the results of this run only |
| An application running | The menu says it is stopped first; Start: "Stopping <app>", then the test |

## 11c. Manager update from the phone

A kit newer than the installed one, its `.nbtr` sent from the phone's Library.

| Action | Expected |
| --- | --- |
| Install on Review update | "Update ready", "NBT Manager <version> starts in a moment"; 3 s later iDrive's blank screen for a moment, then App Manager in the new version without choosing it again; the launcher log has `GUI closed for the manager update`, `phone update staged`, `slot <n> verified, release <seq> (trial)`; the GUI log `started after the manager update; shown at once` |
| A camera or warning during those seconds | The camera or warning shows; App Manager stays hidden until it is chosen again |
| BACK out before the 3 s | App Manager closes; the new version starts at the next opening |

## 11e. Touch screen

A head unit with a touch screen; the debug log on.

| Action | Expected |
| --- | --- |
| Open App Manager | `touch: taken while the manager is shown` |
| Tap an application, Settings, a setting | As turning to it and pushing; `touch: tap, item <n>`; the row under the finger, not the one above or below (note the offset if any, and the make: MINI's round display) |
| Drag up and down a long list | The focus moves a row per row of travel |
| Swipe to the right | BACK; from the menu, App Manager closes |
| Close App Manager, MENU, an application on the panel | `touch: back with iDrive`; iDrive's own screens react to touches again at once |
| Nothing logged at the first touch | The datagrams do not reach the manager: note it with `pidin arg` and the system log |

## 11d. Media sound without iDrive's screen

Native audio on `media`; iDrive restarted since the update (the shim loads
with iDrive); the debug log on.

| Action | Expected |
| --- | --- |
| Start an application with sound | It stays on the panel; sound as before, the volume down to 0; the plugin logs `capture diag kind=8 (audio connection hidden) fblock=0x03 fkt=0x0c03`; iDrive's entertainment source and its last-source memory unchanged (after the application, the radio is still the source) |
| The self test | Every form starts and reports; no fall back to iDrive |
| Switch Native audio to `interrupt`, start the application again | `audio mixer: ended for another audio connection (default)` in the application host's log; sound through the browser source (volume not below 25); no restart of App Manager needed |
| Back to `media` with another application's service playing | `audio mixer: still in use; ...`; the change applies once it stopped |
| A phone call or a navigation prompt while the application plays | iDrive shows and plays it as usual |
| The radio playing, then start the application | The application's sound replaces the radio and stays; iDrive does not bring the radio back while it plays (if it does: `capture diag` lines and the system log, the shim then has to keep the radio's row for iDrive); after the application, note what plays |

## 12. Tools

Settings > Tools, after Status. Record `mount`, `pidin arg` and `ls -lR
/var/opt/nbt-manager` before and after.

| Action | Expected |
| --- | --- |
| Tools, Restart head unit | The question says the whole system restarts; BACK or Later returns to Tools on that entry; Restart head unit restarts the head unit, the debug log notes `restarting the head unit (OnOffDSICommander appreset)`, and the manager opens again with iDrive |
| Tools, Enable SSH, No | Back on Tools, on Enable SSH; nothing started |
| Tools, Enable SSH, Enable SSH | "SSH enabled" over "On until the head unit restarts"; the debug log notes `ssh enabled (OPT_DEBUG runs) until the head unit restarts` |
| SSH and telnet to the head unit as root | `pidin arg` lists `/bin/inetd /etc/inetd.conf`, `sshd -i` and `/bin/qconn`; `mount` still shows `/fs/sda0` read-only; no file under `/var/opt/nbt-manager` changed |
| Restart the head unit | `pidin arg` has no `sshd`; Tools, Enable SSH enables it again |

## 13. Toyota GR Supra

The Supra (A90, J29) carries the same NBT Evo ID6 unit with Toyota's HMI. Nothing
here has run on one yet; a Supra owner's first run covers sections 1 to 6 and 11b,
then these rows. NBT-AAD's notes are the reference for what differs.

| Check | Expected, or what to record |
| --- | --- |
| Installer, Connect and Enable SSH | The gateway answers discovery; SSH opens, or activation enables it |
| Panel size | The debug log's panel size (NBT-AAD's Toyota default is 1280x480, the SDK's desktop size): the menu fills it at scale 2, apps report the same size |
| iDrive entry | App Manager in the Toyota menu. NBT-AAD reports one firmware (NBTevo_T22181I, Middle East) whose HmiMain3D never loads a plugin: record the firmware |
| Controller and buttons | Which hard keys reach the manager; MENU closes it, BACK goes back |
| Phone opens the manager | Only from the home screen context `visibleContext 3` (`phoneContextSafe`): record `hmi.graphicalContext` on the home screen |
| Camera views | Top View (context 692 in NBT-AAD) and reverse take the screen over an app, and the app comes back after |
| Self test | Every row passes or skips with a reason; the report shared from the phone |
| Theme | Automatic shows the Supra theme (orange-red glow line): the log's `theme auto, car supra`. `car unknown`: record whether `/var/opt/conn/carplay/carplay.ini` exists |
| Android Auto | Car make `auto` resolves to Toyota from `carplay.ini`; Cluster `j29` shows turns |
| Gauges and Diagnostics | The B58's readings and the fault-code units answer through the gateway |
