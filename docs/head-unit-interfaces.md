<!-- SPDX-License-Identifier: MPL-2.0 -->
# Head unit interfaces

Facts about the interfaces the manager uses on the head unit (MOST through
DSI, the iDrive PluginApp framework and its car data), read from an `/opt`
dump of firmware **NBTevo_Y21432J** (`sys/etc/nbt_version.txt`). They come from
exported symbol tables, relocations, strings, configuration files and static
analysis of ARM code. No vendor code or file is copied here; the dump stays
outside this repository (see `docs/car-inventory.md`).

"Confirmed" below means confirmed **in the library code of this firmware**;
runtime behaviour is checked with the bench or car steps in
`docs/bench-checklist.md`. Other firmwares may differ: run
`tools/most_catalogue.py` on their `HmiMain3D` before relying on an id.

## Where things run

- The head unit has two CPUs, `omap` (HMI, `/opt`) and `jacinto`, joined by
  QNX networking (`on -f hu-jacinto ...`, `/net/hu-jacinto/...` in
  `car/bin/collect_cardata.sh` and `sys/etc/starter.cfg`).
- `starter.cfg` starts iDrive as `/opt/hmi/bin/zipped/HmiMain3D
  -c=DSI,MOSTSubSystem ...` and sets `DSI_TRANSPORT_LOAD_ORDER=mp`,
  `V850_MOSTSUBNET_OMAP_DIRECT_REGISTRATION=1` and the DSI trusted address
  `160.48.199.119` (RSE: `.118`). The MOST service role is `MOSTSubSystem`.
- HmiMain3D and every HMI library (`libAppFramework.A.so`, `libhmiremoting.so`,
  `libhmiplugin.so`) link the GNU C++ runtime (`libstdc++.so.6`) and the
  `.gpp` flavour of the DSI libraries. The manager and plugin use that same
  flavour and GNU C++ ABI; no C++ library types cross the manager's bridge.

## DSYSMOSTServer (MOST over DSI)

Source: `sys/lib/libimp_sys_most_base_nbt.cpp-ne.so.17` (proxy, stub, client
base and serialisers for `DSYSMOSTServer`) and
`sys/lib/libsys_dsi_colibry.gpp.so.8`.

**Version 4.5, confirmed.** A static initialiser sets
`CDSYSMOSTServerProxy::sVersion = CHBVersion(4, 5, 0)`.
`DSYSMOSTServer::createInterfaceData()` describes 18 requests, 10 responses
and 5 attributes.

Requests (the `UpdateIdEnum` each `CDSYSMOSTServerProxy` method sends):

| Id | Request | Arguments |
| --- | --- | --- |
| 0x0 | `requestRegisterDevices` | client id (u32), FBlock keys (vector of u16) |
| 0x1 | `requestRegisterDevCtrls` | client id (u32), FBlock keys (vector of u16) |
| 0x2 | `requestProcessMessage` | client id (u32), `MessageType` |
| 0x3 | `registerMOSTClient` | (reply 0x80000000) |
| 0x4 | `requestUnregisterDevices` | client id, keys |
| 0x5 | `requestUnregisterDevCtrls` | client id (u32), FBlock keys (vector of u16) |
| 0x6 | `requestChangeInstID` | client id, FBlock key (u16), new instance (u8) |
| 0x7 | `requestCleanUp` | client id |
| 0x8 | `unregisterMOSTClient` | client id |
| 0x9 | `registerMOSTSubnet` | (reply 0x80000008) |
| 0xA | `unregisterMOSTSubnet` | client id |
| 0xB | `requestSetRegistryFilter` | client id, keys |
| 0xC | `requestSetCommunicationCtrl` | `CommCtrlMode` |
| 0xD | `requestRegisterExtDevices` | client id, `FBlockKeyStringType` vector |
| 0xE | `requestProcessMessageVector` | client id, `MessageType` vector |
| 0xF | `registerMOSTClientExt` | client name (string); reply 0x80000000 |
| 0x10 | `unregisterMOSTClientExt` | client id |
| 0x11 | `requestEnableNotificationProxy` | client id |

The three requests the manager sends (0xF, 0x1, 0x5) have these ids. The 0x5
argument list (`WriteRequestUnregisterDevCtrlsArgList(stream, unsigned int,
THBVector<unsigned short>)`) is the same as 0x1's, so the shape assumed for
`release=1` (client id, count, keys) is the library's. Whether the server
accepts it at runtime is still a bench check.

Responses and informations (`CDSYSMOSTServerClientBase::processEvent` and its
virtual table):

| Id | Meaning | Payload |
| --- | --- | --- |
| 0x80000000 | `responseMOSTClient` | client id (u32), device id (u16) |
| 0x80000001 | `informationProcessMessage` | `MessageType` |
| 0x80000002 | `informationChangeInstID` | FBlock key (u16), new instance (u8) |
| 0x80000003 | `informationConnect` | vector of `DeviceInfo` {u16, u16} |
| 0x80000004 | `informationDisconnect` | vector of u16 |
| 0x80000005 | `informationActivate` | vector of u16 |
| 0x80000006 | `informationDeactivate` | vector of u16 |
| 0x80000007 | `informationCleanUp` | `CleanUpRequest`, DevCtrl (u16), device id (u16) |
| 0x80000008 | `responseMOSTSubnet` | subnet client id (u32) |
| 0x80000009 | `informationRegistry` | vector of `RegistryEntry` {u16, u16} |

Attributes:

| Id | Attribute |
| --- | --- |
| 0xC0000000 | `MOSTCommunicationState` |
| 0xC0000001 | `Registry` (the FBlocks present on the ring) |
| 0xC0000002 | `DeviceID` |
| 0xC0000003 | `CommunicationCtrl` |
| 0xC0000004 | `ExtDeviceID` |

FBlock keys are `fblock << 8 | instance` (`0xE501` and so on), as already used.
The manager subscribes to 0xC0000001-3 and 0x80000001-7, then to 0xC0000000
after registration; the registry it receives is the list of FBlocks present
in the car, a discovery source that needs no new request.

