# Reviewed parameter layouts

[most-params.tsv](most-params.tsv) records the manually traced paths in
NBTevo_Y21432J. Source B2 means static generated-code analysis; `car_confirmed`
is `no` until a reviewed B4 capture supports that exact row. Firmware hashes
and the stream helpers are documented in
[head-unit-interfaces.md](head-unit-interfaces.md#notification-static-findings-nbtevo_y21432j).

The TSV uses a header and UTF-8 tab-separated columns. `op` names the MOST
operation, `instance` is a concrete two-digit hex instance or `*`, and `fields`
is a semicolon-separated sequence of `type name`. Integers with `be` use
network byte order; u8 occupies a whole byte even for a narrow enum. Arrays
consume the remaining payload unless a row states otherwise. `unknown` in
future rows means no decoder can be generated. Omitted functions/operations
are unknown, not empty payloads or unsupported operations. An instance-specific
row must never be applied to a different instance.

The generated classes are not uniform enough for a straight-line list of
typed calls to establish a schema. For example E5/C10 checks the instance at
`0x02135ac4`: instance 01 omits a byte that other instances read. Other
classes have branches, loops and nested structures. The virtual dispatcher is
slot 6 after the two vtable header words (CarSpeed2 at `0x02320e8c`, rotary
encoder at `0x02135a40`); Set writers are separate nonvirtual methods, not a
uniform second vtable slot. The plan's manual-analysis fallback is used.

Reviewed scalar helpers in HmiMain3D: `0x02051888` reads a byte and validates
a range; `0x02051dd0` reads a u16 using `readUInt16`; `0x02051e30` is its
unrestricted wrapper. `0x02135a08` wraps the byte helper with range 0..31.
Notification's writer uses `writeUInt16` via `0x02052004`/`0x02052060`.
There is no struct padding between wire fields.

The `*` FBlock rows describe the generated CPxyBase interface, not NetBlock
(01), whose low function IDs have different meanings. NetBlock subscriptions
are rejected until its firmware-specific interface is traced.
Byte order is independently established in `libhmibase.so`:
`readUInt16` at `0x0003a7b4` combines the first byte shifted by eight with the
second; `writeUInt16` at `0x0003a8e4` emits high byte then low byte.
Library SHA-256:
`db4dc3d95ccd8c936b161de5203ed86bc329f536e7a6f50285f00805a5a1fc39`.

CDS JSON is a different layer. [car-data-properties.tsv](car-data-properties.tsv)
is reproducible with:

```sh
python3 -I tools/cds_catalogue.py /path/to/libhmiremoting.so > /private/car-data-properties.tsv
```

Its 258 rows describe factory registrations and JSON format strings, with
unknown formats retained. Python's `csv` reader with `delimiter='\t'` handles
quoted JSON cells. The library SHA-256 is
`a0e125f90eb45a7942ccbbe4a09470a5d29e9d34367ebb263cc1fdd7f778584d`.
`Gear::onStatus` (`0x00208674`) calls an internal getter (`0x004d4618`),
`CarSpeed::onStatus` (`0x0020965c`) scales its internal value, and
`HmiGraphicalContext::getFormatedValue` (`0x00211640`) uses HMI state.
Those JSON names cannot establish MOST offsets, units or enum mappings.
Consequently the extractor emits `unknown` for FBlock, instance and function.

The existing function catalogue remains byte-for-byte reproducible. Its 13
unreached base classes remain unknown: PxyAudioDiskChangerBase,
PxyAudioDiskPlayerBase, PxyAudioDiskPlayerChanger, PxyDebugApplication,
PxyDiagnosis, PxyDiagnosisBase, PxyMapControl, PxyNetBlock, PxyNetwork,
PxyNetworkMaster, PxySyncSource, PxyTiTelematics and StbNetBlock.
Registry instances (B3), capture-derived meanings (B4), public standard
catalogue cross-checks (B5), and additional Set/Get/StartResultAck layouts
remain follow-up work. Keep vendor binaries and raw captures outside the repo.

Public cross-check (B5, partial): OASIS's
[DVDPlayer 4 MOST manual, sections 4.2.2-4.2.3, pages 38-40](https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/UserGuides/UM_DVDPlayer_4_MOST_V01_02_00-0.pdf#page=38)
agrees on Notification's control values, word-sized addresses/IDs and four-ID
limit. Its NotificationCheck takes a device address and returns that address
plus function IDs. That suggests the meaning of the HMI Check parser's leading
word, but its request builder and firmware revision still need tracing.
The same manual assigns NetBlock Notification/Check to 401/402, while target
HmiMain3D exports `CPxyNetBlock::CDeviceInfo` (vtable `0x03823bc8`), reinforcing
that CPxyBase's low IDs cannot be applied indiscriminately.
[Microchip NetServices Layer II V2.2.X, page 137](https://ww1.microchip.com/downloads/aemDocuments/documents/AIS/ProductDocuments/UserGuides/UG-MNS-Layer2-V2.2.X-60001775.pdf#page=137)
also describes the historical four-ID NotificationCheck reporting limit. This
does not establish BMW-specific parameter meanings or full catalogue parity.
