# Wired USB gamepad bridge

Bridge 1.3 adds `gamepad` without changing package, installed-record or journal
formats. Applications may retain minimum bridge 1.2 and request it optionally.
Unknown optional capabilities remain compatible with older managers. A required
capability needs an implemented service; an unplugged controller is a normal
disconnected state. Applications never select a USB path or send USB commands.

## Channel

`NBT_GAMEPAD_FD=7` is a nonblocking `AF_UNIX/SOCK_DGRAM` socket, passed only to
declared/granted user launches, alongside MOST at 6 and status at 5. Both launch
implementations preserve it through descriptor mapping and sandbox setup.
Health checks have no gamepad descriptor and open no USB connection. Applications
cannot send commands on this input channel; inbound data is discarded in bounded
batches. There is no HTTP or plugin route to generate local gamepad input.

Every datagram is exactly 24 bytes, little-endian:

| Offset | Field |
| --- | --- |
| 0 | Version, 1 |
| 1 | Type, 1 (complete state) |
| 2 | Connected, 0 or 1 |
| 3 | Reserved, zero |
| 4 | Unsigned 32-bit sequence, modulo 2^32 |
| 8 | Unsigned 32-bit buttons |
| 12, 14, 16, 18 | Signed 16-bit left X/Y, right X/Y |
| 20, 22 | Unsigned 16-bit left/right trigger |

Stick axes span -32768..32767, positive right/up. Triggers span 0..65535.
Button bits 0..14 are south, east, west, north, LB, RB, View, Menu, left/right
stick clicks, Guide, D-pad up/down/left/right. Other bits are rejected.
Disconnected records must have neutral controls. Changed reports are delivered
immediately; complete snapshots repeat every 100 ms. Socket backpressure never
blocks USB or the UI. Latest state is retried on subsequent snapshots.

The standalone SDK wire and input modules have no manager or USB dependencies.
They reject short/oversized datagrams and malformed fields, suppress stale or
duplicate sequences, clear controls after 500 ms without valid state, and require
neutral controls after connection/focus acquisition. Consumers maintain their
own button edges; a disconnected state cancels a selection, never confirms it.

## Service and ownership

Only one controller is active. The first supported arrival is retained until
removal; other supported devices wait. A pad that fails to open is retried after
2 s, doubling to 60 s, and told apart from others on its port by vendor and
product (until dev113 it waited for a replug, and a later pad on the same port
was taken for it). QNX uses the installed USB DDK with asynchronous interrupt input
and bounded, timed output on a service thread. The main GUI and host loops do
not perform USB attachment or transfers. At the first connection the service
loads the car's `/lib/libusbdi.so.2` with `dlopen` (NBT-AAD links that library
and works on the car) and uses it only when all 20 entry points it calls
resolve; otherwise it uses the SDK's static `armle-v7/lib/libusbdi.a`, linked
in. No `DT_NEEDED` entry is added, so a missing or different library cannot
stop the manager from starting. The log says which one is used
(`src/gamepad/usbdi.cpp`). Both connections use libusbdi's default path, as
NBT-AAD does.

A short descriptor-only shared connection discovers the controller interface
before exclusive attachment. Xbox uses data interface 0; Sony uses the HID
interface. Configuration queries preserve an already matching configuration;
an unconfigured controller is configured once, and an unexpected active
configuration is rejected. The selected interface uses alternate setting 0.
Sony SET_IDLE is best effort, without changing HID protocol or enabling
optional controller features.

**One USB owner (since dev115): the manager GUI**, for its whole life (it keeps
running, hidden, while an application is shown). Until dev113 the GUI and each
application's host took turns, closing and reopening the pad at every switch;
on the car (2026-11-18) a DualShock 4 failed `GET_CONFIGURATION` twice right
after such a switch and stayed off until replugged. Now the GUI's service
(`src/gamepad/service.cpp`) keeps the pad open, sends each state to the GUI's
own socket (used only while the manager is on the panel) and publishes it in
`/dev/shmem/nbt-manager-gamepad` (`src/gamepad/shared_state.cpp`: one 48-byte
record, magic, count, monotonic time, the 24-byte wire state, count again;
rewritten at least every 100 ms; the capture control file's owner and mode
checks). An application's host relays it (`src/gamepad/relay.cpp`) on
`NBT_GAMEPAD_FD` unchanged: the same 24-byte states, on change and at least
every 100 ms, connected only while the host is active (a surface, not paused)
and the application's surface holds the panel (checked every 250 ms), and
disconnected when the record is torn, malformed or older than 500 ms (the GUI
gone). Hosts never open USB. `state/manager/gamepad.lock` byte 0 still makes
one process the owner (a second GUI waits; process death releases it); byte 1,
the GUI's old priority, is unused. USB buffers are freed before disconnect
destroys the DDK allocator; unexpected USB cleanup errors terminate the owner.