The Registry payload (`DSYSMOSTTypes::DSIReadRegistry`) is an `int` count,
then that many `RegistryEntry` of two `u16` (`DSIReadRegistryEntry`), with no
padding between them. The library's own client (`CHBMOSTClientImpl::
onRegistryUpdate`) uses it only when the data state is 2 and collects the
**second** word of each entry into its sorted set of FBlock keys; the first
word is not named (likely the device address, unconfirmed). The data state is
not in the payload: the response constructor takes it from the stream header
and reads no payload for the states 0x201, 0x203 and 0x204. With the debug
log on, the manager GUI logs the Registry when it arrives and again when it
changes or after the link was lost (`MOST registry: <n> entries`, then 16
`key/first` pairs per line, at most 128); `nbt_most_registry` in
`src/most/codec.cpp` parses it.

`MessageType` on the stream, from `DSIReadMessageType` and
`DSIReadMessageHeaderType`: transmission type (u32), a variant {tag (u32),
`eTransmissionTXControl` or `eTransmissionInfo` (u32)}, the header {u16, u16,
FBlock (u8), instance (u8), function (u16), op type (u8), u32}, then the data
as a length-prefixed buffer. With the DSI stream's alignment this gives the
offsets `src/most/codec.cpp` parses: header words at 12 and 14, FBlock 16,
instance 17, function 18, op 20, the unnamed u32 at 24 (not used), data length
at 28, data from 32. The library's log format
`[%04X->%04X] %02X %02X %04X %02X %04X`
prints the two header words as source -> target; which of offsets 12/14 is
which was not checked (the controller decoder does not use them).

`CDSIClient`'s virtual table has **8** entries. Slots 3, 4 and 7 are its pure
virtuals, the ones `src/most/dsi_client.cpp` replaces from C++. Slot 7 is
`processResponse(CDSIIStream&)` (named by the `CDSIStatisticsDSIProxy`
subclass); slots 3 and 4 are, by HmiMain3D's own DSI handler,
`ComponentConnected` and `ComponentDisconnected`. `src/most/dsi_client.cpp`
copies exactly these 8 entries; `tests/fake_dsi.c` has a table of the same size.

The `.gpp` layout was rechecked against
`libsys_dsi_colibry.gpp.so.8` (SHA-256
`f559c5c1455ed62e376ec72fdb77d5211b748bc9167f9c6866083db1515d19b4`).
`CDSIBase::CDSIBase` zeroes the client and server parties at 0x110 and 0x118.
`CDSIClient::CDSIClient` uses the channel, socket, IPC fd and final pointer at
0x130, 0x138, 0x140 and 0x148, so the minimum client size is 0x14c.
`CDSIConnection::CDSIConnection` writes through 0x78.
`CDSIStreamBase::CDSIStreamBase` stores the length at 0x08 and payload pointer
at 0x10; `CDSIClient::handleDataResponse` reads the stream id at 0x48. The
vfn 0 complete and deleting destructors occupy slots 0 and 1, followed by the
confirmed pure-virtual slots above. `include/nbt/platform/dsi_abi.hpp` records
these values; allocated objects retain 64 bytes of margin.

Not found: a built-in MOST message logger usable on the car. The
`sys_most_mss_MOSTSpy` scope writes to Harman's trace server
(`trace-recorder-app`, to `160.48.199.99:851`).

## Notification (static findings, NBTevo_Y21432J)

The references below are virtual addresses in the dump, not file offsets.
`HmiMain3D` SHA-256 is
`f397906974addd57c946dfd04243477506560893f3ea405e81b4369ce7a47df5`;
`sys/bin/most` is
`b114af0fed2a02a89245c133b79f68e0df0120084b5b67ab1fb4abba444af511`;
`libimp_sys_most_base_nbt.cpp-ne.so.17` is
`63f534151854b50e9a8fd4cdd19f4d8837828d6c9a15ee9097dc97d609f3bc8f`.
REA decompilation was checked against local ARM disassembly. The large HMI
REA decompile hit its client deadline; HMI references here use disassembly.
Raw evidence remains outside the repository.

- **Set format.** `CPxyBase::CNotification` vtable `0x03822768`,
  dispatcher slot 6 `0x0203ca04`; the Set builder is `0x0203caa8`.
  It writes one control byte (0..3), a big-endian u16 device address, then
  an uncounted stream of big-endian u16 function IDs. IDs are not packed
  into 12-bit pairs. The generated writer limits this stream to **8 bytes**
  (four IDs). The bridge therefore splits larger instance subscriptions
  into batches of four, adjusting A2's proposed single message per instance.
  `CSvcFunctionProxy` at `0x0204e340` writes its function ID with
  `writeUInt16`, gets its own address through the MOST service, and calls
  the builder with control 1 at `0x0204e4d8`.
- **Controls.** In the MOST base library, `CHBLogDevBase::setAllNotification`
  (`0x000d5874`) uses 0; `setGroupNotification` (`0x000d566c`,
  `0x000d56d4`) uses 1; `deleteAllNotification` (`0x000d586c`) uses 2;
  `deleteGroupNotification` (`0x000d562c`, `0x000d56ac`) uses 3.
  `setOutGroupNotification` (`0x000d50f0`) emits its own address and u16 IDs,
  independently confirming the HMI format and the four-function batches.
- **Check.** `CPxyBase::CNotificationCheck` vtable `0x03822720` uses
  dispatcher `0x0203c0e4`. Its Status path reads a u16 and a remaining-byte
  stream capped at 200 bytes (`0x02051e30`, `0x02051b08`). The leading u16's
  meaning and Check request construction remain unconfirmed; the host does
  not use NotificationCheck. No claim about its list entries is made.
- **Address ownership: per client.** The server's registration helper
  `0x0012e278` uses the current session ID at object offset `0x8c`.
  Standard client wrappers `0x0012e55c` and `0x0012e56c` pass the server's
  base address at `this+0x1ac`. At `0x0012e2fc` and `0x0012e3b4` the server
  adds that base to the session ID, constructs the client with this address,
  and returns it through response slot `0x7c` (responseMOSTClient).
  The base library's `CHBMOSTClientImpl::responseMOSTClient`
  (`0x0010acb4`) saves the returned address as its own device ID.
  **Choice: subscribe and clear only this client's functions/address.**
  Neither the numerical base address nor session IDs are hard-coded here.
- **Notification proxy.** Request 0x11 in the base library (`0x00125b84`)
  only serialises the request. The implementation lives in `sys/bin/most`,
  not that library: handler `0x0012cc94` looks up a client and calls
  `0x00149efc`, allocating its optional notification state. Report routing
  at `0x00130034` consults this state through `0x00152454` when present.
  The bridge does not enable this optional proxy.
- **Routing.** `sys/bin/most` at `0x00130c0c` compares the destination's
  high nibble with the server's base. For a matching per-client destination
  it looks up the low part in the client map and calls `0x00147718` for
  that client. This is not unconditional delivery to all clients. Other
  subnet and device-control paths also exist. Whether unsolicited Vehicle
  statuses reach a monitor without `ctl` is still A1's live check.
- **Cleanup.** Client unregistration at `0x0012f408` collects its device
  controls (`0x00149f74`), unregisters them upstream when applicable,
  removes local associations and destroys the client. This is distinct
  from information 0x80000007: the base-library handler `0x0010a14c`
  invokes `CHBDeviceAdmin::netOffCleanup` and removes queued external
  messages for the relevant cleanup kind. These paths do **not** prove
  that a crashed client's remote FBlock notification entries are cleared;
  automatic disconnect cleanup and eventual expiry remain bench questions.
- **Retry.** HMI `CSvcFunctionProxy::onNotificationTimeout`
  (`0x0204db74`) checks remaining consumers and connected state, invokes
  its notification action and doubles the stored timeout while below
  64000 (`0x0204dc5c`). The bridge uses 1 s initial retry, doubling to a
  64 s cap until each requested Status has arrived. The HMI's initial
  timeout value was not established. It is not claimed to be 1 s.

Not established: initial values after Set, behaviour with several clients at
once, and the server's cleanup after a client process dies.
Use the MOST section in [bench-checklist.md](bench-checklist.md).

## MOST function catalogue

HmiMain3D (`/opt/hmi/ID5/bin/HmiMain3D`, unstripped dynamic symbols) carries
generated classes for every MOST function block it uses:
`CPxy<FBlock>::C<Function>` where iDrive controls the block (proxy) and
`CStb<FBlock>::C<Function>` where iDrive implements it (stub).
[most-functions.tsv](most-functions.tsv) lists 2148 functions of 80 FBlock
classes with FBlock and function ids, generated by `tools/most_catalogue.py`.
How the ids are recovered:

- slot 4 of each FBlock class vtable (`vtable for CPxyVehicle`) is the routine
  that initialises the function members, each with
  `init(this + offset, this, function_id)` (`CSvcFunctionProxy` at
  `0x204d458` for proxies, `0x204ebf0` for stubs, in this build);
- the FBlock constructors store each member's vtable, which names its class,
  at the same offset, inline or through an out-of-line member constructor;
- a registration call after each factory passes the FBlock id (`Vehicle`:
  `mov r0, #5`).

