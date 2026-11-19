# dev61: update launch, pairing and activation investigation

Evidence is the owner's NAS logs under `/mnt/nas/public/nbt/`, current source
at `c739a00`, and static inspection of the owner's NBTevo firmware dump.
No commands were run on the head unit and no activation request was sent.

## Activation stops before connecting

`installer-1794829269.log` repeats the same sequence at 19–23, 162–167 and
256–260 seconds: the service reports `SSH: enabled`, asks for the head-unit
address, reports an IP, then the installer reports `Invalid activation
response`. Each subsequent Connect authenticates successfully. This is a
regression reported by the user; the failure occurs after activation has
already enabled SSH.

The codec is unchanged since the earlier working implementation. The shared
core extraction changed socket creation but retained the response dispatch.
The new IP-query status is absent from the older successful local capture;
a changed service response is a hypothesis, not an established wire format.
The log contains no offending tag or decoder reason, so it cannot establish
which message failed. Do not invent an additional message format from the
human-readable status text.

The reconnect path now checks actual SSH reachability after a relay error,
retaining the error in the log and completing ordinary SSH authentication
when a host answers. It never repeats activation automatically. An explicit
negative result still stops. Decoder failures report only reason, tag and
length. A local mock reproduces the old failure with an already accepting
SSH endpoint; it fails before the change and passes after it. Explicit
refusal and cancellation have separate regression cases.

## Update launch freezes before the window

The user confirmed that the freeze is immediate on selecting App Manager.
The installer successfully stages release 61, then reports the plugin files
unchanged by checksum. That rules out a plugin-file replacement during this
particular update; it does not establish that the loaded plugin is healthy.
The launcher's pending/trial state and alternate slot distinguish an update
from a fresh install.

The available debug log begins after the later removal and fresh install:
it verifies slot 0/release 61, starts worker and GUI, receives controller
input and reaches pairing. It does not contain the failing updated launch.
The system logs include a missed SvcDSI watchdog callback followed by a
return to the heartbeat-present state. Those entries alone do not locate
the freeze or establish its reset sequence. The repeated `kdMain` warning
also appears on the successful launch; it is not evidence of this cause.

There is no host reproduction of the iDrive freeze. The live candidates
remain a blocking plugin spawn, a blocking focus-event dispatch, or a
launcher/update-state failure before GUI readiness. Plugin diagnostics now
bracket `spawn()` and focus events 52/50/51 so that a missing return can
distinguish these paths. They do not change launch or focus behavior.

For a logged reproduction, enable **Test tools → Debug log on** on the head
unit before Update. The installer's own debug logging is separate. Fetch
the head-unit debug log and system log before Remove: removal deletes the
manager directory and its logs. Preserve `debug.log.1` as well. A reproduction
belongs on a bench; no fix for the freeze or vehicle validation is claimed.

The logged removal command deletes the manager root and its two plugin
folders. It does not delete OEM driver profiles. These captures do not show
what erased the iDrive data, or when it happened.

## Pairing advertises saved credentials without starting a group

The worker binds HTTP and its temporary firewall rule to `wfd0`, then reads
the saved passphrase and derives the SSID from CarPlay configuration. It
never asks Connectivity to enable Wi-Fi Direct, creates a group, or verifies
that the radio advertises it. The latest log names the same SSID as the older
working logs. An assigned `172.16.222.1` address and a listening TCP socket
do not establish radio/group availability.

This confirms the missing initialization/readiness contract. It supports
the user's prior-state explanation, but does not prove that NBT-AA itself
created that state. Its documented alternate-Wi-Fi path also assumes an
OEM group already exists. The user confirmed that this unit exposes no WLAN
setting in iDrive; a manual iDrive toggle is therefore not a setup procedure.

The firmware's `ConfigGen_Options.ini` sets autonomous group ownership and
manual DHCP-server startup. Static inspection of Connectivity establishes
separate persisted radio-enable and Wi-Fi Direct state, profile-import/reset
handling, and different startup paths. The networking library contains
enable, start-GO and hidden-SSID operations. These are evidence of available
mechanisms, not verified application-call signatures or permission to
replace Connectivity's radio management with a second daemon.