**Status page and gamepad test** (manager Settings > Status, since the kit
after dev116; `src/gui/status_page.cpp`). The service keeps a report the GUI
reads each loop (`Service::report`): whether it runs, whether it holds the
pads, whether another process holds the ownership lock, the open pad's family,
vendor and product, `usbd_connect`'s last error and a plugged-in pad that
failed to open with the step it stopped at (`UsbDevice::failure`: "configuration
failed", "unsupported interface", "attach failed" and so on; it is retried as
above). The page shows one line for the pad, most telling cause first: the
open pad (`Gamepad: DualShock 4 (054c:09cc)`), `USB held by another process`,
`no USB stack (usbd_connect N)`, `045e:02d1 could not be opened` with the step
below it, or `none found`. **Test the gamepad** shows the pressed buttons
(Sony's names for Sony pads), both sticks and both triggers in percent, live.
The pad's own buttons are not acted on there (B, Menu and the D-pad would
otherwise leave it); the iDrive controller's BACK leaves.

No configuration reset is issued to unrelated USB devices. The actual USB stack
path is `/dev/io-usb` when it is a node, or `/dev/io-usb/io-usb` when its parent is
a directory. Driver permissions and interface attachment still require car tests.

## Controller protocols and controls

Xbox One GIP interfaces match FF/47/D0 on interface 0 and interrupt endpoints,
covering wired Microsoft Xbox One (`045e:02d1` original, `045e:02dd` 2015,
`045e:02e3` Elite Series 1, `045e:02ea` One S), Elite Series 2 (`045e:0b00`),
Adaptive (`045e:0b0a`), Series X|S (`045e:0b12`), and compatible third-party
GIP devices. The decoder checks lengths, trigger limits and main-client framing
before changing state. Chunked reports are unsupported and cannot allocate
reassembly buffers. Initialization includes Microsoft S/Elite (`045e:02ea/0b00`),
Hori/PDP (`0e6f:0165`, `0f0d:0067`) and PowerA (`24c6:541a/542a/543a`) command
variants alongside generic power/LED/auth startup (e.g. PDP `0e6f:*`, PowerA
`20d6:a01a`, 8BitDo `2dc8:2008`). Guide and announcement acknowledgments preserve
the incoming sequence. The basic control layout also works for compatible
third-party GIP devices; variants require tests.

Sony USB support recognizes DualShock 4 (`054c:05c4` v1, `054c:09cc` v2) and
DualSense (`054c:0ce6` DualSense, `054c:0df2` DualSense Edge) HID interfaces.
Both use validated 64-byte report ID 1 layouts and common normalization.
Bluetooth, wireless dongles (such as `054c:0ba0`), touch/gyro input, rumble
control, headset audio and unknown clone layouts are outside this capability.

The GUI maps D-pad/left stick to navigation (left also goes back), south/A/Cross
to push including the settings hold, east/B/Circle and View/Share/Create to back,
and Menu/Options to the existing leave-manager action. Navigation starts at a
20% stick threshold, repeating after 400 ms every 100 ms. Existing local approval
and installation guards apply unchanged.

Protocol references, independently implemented:

- [QNX USB connection API](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.ddk_en_usb/usbd_connect.html)
- [QNX endpoint enumeration](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.ddk_en_usb/usbd_parse_descriptors.html)
- [Linux Xbox protocol reference](https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c)
- [Sony's Linux controller report reference](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-playstation.c)

## Hardware publication gate

Host simulation, USB API shim tests, fuzzing and QNX cross-compilation establish
software coverage, not head-unit compatibility. Before publishing the signed
candidates, retain the captured USB descriptors and stack permissions, and
record actual claiming results and input behavior for Xbox One and PS4.

On isolated bench storage or the parked car, test each controller's buttons,
D-pad, axes and triggers; cold connection; plug after launch; unplug while
moving/firing/selecting; reconnect; repeated GUI/app/GUI switches (the pad
stays open: no `USB service detached` between them); failed app
launch; focus loss; app/GUI termination; controller replacement; and ordinary
iDrive input. Health-check launches must leave the GUI's controller ownership
intact. Verify no stuck actions, accidental approvals, growing descriptors or
threads, and that other USB functions remain available.

The 2026-11-17 car capture confirms Xbox One 045e:02d1 (FF/47/D0,
controller data interface 0), DualShock 4 054c:09cc (HID interface 3), and
/dev/io-usb/io-usb (root-owned, mode 0644). Functional Xbox One and PS4
validation is pending. DualSense is enabled experimentally with
fixtures and build checks; no PS5 hardware validation is available. Hold public
NAS publication until the Xbox One/PS4 checks above pass.

The first car run (2026-11-17, releases 86 and 89) never reached `io-usb`:
the service stopped before its first USB call. See
[gamepad-usb-research.md](gamepad-usb-research.md#car-run-2026-11-17-the-service-never-reached-io-usb).