Checks: every id known independently matches. The controller functions
(0xE5: 0xC10, 0xC20 on CCS; 0xC46, 0xC47, 0xC4F on CCS35up) are those
`src/most/ccs.cpp` decodes; NBT-AAD's gear (0x05/0x228), parking brake (0xCB5),
speed (0x621), direction (0x626), GNSS (0xCD: 0x203, 0x20B) and steering
wheel (0x15) ids map to the functions named below. 1994 of the 2148 are
named; the rest are the MOST standard functions 0x000-0x002 (`FktIDs`,
`Notification`, `NotificationCheck`) and members whose class the analysis
could not place (mostly media and tuner blocks). Thirteen classes initialise
their members through a generic base and are missing: the AudioDisk blocks,
`DebugApplication`, `Diagnosis*`, `MapControl`, `NetBlock`, `Network`,
`NetworkMaster`, `SyncSource` and `TiTelematics`. Instances (the FBlock key's
low byte) are not part of the catalogue.

FBlock ids: Network 0x00, NetBlock 0x01, NetworkMaster 0x02, ConnectionMaster
0x03, Vehicle 0x05, Diagnosis 0x06, ManMachineInterface 0x10, ControlElements
(MFL, EC, CECenterStack, CEFondCommander) 0x15, AudioMaster 0x20,
AudioAmplifier 0x22, HeadPhoneAmplifier 0x23, AuxiliaryInput 0x24,
AudioDiskPlayerChanger 0x31, MultiMediaPlayer 0x32, AmFmTuner 0x40, TVTuner
0x42, DABTuner 0x43, SatRadio4 0x44, Telephone 0x50, GeneralPhoneBook 0x51,
Navigation 0x52, Bluetooth 0x54, Monitor 0x6F, Climate 0x71, HUD_Terminal
0x82, WLAN 0xA0, Kleer 0xA1, NFC 0xA2, TollCollect 0xAB, Browser 0xBE,
CANDevices* 0xC0, MPEG_TS_Decoder 0xC1, TouchCommandInterface 0xC5, Services
0xC9, NavigationInfo 0xCD, ISpeech/ISpeechTTS 0xCE, HMIControl 0xCF,
SystemFunction* 0xD1, MassStorageControl 0xD3, SWUpdate 0xD4,
VideoConnectionMaster 0xD7, VideoSink 0xD8, EarlyVideoControl 0xD9,
MapControl 0xDA, TiTelematics 0xDB, DataComIP 0xDC, DUMM 0xDD,
TelematikTCU/TelematicAssistECall 0xDE, TeleX 0xDF, KombiInterface/KIListBasic
0xE0, HUDInterface 0xE1, Camera* 0xE2, SoundApplications 0xE4,
CentralControlSystem* 0xE5, DebugApplication 0xEB, PIM 0xED. Blocks sharing an
id (CANDevices*, SystemFunction*, Camera*, ControlElements*) differ by
instance.

For the bridge and the controller:

