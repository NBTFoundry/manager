# Bench checklist

Nothing in NBT Manager has run on a head unit. This is the order in which to
validate it on a bench unit: each step relies on the ones before it. Record the
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

## 5. Pairing

| Action | Expected | Log |
| --- | --- | --- |
| Application, then "Update from phone" | QR screen with "Closes in 60 s" counting down | `gui ... screen app -> pairing`, `worker ... session for <app> listening on <address>:8080` |
| Wait 60 s | Session cancelled: "Timed out; no package received" | `gui ... screen pairing -> app: Timed out; no package received`, `worker ... command CANCEL` |
| Scan the QR code with a phone on the head unit's Wi-Fi | The upload page opens; the head unit shows "Phone connected" and stops counting | `worker ... http upload page served`, `worker ... upload page opened`, `gui ... phone opened the upload page` |
| Open the link again after cancelling | "This link has expired" | `worker` has no listener: the phone reports a connection error, or `http page refused` while another session is open |
| Address in the QR differs from `ifconfig` | Record it; check `SESSION_INTERFACE` | `http ... refused (host ...)` shows what the phone used |

## 6. Install, update and rollback

Use the test application (`apps/test`, `TEST - v1` / `TEST - v2`) provisioned
as described in `README.md`.

| Action | Expected | Log |
| --- | --- | --- |
| Upload `test-v2.nbt` from the phone | Review screen: application, versions, sequence, SHA-256 | `worker ... http upload started (<n> bytes)`, `http upload of <n> bytes verified`, `state verified` |
| Compare the digest with `sha256sum test-v2.nbt` | Equal | — |
| Install, Yes | Progress, health check, "Update installed"; `TEST - v2` runs | `worker ... command APPROVE`, `state installing: ...`, `state installed: Update installed` |
| Upload a package signed with another key | "Upload rejected" | `http upload of <n> bytes failed verification` |
| Install a v3 whose executable exits at once | "Update failed; previous application restored"; v2 runs again | `state failed: Update failed; previous application restored` |
| Install an older sequence | Refused unless "Install as recovery" is chosen | — |
| Reject on the review screen | Upload discarded; the phone shows "Cancelled in iDrive." | `command CANCEL`, `state idle: Session closed` |
| Watch the phone during each of the above | It follows: "Verified. Review and approve it in iDrive.", "Installing: ...", then "Installed." or "Failed: ..." | — |
| Let a verified session reach 10 minutes | The phone shows "Session expired. Scan the code again." | `state idle: Session expired` |

## 7. Interruption and recovery

| Action | Expected |
| --- | --- |
| Kill the worker process during the health check | The GUI restarts it; the next start restores the previous executable if needed; the log shows the recovery |
| Kill the worker while the GUI is idle | The GUI reports the failure and starts a replacement without a new launcher |
| Kill the GUI while it holds focus | The launcher restarts it with backoff; the plugin releases the hold within its liveness bound; `manager/crash.gui` is reported after a fatal signal |
| Kill the GUI while hidden during an installation | The launcher restarts it; the installation finishes or is restored; the next open shows the result |
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

## 9. MOST subscriptions and parameter capture (A1/A3/B3/B4)

**Not yet performed.** Record the firmware, manager commit, monitor package
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
  --bridge 1.1 --bridge-uses most --bridge-most 'rx:*.*.*' \
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
`car_confirmed` only for individually checked rows. A1/A3/B4 remain pending
until the observations above exist; a host test or QNX build cannot fill them.
