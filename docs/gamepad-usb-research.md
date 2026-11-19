# Research: wired gamepad USB discovery

Reviewed 2026-11-17. Use descriptor discovery, with small device-family decoders,
for wired Xbox One, DualShock 4 and experimental DualSense support. Linux's
drivers select endpoints from their descriptors; the addresses below are useful
regression fixtures rather than a portable endpoint table. [Linux xpad](https://github.com/torvalds/linux/blob/666539eecdf4db623f5e3c3e55cf9f2dfad24aa4/drivers/input/joystick/xpad.c),
[Linux usbhid](https://github.com/torvalds/linux/blob/666539eecdf4db623f5e3c3e55cf9f2dfad24aa4/drivers/hid/usbhid/hid-core.c).

## Evidence from the car

The supplied capture at `/mnt/nas/public/nbt/usb.txt` identifies Xbox One
`045e:02d1`, with interfaces 0, 1 and 2 sharing class/subclass/protocol
`ff/47/d0`, and DualShock 4 revision 2 `054c:09cc`, with device class 0 and HID
interface 3 alongside audio interfaces 0–2. Both controller captures report
Full Speed. The capture contains no endpoint descriptors, so it establishes
identity and interface topology, not endpoint addresses or successful gamepad
I/O. The same file records `/dev/io-usb/io-usb` as a root-owned character device
with permissions `crw-r--r--`.

## Car run 2026-11-18 (dev111): the DualShock 4 works; the Xbox pad was never tried

Log `debug-1795029969.log` on the NAS (its start rotated out). Reading the
interfaces one by one found the DualShock 4's HID interface (`054c:09cc config 1
interface 3 class 03/00/00`, then `interface 3`): the pad worked in Doom and in
the manager (the user). Three faults, fixed for the next kit:

- **The Xbox pad was taken for the failed PS4 pad.** At 381.9 s, right after the
  service changed hands, the DualShock 4's open failed (`GET_CONFIGURATION failed
  (status=0x02000004)`) and it was listed "until replug" as `dev=2 gen=0`. Its
  removal came while the service was disconnected, so the entry stayed; the Xbox
  pad then arrived on the same port as device 2 generation 0, matched it, and
  was ignored without a word (only `USB device 045e:02d1 class ff/47/d0` three
  times, no interface lines). The list now compares vendor and product too
  (`same` in `src/gamepad/usb_transport.cpp`). Whether the Xbox pad opens and its
  GIP start works is **not established**: it was never tried.
- **"Until replug" left a working pad dead.** The same `GET_CONFIGURATION`
  failure at 500.98 s (again right after the GUI took the pad back) kept the
  DualShock 4 off until it was replugged. A failed pad is now retried after 2 s,
  doubling to 60 s (`block`, `retryDue`); entries for pads not reported again
  within 1 s of a reconnect are dropped. Why the request fails right after a
  hand-over is not established.
- **The DualShock 4's light bar stayed in its charging colour** (the user:
  yellow, not blue). Nothing set it: a DualShock 4 shows its own colour until
  the host sends output report 0x05. The service now sends it once when the pad
  opens, blue (`dualShock4LightBar` in `src/gamepad/reports.cpp`, the layout
  Linux's hid-sony uses: id, flags 0x07, motors 4-5, RGB 6-8, flash 9-10, 32
  bytes); a failed write only logs `DualShock 4 light bar not set`.

## Car run 2026-11-18 (dev107): the pads are seen, no interface chosen

Log `debug-1795024338.log` on the NAS. The service now starts (`service ready
for the GUI`, `using /lib/libusbdi.so.2`, `USB service acquired by GUI`), and
io-usb reports every interface of both pads: DualShock 4 `054c:09cc` as
`01/01/00`, `01/02/00`, `01/02/00`, `03/00/00` (audio, then HID) and Xbox One
`045e:02d1` as `ff/47/d0` three times. Both were then refused: `has no matching
interface`, so the open failed and the pad was skipped until replugged. The
media launcher did not drop them this time (they were reported again when the
service attached anew); the system log of that run has no `vdev-medialaunch`
line, so whether it reset them is not established.

The refusal came from `discoverInterface`: it walked the configuration with
`usbd_parse_descriptors` and found no interface, without saying where it
stopped. Only the device's first reported interface is queued (the
DualShock's audio one), so the walk is what has to find the HID or GIP
interface. Changed (`src/gamepad/usb_discovery.cpp`): interfaces are read one
by one with `usbd_interface_descriptor` (configurations 1 to 4, interfaces 0 to
15), as `UsbDevice::open` already reads the chosen one, each logged as
`gamepad: USB vvvv:pppp config C interface I class cc/ss/pp`; the walk stays
as a fallback and logs where it stops. Not established on the car until the
next log.

## Car run 2026-11-18 (dev105): two causes

Logs on the NAS: `debug-1795023382.log` (the manager opened, pads plugged) and
`slog-1795023107.log` (system log). Read-only analysis; nothing new has run.

- **Our service stopped at its first step**: `gamepad: service socket flags
  failed (errno 0)`. `Service::start` re-set the flags `F_GETFL` returned with
  `F_SETFL` and took any non-zero result as failure; POSIX only promises
  "a value other than -1" on success, and on the car's socket it returned
  neither 0 nor -1 (errno untouched). Setting `O_NONBLOCK` alone, in
  `Service::socket` just before, had returned 0. Fixed (`nonBlockingPrivate`
  in `src/gamepad/service.cpp`): only -1 is a failure, a flag already set is
  not set again, and both flags are read back (logged with their values if
  missing). The same call serves the applications' channel, so Doom's
  `gamepad service unavailable` most likely had this cause too.
- **The car's media launcher disconnects both pads**: for the DualShock 4
  (`054c:09cc`, 00:03:43) and the Xbox One pad (`045e:02d1`, 00:03:50),
  `vdev-medialaunch` asked whether the device is an Apple accessory
  (`is_OAP_capable`), its requests failed (`usbd_urb_status failure: status
  5`), it reset the device twice (`usbd_reset_device`) and the device was
  removed the same second (`Removal (vid ...)`), about 2 s after insertion. No
  insertion followed in the minute and a half the log covers. Whether a pad
  comes back after that, or whether the media launcher can be told to skip
  devices that are not Apple's or storage, is **not established**: its
  configuration is not in our inventory. `tools/car_inventory.sh` now saves
  `/etc`'s listing and `/etc/*.cfg` with any `medialaunch` configuration.

## Fix for the next kit (2026-11-18)

The failing step is not established, so every suspect is either removed or
logged. Nothing here has run on the car.

- **Every silent stop logs its step and errno** (`gamepad: <step> failed
  (errno N)`): the socket pair and its flags, opening `gamepad.lock`, the
  service thread, the GUI priority lock, the USB ownership lock (once per
  failure streak), a failed send, and the GUI's manager root.
  `gamepad: service ready for the GUI|application` confirms a start.
- **`getsockopt(SO_TYPE)` is gone.** Both callers pass the datagram end that
  `Service::socket` made; nothing else had ever run that call on the car.
- **`ENOBUFS` on send no longer stops the service.** io-pkt is BSD-derived,
  where a full datagram receiver returns `ENOBUFS`, not `EAGAIN`. That was a
  silent, permanent stop whenever the reader fell behind.
- **The car's `/lib/libusbdi.so.2` is loaded with `dlopen`**, with the SDK's
  static copy as fallback when it does not load or lacks one of the 20 entry
  points (`src/gamepad/usbdi.cpp`). The log says `gamepad: using
  /lib/libusbdi.so.2` or why not. The binaries export no `usbd_*` symbol, so
  the static copy cannot interpose on the car's library.
- **Default connection path** (`path = NULL`), as NBT-AAD uses on the car,
  instead of choosing `/dev/io-usb/io-usb` ourselves.
- **Every device io-usb reports is logged** at connection (`gamepad: USB device
  vvvv:pppp class cc/ss/pp`), so the next log shows whether the pad is seen.
  A failed `usbd_connect` is logged once per new error code, not every 500 ms.

What the next manager log should show, in order: `service ready for the GUI`,
`using /lib/libusbdi.so.2`, `USB device ...` lines (with the pad among them),
`USB service acquired by GUI`, `USB <vid:pid> interface N`. The first line
missing, or a `failed` line, names the next step to fix.

## Car run 2026-11-17: the service never reached io-usb

Logs: `manager (2).log` and `idrive (2).log` from the NAS (releases 83 to 89
in one manager log; the system log covers one minute of the release 89 Doom
run). Read-only analysis against the code at `6ac832b`; nothing ran.

**Established**

- Release 89 contains every USB log line, `gamepad: usbd_connect(%s) failed`
  included (`strings` on `NBT-Manager-dev89.nbtr`).
- No `gamepad:` line appears in the manager log, in any session. Connecting
  always logs: `Service::acquire` logs `USB service acquired`, and
  `QnxUsb::start` logs a failed `usbd_connect`. So neither the GUI nor a host
  ever called `usbd_connect`: enumeration was never attempted.
- Doom's host logs `host doom: gamepad service unavailable` at both starts
  (release 86, line 914; release 89, line 1646). The line comes from
  `src/process/host_channels.cpp:33`. The manager serves `gamepad`
  (`src/model/bridge_api.cpp:145`), so `Service::socket` or `Service::start`
  returned false. Neither records the step or errno. Their steps, all before
  any USB call:
  1. `socketpair`, `FD_CLOEXEC` and `O_NONBLOCK` (`src/gamepad/service.cpp:23-31`);
  2. `F_GETFL`, `getsockopt(SO_TYPE)`, `F_SETFL`, `F_SETFD` (`:43-46`);
  3. opening `state/manager/gamepad.lock` through the checked open (`:48`);
  4. `pthread_create` (`:59`).
- The GUI's service can go quiet without a log:
  - `GamepadInput::start` ignores a failed `Service::start`
    (`src/gui/gamepad_input.cpp:21`);
  - the byte locks on byte 1 (GUI priority, `service.cpp:105`) and byte 0
    (USB ownership, `:119`) fail without a log;
  - the service thread stops without a log when `send()` to its socket fails
    with anything but EAGAIN, EWOULDBLOCK or EINTR (`:152-154`). It sends
    every 100 ms, connected or not.
- The car's USB stack enumerates devices:
  - the earlier capture above lists both pads;
  - `io-usb -c -vvv -d omap5-xhci ... -d hb-ehci-omap3 ...` runs (inventory
    `pidin-arg.txt`); `-c` only skips choosing a configuration for devices
    with more than one;
  - `devb-umass` runs for a USB stick at bus 0, port 1, device 2.
- `io-usb` writes nothing to the system log, even for USB sticks (inventory
  `sloginfo.txt`), so its silence says nothing about enumeration.
- `usbPowerMonitorOM5 ... OVERCURRENT_DETECT` repeats every 29 s in the
  inventory log as well: a periodic poll, not a fault.
- NBT-AAD, car-tested 2026-11-01 (its commits `531d9e4`, `d413820`, `0f5ea3d`):
  - a daemon connects to `io-usb` with libusbdi (default path, wildcard
    ident, `USBD_CONNECT_WAIT`) and gets insertions for every device;
  - control requests sent with a completion callback never completed on the
    car; synchronous `usbd_io` worked;
  - queued bulk reads with callbacks worked;
  - vendor `0424` is the head unit's own hub and network adapter;
  - `vdev-medialaunch` sends every new device an AOA request.

  NBT-AAD links the car's `libusbdi.so.2` dynamically (`-lusbdi`).

**Ruled out by the code**

- The lock's path: same table, owner, mode and link-count checks as
  `state/manager/surface.lock`, which works on the car.
- The socket pair: the MOST channel makes the same one in the same process.
- The capability: served.

**Not established, most likely first**

1. The checked open of `gamepad.lock`. Before dev76 the host's checked open
   of the application log failed with errno 89 on every start; the cause was
   never found and it stopped by dev79.
2. `getsockopt(SO_TYPE)`: nothing else in the manager runs it on the car.
3. For the GUI only, the silent `send()` stop.

**Open after that**

- Whether the SDK's static `libusbdi.a` works with the car's `io-usb`. The
  car's `/lib/libusbdi.so.2` is 44,696 bytes, the SDK's 54.8 KiB. If
  `usbd_connect` fails, link the car's library as NBT-AAD does.
- Whether `vdev-medialaunch` (`/etc/umass-enum_2100mA.cfg`, not in our dump)
  starts a HID driver that takes a DualShock exclusively. An Xbox One pad
  (class `ff`) is not HID.
- Our control requests are already synchronous
  (`src/gamepad/usb_configuration.cpp:42`), as NBT-AAD found necessary.

**Next:** a kit that logs the step and errno at each silent point above, or
the same start sequence on the QNX 6.5 VM (socket pair, `SO_TYPE`, the lock
created on qnx6, byte locks with a second thread).

## Endpoint expectations

These are direct descriptor observations published by device owners. They are
not manufacturer guarantees for every firmware revision or clone.

| Device | Configuration / interface / alternate | Interrupt IN | Interrupt OUT | Maximum packet size | Raw `bInterval` | Primary observation |
| --- | --- | --- | --- | --- | --- | --- |
| Xbox One `045e:02d1`, original model 1537 | 1 / 0 / 0 | `0x81` | `0x01` | 64 bytes each | 4 each | [Owner's `lsusb` capture](https://github.com/univrsal/input-overlay/issues/183) |
| DualShock 4 v2 `054c:09cc` | 1 / 3 / 0 | `0x84` | `0x03` | 64 bytes each | 5 each | [Owner's genuine Sony capture](https://forums.developer.nvidia.com/t/adding-kernel-driver/187090) |
| DualSense `054c:0ce6` | 1 / 3 / 0 | `0x84` | `0x03` | 64 bytes each | 6 each | [Published descriptor capture](https://github.com/nondebug/dualsense/blob/02338358b6b4be2d64dbf661ae2a47842156cfcf/lsusb-descriptor-info.txt) |

The DS4 discussion also contains an earlier off-brand `054c:05c4` dump; that
dump must not be confused with the later genuine `054c:09cc` capture. The Xbox
capture shows interface 1 alternate 1 using isochronous endpoints and interface
2 alternate 1 using bulk endpoints. Those are separate from interface 0's
interrupt controller channel. [DS4 captures](https://forums.developer.nvidia.com/t/adding-kernel-driver/187090),
[Xbox capture](https://github.com/univrsal/input-overlay/issues/183).

Linux xpad explicitly rejects Xbox One interfaces other than interface 0,
explaining that the three interfaces can share the same class/subclass/protocol.
It finds interrupt IN and OUT endpoints by direction and transfer type, uses
their actual endpoint addresses and intervals, and allocates a 64-byte input
buffer. Therefore matching `ff/47/d0` alone is insufficient to choose the Xbox
controller interface. [Linux xpad, `GIP_WIRED_INTF_DATA` and `xpad_probe`](https://github.com/torvalds/linux/blob/666539eecdf4db623f5e3c3e55cf9f2dfad24aa4/drivers/input/joystick/xpad.c).

Linux hid-playstation uses USB input report ID `0x01` and a 64-byte report for
both DS4 and DualSense, while the generic usbhid driver discovers interrupt
endpoints on the selected interface. These report lengths are parser contracts,
not a reason to hardcode endpoint addresses. [Linux hid-playstation](https://github.com/torvalds/linux/blob/666539eecdf4db623f5e3c3e55cf9f2dfad24aa4/drivers/hid/hid-playstation.c),
[Linux usbhid](https://github.com/torvalds/linux/blob/666539eecdf4db623f5e3c3e55cf9f2dfad24aa4/drivers/hid/usbhid/hid-core.c).

## QNX 6.5 attachment and lifetime

QNX documents a useful discovery split: a connection without callbacks has no
asynchronous I/O/event thread; its attachments have shared, descriptor-only
access. An attachment on a callback-enabled connection is exclusive and permits
I/O. The `usb` utility uses the descriptor-only connection pattern. This supports
probing configurations and interfaces without selecting the arrival event's
class as the final interface identity. Close the shared probe attachment before
the exclusive attachment, because a shared/exclusive conflict can return
`EBUSY`. [QNX `usbd_connect`](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.ddk_en_usb/usbd_connect.html),
[QNX `usbd_attach`](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.ddk_en_usb/usbd_attach.html).

`usbd_attach` accepts configuration, interface and alternate values in the
instance. `usbd_parse_descriptors` exposes a configuration/interface/endpoint
tree, and a subtree root restricts the search; its index is an occurrence index.
`usbd_endpoint_descriptor` takes the endpoint's address, not its position in the
descriptor list. Root endpoint enumeration at the selected interface and retain
the actual address, type, size and interval. [QNX `usbd_attach`](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.ddk_en_usb/usbd_attach.html),
[QNX `usbd_parse_descriptors`](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.ddk_en_usb/usbd_parse_descriptors.html),
[QNX `usbd_endpoint_descriptor`](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.ddk_en_usb/usbd_endpoint_descriptor.html).

The documentation describes exclusivity at the **device** level. Selecting an
interface does not establish that an independent audio driver can concurrently
own the other interfaces. The documentation also does not settle whether every
composite arrival notification's identifier class is the device class or the
interface class. Admit known Sony VID/PID arrivals before descriptor inspection,
including device class 0, and avoid claiming verified audio coexistence.
[QNX `usbd_attach`](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.ddk_en_usb/usbd_attach.html),
[QNX `usbd_connect`](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.ddk_en_usb/usbd_connect.html).

`usbd_detach` closes associated pipes and returns `EBUSY` if I/O is still
pending. Teardown must stop submission, abort/drain transfers, detach, free USB
allocations, then disconnect the last connection. [QNX `usbd_detach`](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.ddk_en_usb/usbd_detach.html).

The last ordering requirement is also supported by inspection of the shipped
ARM static library:
`/home/mobyfab/src/tools/qnx-6.5-tools/sdk/target/qnx6/armle-v7/lib/libusbdi.a`,
SHA-256 `0567f510340bd6fdb77781cca80dac5e1620e05b44ab9d15002460311f12db07`.
In that artifact, `usbd_detach` checks pending references before freeing the
device; `usbd_free` and `usbd_free_urb` use global memory pools;
`usbd_disconnect` destroys those pools when the connection count becomes zero,
then cancels and joins the callback thread. Its `usbdi_open` uses read-only
access without callbacks and read/write access with callbacks. Thus successful
read-only discovery alone does not prove permission to open the I/O connection
on the captured root-owned mode-0644 node. This is SDK binary evidence, not a
measurement of a running head unit.

## Configuration and minimal HID initialization

Attaching with a configuration/interface in the instance is not a documented
replacement for selecting the hardware configuration. QNX's class-driver
sequence explicitly separates attachment, descriptor retrieval, configuration
and interface selection, and opening endpoint pipes. The SDK's `usbd_attach`
implementation sends an attachment command and copies its returned instance;
it contains no separate configuration transfer. Server-side implicit
configuration was not established by this inspection. [QNX class-driver
sequence](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.ddk_en_usb/overview.html),
[QNX `usbd_select_config`](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.ddk_en_usb/usbd_select_config.html).

A conservative initialization policy is to read the current configuration,
preserve it when it matches the chosen descriptor, select the chosen
configuration only when the current value is zero, and reject an unexpected
nonzero configuration. Re-selecting the current configuration resets interface
alternate settings, endpoint halts and endpoint state, affecting other
interfaces of composite devices. Linux documents this explicitly in
`usb_reset_configuration`. [Linux USB core](https://github.com/torvalds/linux/blob/666539eecdf4db623f5e3c3e55cf9f2dfad24aa4/drivers/usb/core/message.c).

For the query, QNX's `usbd_setup_vendor` supports standard requests despite its
name. Open the control pipe using the device descriptor, allocate the one-byte
response with `usbd_alloc`, set up `USB_GET_CONFIGURATION` with `URB_DIR_IN`,
standard/device recipient, zero value/index, and submit synchronously with a
bounded timeout. Check both completion status and the exact one-byte response.
The shipped `sys/usb100.h` defines `USB_GET_CONFIGURATION` as 8 and
`USB_GET_INTERFACE` as 10. The latter can inspect the selected interface's
alternate setting with standard/interface recipient and its interface number
in the index. Preserve alternate 0 if already selected. [QNX
`usbd_open_pipe`](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.ddk_en_usb/usbd_open_pipe.html),
[QNX `usbd_setup_vendor`](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.ddk_en_usb/usbd_setup_vendor.html),
[QNX `usbd_io`](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.ddk_en_usb/usbd_io.html).

Avoid requiring an unconditional `SET_INTERFACE(0)` to succeed on a
single-alternate interface: USB permits that request to stall in this case,
and Linux explicitly handles it. If an alternate change is necessary, select
only the claimed interface, not unrelated audio interfaces. [Linux
`usb_set_interface`](https://github.com/torvalds/linux/blob/666539eecdf4db623f5e3c3e55cf9f2dfad24aa4/drivers/usb/core/message.c).

For raw DS4 buttons, a best-effort `SET_IDLE` with report 0 and duration 0
matches Linux usbhid: class/interface request `0x0a`, value 0, selected HID
interface index, no data. Linux ignores this request's return during report
descriptor parsing. DS4's captured HID subclass is 0, so do not require
`SET_PROTOCOL`, which HID specifies for boot-subclass devices. [Linux
usbhid](https://github.com/torvalds/linux/blob/666539eecdf4db623f5e3c3e55cf9f2dfad24aa4/drivers/hid/usbhid/hid-core.c),
[HID 1.11, sections 7.2.4–7.2.6](https://www.usb.org/sites/default/files/documents/hid1_11.pdf).

The source evidence does not identify a Sony vendor handshake required for
wired buttons. Linux identifies USB report 1 as the full 64-byte DS4 input
report and opens HID input before creating its Sony-specific devices. SDL
treats wired Sony DS4 input as enhanced reports already; its calibration-report
comment describes switching advanced input mode specifically for Bluetooth.
Thus omitting MAC, firmware, sensor-calibration and output-effect transactions
from a wired button-only service is a reasonable implementation inference,
pending a hardware test. [Linux hid-playstation](https://github.com/torvalds/linux/blob/666539eecdf4db623f5e3c3e55cf9f2dfad24aa4/drivers/hid/hid-playstation.c),
[SDL PS4 HIDAPI driver](https://github.com/libsdl-org/SDL/blob/a22d4f93ef1b648ac1049c03435614f6f3b0afc8/src/joystick/hidapi/SDL_hidapi_ps4.c).

## Implementation and validation recommendations

- Keep endpoint discovery common to the controller families. Select a recognized
  interface at alternate 0, require bounded interrupt endpoints, require both
  directions for GIP, and reject ambiguous or oversized layouts. Make interface
  0 selection an explicit Xbox rule, following xpad.
- Use the three observed layouts above as fixtures, plus reordered endpoints,
  wrong Xbox interfaces, Sony class-0 arrivals, missing endpoints and malformed
  descriptors. Endpoint numbers should come from descriptors at runtime.
- Keep hotplug callbacks short and transfer work to bounded driver state. QNX
  creates its own callback thread, so protect resources shared with it and
  synchronize teardown. [QNX `usbd_connect`](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.ddk_en_usb/usbd_connect.html).
- Treat USB values as untrusted, and treat the table's intervals as raw
  descriptor bytes rather than measured latency. Passing host fixtures or
  cross-compilation does not establish successful exclusive attachment,
  permissions, transfer cancellation, or GUI/application handoff on the car.
  Test Xbox One and PS4 attach, input, unplug, replug and panel handoff there;
  retain DualSense as experimental until hardware validation is available.

No traffic was sent to a vehicle during this research. The available capture
and primary sources are sufficient to implement descriptor-driven endpoint
selection without another endpoint dump; physical-device ownership and runtime
handoff remain hardware validation items.