| FBlock | Function ids |
| --- | --- |
| Vehicle 0x05 | 0x21F `TerminalStatus`, 0x220 `ClampStatus`, 0x221 `CarSpeed`, 0x224 `IgnitionKey`, 0x228 `GearBox`, 0x229 `DrivingCondition`, 0x22B `EngineStatus`, 0x22C `ReverseGear`, 0x409 `ParkingBrake2`, 0x620 `SteeringAngleDriver`, 0x621 `CarSpeed2`, 0x626 `DrivingCondition2`, 0x720 `LightStatus`, 0x902 `VehicleState`, 0xC31 `DayNight_ChangeOver`, 0xC76 `DisplayStatusDSC`, 0xCB2 `ValueSpeedIndicator`, 0xCB5 `HandBrake`, 0xCC2 `GearIndicator`, 0xCF1 `DSCSwitchHMI`, 0xD01 `StateStandstillFunctions`, 0xD06 `ValueSpeedIndicator2` |
| Camera* 0xE2 | NightVisionL6 0x662, 0x663; RearView 0x674 `RearViewControl`, 0x675 `RearViewStatus`, 0x677 `RearViewImageState`; SideView 0x690 `SideViewControl`, 0x691 `SideViewStatus`, 0x692 `FrontViewImageState`; TopView 0x6A0 `TopViewControl`, 0x6A1 `TopViewStatus`, 0x6A2 `TopViewImageState` |
| CANDevicesParkingFunctions 0xC0 | 0xC06 `ParkingFunctionsAvailable`, 0xC25 `PDCDistanceFront`, 0xC26 `PDCDistanceRear`, 0xC27 `PDCStatusFunction`, 0xC60 `ParkingSystemState`, 0xC6A `ParkingSystemState2`, 0xD33 `ParkingSystemState3` |
| HMIControl 0xCF (stub: iDrive implements it) | 0x200 `GraphicalContext`, 0x202 `GraphicalContext2`, 0x206 `HMIInputMode`, 0xC06 `HMIActiveApplication`, 0xC21 `SplitScreenState`, 0xC22 `GraphicalContextPath`, 0xC32 `PluginLoad`, 0xC33 `PluginUnload` |
| CentralControlSystem 0xE5 | 0xC06 `ccsState`, 0xC10 `ccsRotaryEncoder`, 0xC20 `ccsPushButton`, 0xC41 `ccsTouchControl` |
| CentralControlSystem35up 0xE5 | 0xC46 `RotaryEncoderState`, 0xC47 `PushButtonsState`, 0xC4A `RotaryEncoderControl`, 0xC4C `ccsControlTouchCCU`, 0xC4F `PushButtonsState2` |
| ControlElementsMFL 0x15 | 0x400 `TelephoneButton`, 0x401 `AudioVolumeButton`, 0x511 `SearchSwitch1`, 0x512 `PTTButton1`, 0x513 `SpecialFunctionButton`, 0xC30 `RotarySteps`, 0xC31 `RotaryPress` |
| AudioMaster 0x20 | 0x200 `VolumeActual`, 0xC08 `VisibleVolumeIndicator`, 0xC30 `AMGenVolume` |
| NavigationInfo 0xCD | 0x202 `GPSPosition`, 0x203 `GPSPositionExtension`, 0x20B `PositionHeadingSpeed`, 0x210 `StatusGuidance`, 0xC10 `CurrentPositionCountryCode` |
| SystemFunctionPowerMode, SystemFunctionVehicleState 0xD1 | 0x430 `PowerState`, 0x434 `PowerStateDetailed`; 0xC52 `InteractionState`, 0xC53 `RequestStatePWF` |

So NBT-AAD's "gear" 0x228 is `GearBox`; `ReverseGear` 0x22C is a separate
one-byte status (its handler accepts op 0xC, Status, and stores one byte);
its parking-brake fallback 0xD01 is `StateStandstillFunctions`; its "master
volume" 0xC08 is the HMI's `VisibleVolumeIndicator`. Names say what a
function is, not its value format; formats still come from captures.