The required pairing behavior is implemented for `wfd0` sessions:

- `WfdMostClient` (`src/worker/wfd_most_client.cpp`) and `WifiDirectController`
  (`src/worker/wifi_direct_controller.cpp`) open a dedicated MOST client
  restricted by policy to `A0.03` (`rx:A0.03.*,tx:A0.03.*,sub:A0.03.C10,sub:A0.03.C12`).
  CarPlay (`A0.01` / `uap0`) is excluded by policy and untouched.
- `MostSession::destination` and `Subscriptions::registry` now accept addressable
  HU service nodes up to `0x4FFF` (such as Connectivity's live `0x203F` entry),
  while continuing to exclude the `0x5000` gateway and `0xFFFF` broadcast.
- On `SESSION wfd0 <port>`, the worker waits up to 10 seconds (`R_SESSION` GUI
  reply timeout raised to 15 seconds) for `A0.03`, queries `WLANState3` (`0xC10`),
  enables the group with `SelectWLANState3` (`0xC11`) only when `0` -> `1`, queries
  live `WLANConfiguration` (`0xC12`) to confirm a visible non-empty SSID and
  valid passphrase (preferring `WLANAccessPointKey` with `WFDPass.conf` fallback),
  and checks that `wfd0` is `IFF_UP` with a non-zero IPv4 address.
- Any failure or hidden SSID returns `ERR network-unavailable` without advertising
  offline credentials. Only a session that transitioned `A0.03` from `0` to `1`
  restores it to `0` on `CANCEL`, session expiry or worker shutdown, and
  not when NBT-AAD uses the group (dev68, `docs/android-auto-wifi.md`).
- Bench/car testing is still required to confirm live Connectivity DHCP/radio
  timing and Android Auto / CarPlay coexistence on hardware.

Run the kit's read-only probe from the SSH terminal while Pair a phone is
open (replace the USB path with the actual one):

```sh
sh /mnt/umass<...>/car_network_diagnostics.sh
```

Save its stdout. It reports interfaces, relevant processes, configuration
metadata, non-secret device/channel defaults and manager activation state.
It omits keys, PINs, leases and binary profile contents, and changes no
head-unit files. Its defaults section is configuration, not live radio state.

REA observations are retained locally in ignored
`build/car-issues-2026-11-16-rea.json`. Firmware and decompiled implementation
are not committed. Relevant Connectivity locations are `0x1d2888` (startup),
`0x1d35b8` (set-state action), and `0x1c1718` (activation facade); these are
for the owner's dump, SHA-256
`e05c11b74064bd09b72b7992330e9512e0cc1a659357df3473b92dea73c8de0f`.

## Verification

- `make pre-commit test`: passed, 239 Python tests (three optional QR skips)
  and the C++ tests. The read-only probe's fixture test checks both secret
  omission and preservation of every input file.
- Installer core: 38 tests passed; its separately enabled
  `native_bootstrap_over_ssh` integration also passed with the real host
  launcher and isolated temporary storage.
- Windows GNU target: installer and shared core passed `cargo check`.
- QNX 6.5: `qnx-check stack-report` passed using the existing target libraries.
  The plugin remains 300 lines. No new memory ownership or parsing is added
  to the native components; sanitizer/fuzz suites were not rerun.

## Follow-up verification

- Host end-to-end tests: `test_wifi_direct.py` exercises `SESSION`/`CANCEL`/`QUIT`
  with `libfake_dsi.so` (`A0.03` enable+restore, pre-existing active group,
  `WFDPass.conf` fallback, `ERR network-unavailable` on missing `A0.03` / `ErrorAck` /
  hidden SSID / unavailable `wfd0`), `test_most.cpp` and fuzz target `wfd-most`
  exercise the `0xC10`/`0xC11`/`0xC12` codecs, `test_ui.cpp` checks the 15-second
  `R_SESSION` timeout, and `test_manager_stage.py` adds a `--stage-update` ->
  `--refresh-setup` -> `--open` trial-confirmation regression test.
- Live head-unit validation of the MOST `0xA0.03` Wi-Fi Direct sequence and a
  logged bench reproduction of the Update launch freeze are still needed.