0xC08's Status is 5 bytes, `01 00 LEVEL MIN MAX`, sent as the knob changes
the volume of the source being heard: `MIN` is that source's floor, 0 for the
radio and 25 for navigation prompts, a phone call and the browser source
(audio connection 0xb8, what applications and NBT-AAD's native audio play
through). The floor is iDrive's own, per source, and the knob stops there;
no way to lower it is known. (F30, NBT Evo ID6, captured by an
NBT-AAD contributor, 2026-11-21; NBT-AAD issue #181 and pull request #278.)

## The PluginApp framework

### Our plugin's ABI, checked against `libAppFramework.A.so`

Every reconstructed assumption in `plugin/plugin.cpp` matches the library and
BMW's own plugins (`ActiveSeating`, `SecurityMenu`):

- `vtable for bmw::PluginApp`: 34 primary slots; slot 22 `onInit()`, slot 23
  `onFinalize()`, as `kOnInit`/`kOnFinalize`. The class also derives from
  `bmw::PluginAppHandler`, with a second vtable pointer at object offset 4
  (offset-to-top -4: thunks for the destructors and `handleMessage`, and
  `nextExecTime`/`dispatch`). The plugin copies the 2 + 34 words of the
  primary table and swaps only the primary pointer, which is right: none of
  the secondary entries is overridden.
- `vtable for bmw::Runnable`: destructors (2 slots), then pure `run()`, as
  the plugin's `pollVtable`.
- `PluginApp`'s constructor writes members up to offset 0x15C; the plugin
  allocates 0x200 (ActiveSeating allocates 0x300 for its own subclass).
- `pluginAppInfo` is 0x18 bytes in every plugin: {u16 major, u16 minor,
  name, vendor, u32 interface version, create, destroy}; ActiveSeating's is
  {1, 0, ..., 1, ...} like ours.
- `addHandler(id, type, Delegate const&)` copies the delegate as two words,
  {object, stub}, and `handleEvent` calls the stub only when the object word
  is set.

This is build-level agreement with this firmware's libraries, not runtime
proof.

### Other firmware versions

ID6 Apps (MIT, `source/docs/COMPATIBILITY.md`) compared the framework
libraries of ten builds, M17345I (ID5_1703, 2017) to Y24073J (ID5_2403,
2024), Y21432J among them: all export the same framework symbols with the
same names, sizes and types, and an audit of instruction prefixes and vtable
slots passes for its plugins on each. `libAppFramework.A.so` is 51524 to
51644 bytes across them; `libm.so.2`, `libsocket.so.3` and `libstdc++.so.6`
are byte-identical in most. So the slots above most likely hold on those
builds; only Y21432J is checked here.

### Two-CPU units

On some cars an SSH unlock lands on `hu-jacinto`, where `/opt/hmi/ID5/lib`
and `/opt/sys/etc` are empty: the HMI side is read over Qnet
(`/net/hu-omap/fs/sda0/...`, `on -f hu-omap ...`), and the plugin folder
`/var/opt/hmi/data/ID5/Plugins` is on the OMAP (ID6 Apps, `PORTING.md`). The
installer already writes through `/net/hu-omap`.

### Loading

HmiMain3D's remote HMI start loads `ActiveSeating` and `SecurityMenu`, then
the `Plugins` and `PluginsCoded` folders under `/opt/hmi/ID5/data/ro/`. BMW's
own coded plugins sit in `PluginsCoded` with `.p12`/`.p7b` files; NbtAad sits
in `Plugins` without them. `ManagedPluginApp::init` reads the `.manifest`,
needs a `<plugin>` tag, a name, a UI description, a library with a
`pluginAppInfo` entry point and a valid connector, and can refuse with
"Plugin is not allowed" (the rule was not traced; on the car it did not
refuse NbtAad, whose manifest has our plugin's shape: see "Seen on the car"). The `HMIControl` stub
implements `PluginLoad` (0xCF/0xC32) and `PluginUnload` (0xC33) for the
`Plugins/` location: plugins can be loaded and unloaded over MOST at
runtime. Not used by the manager; noted for bench work.

### Car data calls

BMW's plugins import `cdsAddPropertyChangedEventHandler(name, unsigned int,
Delegate<bool, const wstring&>)`, `cdsRemovePropertyChangedEventHandler(name)`,
`cdsIsPropertyAvailable(name)` and `cdsSetPropertyAsync(name, ident, data,
length)`. The path of a call, traced through the binaries:

1. `bmw::PluginApp` forwards to its `IDispatcher` (`PluginApp + 0x58`; vtable
   offsets 0x98 set, 0x9c add, 0xa0 remove, 0xa4 available).
2. The dispatcher is HmiMain3D's `CLocalPluginAppConnector`, which forwards to
   its `CLocalCommAdapter`.
3. The adapter calls the car-data provider (`libhmiremoting.so`,
   `CCarDataMainProvider`): add becomes `bindProperty(handler, name, ident,
   unsigned int)` with the adapter's own event handler and identity string;
   remove becomes `unbindProperty`; available becomes
   `isPropertyAvailable(name)`.
4. The `unsigned int` is the subscription's **interval limit**: it creates a
   `CCarDataProviderDebouncer` for that subscription (0 means none, every
   change is delivered), checked against the system clock; the remote app
   server logs the same value as `debounceTimer` and `key_intervalLimit`.

No permission check is made on the plugin's bind path itself
(`_bindProperty` goes straight to `bindPropertyChangedEvent`). Whether
property access is filtered elsewhere for local plugins was not traced in the
code; on the car, NbtAad received `hmi.graphicalContext` ("Seen on the
car"). The
certificate-based checks (`AuthPermManager`, `AuthToken`) belong to remote
apps.

## Car data (CDS)

`libhmiremoting.so` (`/opt/hmi/ID5/data/ro/hmiplugins/`, loaded as the
`RemotingBundle`) holds both the car-data service and BMW's remote app server.

- **Catalogue.** 258 registered `TPC<CCdsProperty<Name>>` creators, of which
  146 property classes export `onStatus()`. Values come from
  iDrive's internal data model (ProcUnits), not from MOST directly, so the
  CDS-to-MOST mapping is not established by these getters.
  [car-data-properties.tsv](car-data-properties.tsv) records all 258 names,
  creator IDs, ProcUnit IDs, 142 statically recovered JSON formats and
  method addresses. `tools/cds_catalogue.py` reads the ELF only. The remaining
  formats and all direct MOST mappings are explicitly unknown. Among them
  everything the bridge plans:

  | Name | JSON format |
  | --- | --- |
  | `driving.gear` | `{"gear":%d}` |
  | `driving.parkingBrake` | `{"parkingBrake":%d}` |
  | `driving.speedActual` | `{"speedActual":%hu}` |
  | `driving.speedDisplayed` | `{"speedDisplayed":%hu}` |
  | `driving.keyPosition` | `{"keyPosition":{"terminalStatus":%d,"vehicleConditionStatus":%d,"terminalStatus30b":%d}}` |
  | `driving.DSCActive` | `{"DSCActive":%d}` |
  | `driving.mode` | `{"mode":%d}` |
  | `sensors.PDCStatus` | `{"PDCStatus":%d}` |
  | `sensors.PDCRangeFront`, `...Rear` | `{"PDCRangeFront":{"outLeft":%d,"left":%d,"middleLeft":%d,"middleRight":%d,"right":%d,"outRight":%d}}` |
  | `vehicle.dayNightChangeOver` | `{"dayNightChangeOver":%u}` |
  | `vehicle.powerStateHeadUnit` | `{"powerStateHeadUnit":%u}` |
  | `hmi.graphicalContext` | `{"graphicalContext":{"visibleContext":%d,"focusIndex":%u,"listIndex":%d,"widgetType":"%s",...}}` |
  | `navigation.GPSExtendedInfo` | `{"GPSExtendedInfo":{"altitude":%d,"heading":%.4f,"quality":%d,"speed":%d}}` |

  Namespaces: driving (41 names), navigation (22), climate (22), sensors (21),
  communication (21), controls (18), vehicle (17), comfort (13), rhmi (12),
  hmi (9), engine (8), entertainment (7) and a few single names
  (`specialvehicle.menucontrol`, `mmi.rseAccess`, ...). The JSON formats are
  next to the names in the library's strings.
- **Access control.** `CCarDataMainProvider::availabilityOfProperty(name,
  IAuthPermHandler*, ECarDataPropertyAccess)` and per-session permission
  managers serve remote apps; see "Car data calls" for the plugin path.
- **Remote app server.** The same library is BMW's Etch remoting server
  (`CRemotingServer`, `bmwremoting_*`, `CRemotingServiceSession::cds_*`), the
  BMW Apps interface for phones. `conn/etc/ipce.cfg` sets its URL to
  `tcp://127.0.0.1:4004` (RSE `160.48.199.118:4004`/`4008`); `IPCEServer`
  bridges the browser to it (FastCGI `/dev/shmem/ipce-fastcgi.socket`,
  WebSocket port 8082). The server creates a `CAuthPermManager` and loads a
  certificate revocation list: clients authenticate with BMW-issued app
  certificates. A development certificate (`BMWAppKitDevelopment.p12/.p7b`)
  ships in `/opt/conn/etc/`. The manager does **not** use this path: it would
  mean authenticating to iDrive with BMW's credential, outside the plugin and
  MOST interoperability this project relies on. Recorded so nobody adopts it
  by accident.

## The car's make and iDrive's look

- **Make.** `/var/opt/conn/carplay/carplay.ini`, `[General]`: `Manufacturer=`,
  `OEMIcon=` (a logo under `/opt/conn/data/carplay/`: `OEM_BMW.png`,
  `OEM_BMWi.png`, `OEM_MINI.png`, `OEM_RR.png`, `OEM_TOYOTA.png`) and
  `OEMIconLabel=`. The user's G31 has `BMW` in all three (NBTevo_Y21432J).
  NBT-AAD reads the make there too, the logo before the manufacturer, since a
  MINI keeps `Manufacturer=BMW`; the Supra has `Manufacturer=Toyota`. The
  file exists only on a car coded for CarPlay. The manager's Automatic theme
  uses it (`src/model/theme.cpp`); MINI and Supra are not car-verified.
- **iDrive's own choice.** `HmiMain3D` takes `-brand=bmw|mini|bmwi|rr`
  (default `bmw`) and loads that skin from `/opt/hmi/ID5/data/ro/<brand>/`;
  `pidin` shows no `-brand` on the G31. This firmware holds `bmw`, `bmwm` and
  `rr` only: no MINI or Toyota skin here to measure.
- **BMW's iDrive 6 look.** In the firmware (`bmw/id6l/widgetasset`,
  measured, not copied): a near-black page (#090909 to #171818), square
  shapes with a slanted leading edge on buttons, a dark orange-red fill
  under the cursor (#5a1600) with a bright edge and underline (about
  #ef5600), hairlines under headlines and between list rows. Tile colours
  (`colorregistry.pbc`): entertainment #e1b900, communication #056bd0,
  navigation #6aad00, ConnectedDrive #0074e7. On BMW's press photograph of
  the G30's home screen the selected tile's header is filled deep red (about
  #980808, lighter to the left) on tiles of #181818 over a #080808 page, and
  the page arrow is red (#f80808).
- **Supra** (Toyota's "Audio Multimedia & Connected Services" guide for the
  2020 GR Supra, 6.5 and 8.8 inch displays): the same orange-red on black, as
  a glow under the selected row that fades to the right (#782808 to
  #c82808), a bright underline (#f85828), a curved bracket at the left and a
  thin line under the title. No blue.
- **MINI** (an infotainment report on the 2020 Cooper SE, 8.8 inch display):
  a solid bar on the selected row, coloured by menu: blue in MINI Connected
  (#0888d8), cyan in Communication, green in vehicle settings; a deep blue
  main menu. No lines between rows.

Photographs wash colours out a little; the themes take the saturated end of
what was sampled (`src/gui/theme.cpp`). None is checked against a MINI or a
Supra in person.

## At run time

**Network services on Wi-Fi Direct (from the `/opt` dump, `pidin` and a
manager debug log).** `/opt/conn/bin/DnsMasq -C /opt/conn/etc/dnsmasq.conf` serves
UDP 53 (a socket bound to the `wfd0` address alone gets `EADDRINUSE`, errno
248), with `/dev/shmem/resolv.conf` as its upstream (the BMW APN) and
`/opt/conn/etc/hosts` as its names. `/opt/conn/bin/lighttpd -f
/opt/conn/etc/lighttpd-nbt.conf` binds `0.0.0.0` port 80. It is iDrive's
internal web server for its own browser (`BrowserRenderer`, `DevCtrlBrowser`)
and BMW Apps, not a phone service. From its configuration (Y21432J):
`/service/` goes to `IPCEServer` by FastCGI (the remote app path above);
`/journaline/` to a FastCGI server on `127.0.0.1:8889` (DAB Journaline text
pages); requests for host `160.48.199.119` serve `/mnt/share/conn/AppFS/`
(BMW Apps content); `/cgi-bin/` runs CGI; `/trace` and `/core` serve
`/mnt/quota/sys/` with directory listings and WebDAV (trace and core files,
an engineering leftover); anything else comes from
`/fs/sda0/opt/car/data/htdocs/`, empty in the dump. No authentication module
is loaded. Whether a phone on `wfd0` reaches it has not been tried. ISC
`dhcpd` serves `wfd0` from a generated `/var/opt/conn/dhcpd.wfd0.conf` that has no `domain-name-servers` and no
`routers` option: a phone on the group gets an address and nothing else, so
Android cannot even run its connectivity check and the network has no
internet. Android then makes it the phone's default network only when it was
joined from Settings (AOSP `ClientModeImpl`: explicitly selected means the
connecting UID holds `NETWORK_SETTINGS`) and the user chose to stay
connected; joined from the camera app, mobile data stays the default
and the install page at `172.16.222.1` times out. Android Auto is unaffected:
its phone asks for the group as a local-only network.

A `tools/car_inventory.sh` run on a car with NBT-AAD's plugin installed and
Android Auto connecting gives runtime facts the dump cannot:

- **The dump is this car's `/opt`.** Of the 28737 files the car lists under
  `/opt/hmi`, 28735 have the dump's size; one PNG is truncated in the dump,
  and `bin/zipped/HmiMain3D` is the `ziploader` view of `bin/HmiMain3D`. The
  files the run copied (`nbt_version.txt` NBTevo_Y21432J, `starter.cfg`, both
  MOST libraries, all `libhmi*.so`, `NBTCarHU`) are byte-identical to the
  dump. `HmiMain3D` and `NavNBTEvoController`
  (`nav/eceusa/bin/NavNBTEvoController`) are in the dump. The MOST catalogue
  and the ABI findings above apply to this car.
- **Symbolic links.** The dump was copied to an exFAT stick, which has no
  symbolic links, so a followed link became a copy. On the car `/opt` is
  `/fs/sda0/opt`, and `/opt/hmi/bin`, `data`, `lib` and `speech` link to the
  same names under `/opt/hmi/ID5`; the dump has only the `ID5` trees. Five
  programs are links that the dump holds as copies:
  `conn/bin/BrowserRenderer-NBTEVOEXT_Main`, `conn/bin/DevCtrlBrowser_Main`,
  `conn/bin/dmsserver_addr`, `conn/bin/dmsserver_pim` and
  `sys/bin/dmsserver-kisu` (the last three are `sys/bin/dmsserver`). None of
  them is used here. `/opt/car/data/htdocs/Apps` and `MainApp` link to
  `/mnt/share/conn/AppFS`, outside the dump. `ls -lR` lists links without
  following them, so the listings give the real layout.
- **Processes.** iDrive runs as `/opt/hmi/bin/zipped/HmiMain3D`, served by
  `ziploader` from `/fs/sda0/opt/hmi/ID5/bin/zipped` (the plain file is
  `/opt/hmi/ID5/bin/HmiMain3D`); `pidin` shows its option names without
  their values. The omap's `most` process runs with `--devmode=client
  --serverRoleName=MOSTSubSystemServer`, and `Connectivity` and `Online` map
  `MOSTSubSystem.DSYSMOSTServer` explicitly: several DSYSMOSTServer clients
  run side by side, as the manager's will. NBT-AAD's own process
  (`/opt/aa/nbt-aad`) was running.
- **An uncoded plugin loads and gets car data.** `Plugins/` holds only
  `NbtAad`: no certificate files, manifest connector `Etch` id 210 with
  `autoStart` and `asyncCallbacks`, the same as `plugin/NbtManager.manifest`.
  It started, logged through `slog` (major 20033), and received an
  `hmi.graphicalContext` value (`visibleContext 3`, widget
  `LT_Table_ID6L_Pad_Title`, after iDrive returned to its main menu). So
  "Plugin is not allowed" does not refuse a plugin of this shape on this car,
  and a car-data subscription from such a plugin is delivered, at least for
  `hmi.graphicalContext`. Its other subscriptions (`driving.gear`,
  `driving.mode`, `driving.DSCActive`, `sensors.PDCStatus`,
  `vehicle.dayNightChangeOver`) log only on a change, and none changed in the
  logged minutes.
- **Screen enforces window ownership.** During the Android Auto start, Screen
  refused ten attempts by the `nbt-aad` process to set `SCREEN_PROPERTY_ZORDER`
  (property 54) on a window of another context ("win_perm: unauthorized window
  request"). A process can change only its own windows: the manager GUI cannot
  push iDrive's windows down, only raise its own. It sets only its own
  window's properties and does not set its z-order.
- **Display.** `carplay.ini` records the coding `1440_540,touch_screen`;
  LayerManagerService reports the HMI's surface 256 at 1440×540.
- **LayerManagerService composes the panel.** GENIVI LayerManagerService
  (`/opt/sys/bin/LayerManagerService`, controlled with
  `/opt/sys/bin/LayerManagerControl`) decides what is drawn, not Screen's
  z-order. It binds a Screen window to the LayerManager surface named by the
  window's id string (`SCREEN_PROPERTY_ID_STRING`, 20), creating the surface on
  the window's first post; a window without a numeric id string is never drawn.
  The iDrive HMI is surface 256, alone in the render order of layer
  0x80000000: a window drawn on a surface outside the render order does not
  appear, whatever Screen's z-order. NBT-AAD shows Android Auto on its own
  surface 1282 by setting that layer's render order to it, the surface's source
  and destination regions to the panel and surface 256 invisible, and hides it by
  putting render order 0x100 and surface 256's visibility back (the slog shows
  these calls). The manager GUI does the same with surface 1290
  (`src/display/layer_manager.cpp`). A companion `RGBA8888` overlay window
  on surface 1292 renders the AudioMaster (`0x20`/`0x0C08`) volume pill above
  both surface 1290 and 1291 while the manager or an application holds the
  panel (`src/gui/volume_osd.cpp`). Like NBT-AAD's windows its window is
  panel-sized (`LayerManagerControl get screen 0`), with equal source and
  destination regions: NBT-AAD never relies on LayerManager scaling (its volume
  pill is panel-sized because a smaller buffer "showed a grey wash"), so the GUI
  draws its 640×240 frame at the largest whole scale (×2 on 1440×540), centred
  on black. It never sets the window's Screen visibility (NBT-AAD's recipe:
  LayerManager alone decides). It takes the panel only from iDrive or from its
  own other surface: unless surface 256, 1290 or 1291 is alone on the layer
  (Android Auto, for example, is up) it stays off. An application the GUI
  starts gets surface 1291 (`NBT_SURFACE`, one application at a time) and the
  GUI puts that surface on the panel the same way (`src/display/placement.cpp`):
  the manager's surface stays until the application's is made, then the two
  swap, and back again when the application ends, so the held (frozen) iDrive
  screen never shows in between. Once on the panel, the GUI checks the
  layer once a second: Android Auto can take it without iDrive's focus moving,
  and a closed window takes its surface out of the render order; either way
  the GUI steps aside. iDrive is put back only when the layer holds nothing
  but 256 and the manager's surfaces, or nothing at all (the order a closed
  window leaves), so a layer Android Auto holds is left alone; the launcher
  does the same after every GUI exit. Every `LayerManagerControl` run prints `unknown symbol: kdMain
  referenced from libKD.so.1` (as for NBT-AAD); it is harmless.
- **Screen property numbers are QNX's standard ones.** The unit's
  `libscreen.so` is newer than the 6.5 SDK (it has `BRUSH`, `SCALE_QUALITY`),
  and NBT-AAD's disassembly of it confirmed the standard event properties 47
  (`TYPE`) and 52 (`WINDOW`). NBT-AAD's `screen_video.h` also lists `VISIBLE`
  43, `STRIDE` 58 and `BUFFER_SIZE` 40; those are guesses its code never relies
  on (43 is `STATIC`, 58 `MIRROR_MODE`). `include/nbt/qnx_screen.h` keeps the
  standard values (`SIZE` 40, `STRIDE` 44, `VISIBLE` 51), with which the window
  is created and reports a valid stride and byte size. The library exports
  `screen_get_buffer_property_iv` (an NBT-AAD comment says another unit's did
  not), and a buffer's `SIZE` is its byte count.
- **PPS** holds no car data here (only iAP2 and multimedia renderer objects).
- **System log.** `slogger` keeps a 1 MB ring; the run held about four
  minutes, mostly video decoder messages.

## The touch screen

Facts from NBT-AAD's touch forwarder (Sq00ky/Supra-iDrive6-Android-Auto,
`milestone-7/src/touch_frame.hpp`, `touch_probe.cpp`; facts only, the code is
PolyForm Noncommercial), captured on a Supra (1280x480) and an F20 (1440x540):

- The touch controller sends UDP datagrams from 160.48.199.115:30501 to port
  30567 (30568 for a second display role); iDrive's `CTouchDataClient` binds
  0.0.0.0:30567. A second socket can bind the port too (`SO_REUSEADDR`,
  `SO_REUSEPORT`); io-pkt hands each unicast datagram to one socket, the one
  bound last. While such a socket exists iDrive gets no touch at all: NBT-AAD
  binds it only while Android Auto is on screen and closes it otherwise.
- Datagram: a 16-byte header (bytes 0-3 the ident `B0 04 80 02`, 4-7 a
  big-endian length = size - 8), then 8-byte records `00 type 40 seq` + 4 data
  bytes. `type` 0xC5 starts a message, 0xC1 continues, 0xC3 ends it (bit 2
  start, bit 1 end); `seq` counts records (mod 256) across datagrams. A message
  is 12 records: one 112-byte datagram on the Supra, 2-4 datagrams of 24-64
  bytes on a 1440x540 unit. Other idents (0xB5098004, 0xB5308005) are the
  gesture camera's.
- The data bytes of records 4.. form the stream: a counter, the contact count
  N (up to 5), then 6 bytes per contact: x (BE16), y (BE16), size,
  `state << 4 | id` (state 1 down, 2 move, 3 up; id a stable finger id).
- x and y are panel pixels (0..1279 x 0..479, or 0..1439 x 0..539). MINI's
  round display needs an x offset (NBT-AAD's `defaultTouchOffsetX`).
- The plugin's focus hold does not cover touch (`docs/app-bridge.md`): iDrive
  reacts to touches while the manager or an application is shown, unless a
  process holds the port.

## Confirmed in the firmware

| Fact | Evidence |
| --- | --- |
| DSYSMOSTServer 4.5, request ids 0xF, 0x1, 0x5, info ids 0x80000000/1 (`docs/controller-input.md`) | Confirmed in the library |
| The `release=1` payload | Same argument list as request 0x1 in the library; the server's behaviour is a bench check |
| `CDSIClient` slots 3, 4, 7 | Confirmed; the table has 8 slots and `src/most/dsi_client.cpp` copies 8 |
| Controller FBlock 0xE5 and functions 0xC10/0xC20/0xC46/0xC47/0xC4F | `CentralControlSystem` and `CentralControlSystem35up` functions of those ids |
| Plugin vtable size and slots, object size, `Runnable`, `pluginAppInfo`, delegate layout (`plugin/plugin.cpp`) | Confirmed against `libAppFramework.A.so` and BMW's plugins |
| NBT-AAD's MOST car-data ids | Named: `GearBox`, `HandBrake`, `CarSpeed2`, `DrivingCondition2`, `StateStandstillFunctions`, `VisibleVolumeIndicator`; `ReverseGear` 0x22C is a further candidate |
| Car-data names | Full catalogue and formats in `libhmiremoting.so` |
| The `unsigned int` in the plugin's car-data subscription | Interval limit (debounce); 0 delivers every change |
| Subscribing to an unknown car-data name | Avoidable: `cdsIsPropertyAvailable` exists |
| An uncoded plugin in `Plugins/` | Loads and receives `hmi.graphicalContext` (NbtAad, with the same manifest shape as the manager's) |
| The car's firmware and the dump | The dump is a car's `/opt` (28735 of 28737 HMI files the same size; copied libraries byte-identical) |
| zlib as `libz.so.2` on the car (the manager's worker unpacks game archives with it) | Needed by `NBTCarHU` and 22 iDrive libraries of NBTevo_Y21432J (`DT_NEEDED`); the same soname as QNX 6.5 SP1's |
| The browser audio source `0xb8` (applications' sound, NBT-AAD's native audio) has a minimum volume of 25 | Seen on the car. `/opt/conn/data/audiosourcemap.csv` names it `HTML_Browser_IR` (an interrupting source) |
| The car's audio connections (`audioconn -l`, NBTevo_Y21432J, 196 of them) | `0xb8` `eSRC_BROWSER` (0x51) to the speakers, DSI `BrowserFront`; `0x1d0` `eSRC_BON_BROWSER_ENT` (0x85), DSI `AuxInOEStream1` (`Streaming_OnlineEntertainment_ENT` in the CSV); `0x100`/`0x110` `eSRC_A4A_ENT1/2` through `AuxInUsbAudio1/2`; CarPlay's `0x314`-`0x320` through `AuxInCarPlay`. The CSV's `0x71 HTML_Browser_ENT` is not built on this car |
| `0x1d0` is the media player's online stream source, not the browser's | Firmware: `AuxInOEStream1` is `NBTMediaMainApp`'s `DMMNBTAuxiliaryInputStreaming` device (radio stations by URL: `requestSetRadioStations`, `requestPlayRadioStation`, HTTP and buffer status). The media player (io-media-cinemo, MME output `output1`) plays into io-amp's `input_stereo_0` through A2I (`A2I_Client_Open`/`Close` on `/dev/io-amp/input_stereo_0` in the car's sloginfo); the browser's sound goes to `input_stereo_3`. Car, dev128 (Android Auto): with `0x1d0` built, the volume goes down to 0 and the browser input is silent. Our own A2I writes to `input_stereo_0` are heard under `0x1d0` (car, the user's report with the mixer; earlier kits were silent); with 0.5.1 the sound is clean and the car's volume takes it down to 0 (user, 27 Nov) |
| A2I (`/opt/mm/lib/liba2i_client.so`, server dev-ioamp-router) | Firmware: `A2I_Client_Open(&handle, &params)` with the media player's params for `input_stereo_0`: magic `0x02000000`, direction 0, 1, 8 buffers, a power-of-two buffer size, 2 channels, 2 bytes, 44100 Hz, 0, the input's name without `/dev/io-amp/` at `+0x24`, stereo 1, 0, format 0 (16-bit), `0x05555555`, a timeout of twice the buffers' duration (ms). Then `GetBuffer(handle, &ptr, &bytes)` (`0x14` = none free), `Out_Write(handle, ptr, bytes, 0)`, `GetNotification(handle)` to wait, `Start`, `Stop`, `Close(&handle)`. `dev-libasound -x/-y/-z <input>` serves `/dev/libasound/dev1-1..3` over the same calls, its device table in fixed shared memory (`/libasound_g_device_table`), so a second instance would clash |
| `dev-libasound` queues a playback twice | Firmware (NBTevo_Y21432J, library code): `snd_pcm_plugin_params` of `/opt/mm/lib/libasound` takes the block mode's `frag_size` and `frags_max` as they are: a ring of `frags_max` slots of `frag_size` bytes in shared memory per client, and the A2I input opened by the first client with `frags_max` buffers of `frag_size` bytes (cookie `0x0ababaBA`). `snd_pcm_write` fills one slot per call (at most `frag_size` bytes) and waits while every slot is full. `dev-libasound`'s writer fills every A2I buffer before `A2I_Client_Start`, then refills one from the clients' rings (up to 4 clients, mixed) each time one comes free. A writer that keeps up therefore has about `2 x frags_max - 1` fragments between its write and the input. A later client keeps the first one's A2I buffers; a client fragment larger than them is refused. `A2I_Client_Open` accepts 1 to 256 buffers of a power-of-two size of 32 bytes or more. Car logs: the device reports fragments of 1024 to 65536 bytes and 22050 to 44100 Hz |

To learn more about a car: the `MOST registry` lines of a debug log list the
FBlocks and instances present without any new MOST request; capturing candidate
functions ([bridge-design.md](bridge-design.md)) gives their value formats; a
`tools/car_inventory.sh` run right after shifting into reverse and back shows
whether `driving.gear` and `sensors.PDCStatus` reach a plugin.
