# Plan: MOST subscriptions and parameter formats

## Implementation status (2026-11-12)

- Static findings and address evidence: [Notification](head-unit-interfaces.md#notification-static-findings-nbtevo_y21432j).
  The server gives clients distinct addresses. Clean exit uses per-client
  ClearFunction; crash cleanup still needs hardware evidence. NotificationCheck
  request construction remains unknown.
- A2 is implemented for bridge 1.2, with four-function batches to match the
  observed writer limit, shared rate limiting and bounded exponential retries.
  The initial-value Get fallback awaits A1 evidence.
- A1/B4 tooling and procedure: `apps/most-monitor` and
  [bench-checklist.md](bench-checklist.md#9-most-subscriptions-and-parameter-capture-a1a3b3b4).
  The user will run the car session later. A1/A3/B3/B4 are **pending**.
- B1's direct MOST-mapping premise was incorrect: CDS reads ProcUnits and HMI
  state. `tools/cds_catalogue.py` extracts 258 registrations and 142 JSON
  formats; unknowns stay explicit in [car-data-properties.tsv](car-data-properties.tsv).
- B2 uses the manual fallback because fields vary by branch/instance. Reviewed
  sample layouts and remaining gaps are in [most-params.md](most-params.md)
  and [most-params.tsv](most-params.tsv). Additional operation layouts and B5
  standard-catalogue cross-checks remain pending.

The plan is **not complete** until A3 and the outstanding evidence checks pass.

Two gaps in the MOST bridge ([most-bridge.md](most-bridge.md)):

- **Subscriptions.** Applications get only the status messages that someone
  else (iDrive) has already subscribed to. Nothing subscribes on their behalf,
  and applications can send raw `Notification` (function 0x001) commands with
  no safeguard.
- **Parameters.** [most-functions.tsv](most-functions.tsv) has names and ids,
  not payload layouts. We know the formats of the controller functions (0xE5)
  and of NBT-AAD's handful only.

Part A fixes subscriptions, Part B recovers formats. The static steps (A0, B1,
B2) need no car and can run in parallel. One car session then serves A1 and
B4 together.

## Where things stand

- The host registers a DSYSMOSTServer client (request 0xF) and subscribes to
  the same server attributes and informations as iDrive's client. It registers
  the `ctl` keys as device controls (request 0x1). `rx` rules only filter what
  the server delivers ([src/most/service.cpp](../src/most/service.cpp)).
- MOST-level notification is a function of each FBlock: a controller sends
  `Notification.Set` with its device address and function ids, and the FBlock
  then sends `Status` on every change. The bridge never does this.
- In iDrive this is done by its framework: `CPxyBase::CNotification` and
  `CPxyBase::CNotificationCheck` (vtables in HmiMain3D),
  `CSvcFunctionProxy::onNotificationTimeout` and
  `CSvcArrayWindowProxy::sendSetNotification`/`sendClrNotification`. These are
  the reference for the message format.
- The policy accepts `tx` rules covering 0x000-0x002, including wildcards such
  as `tx:82.00.*` ([src/model/most_policy.cpp](../src/model/most_policy.cpp)).
- Nothing in the MOST bridge has run in a car.

## Part A: subscriptions

### A0. Static research (no car)

Answer from the binaries before designing anything:

1. **Notification.Set format.** Decompile `CPxyBase::CNotification`'s set path
   in HmiMain3D: control byte (SetAll/SetFunction/ClearAll/ClearFunction
   values), the device address it puts in, and how the function list is
   encoded (12-bit packing or u16 list). Same for `NotificationCheck`.
2. **Whose address.** What device id the server gives a client in
   `responseMOSTClient` (0x80000000), and what iDrive puts in its own
   `Notification.Set`. Is it one node address for every client on the head
   unit, or one per client? This decides whether we can ever `Clear` safely.
3. **What the server does.** In `libimp_sys_most_base_nbt.cpp-ne.so.17`:
   - `requestEnableNotificationProxy` (0x11): does the server keep a
     notification list for its clients?
   - `informationCleanUp` (0x80000007: `CleanUpRequest`, DevCtrl, device id):
     does the server clean up a client's state when it disconnects?
   - How a `Status` that arrives for the node address is routed: to every
     client, to the clients registered as device controls (0x1) for that
     FBlock, or to the one that subscribed.
4. **Retry behaviour.** What iDrive does on `onNotificationTimeout`, so ours
   behaves no worse.

Output: a "Notification" section in
[head-unit-interfaces.md](head-unit-interfaces.md) with addresses and
evidence, and the choice between "per-client address: subscribe and clear" and
"shared address: subscribe, never clear".

### A1. Car check with the current bridge

Uses the bridge as it is, with a monitor application (`rx:*.*.*`, no `tx`).
Build one if `apps/test` cannot do it. Add the steps to
[bench-checklist.md](bench-checklist.md):

- log STATE (own address) and REGISTRY (FBlocks and instances present);
- run it beside the manager GUI's own controller client: do both receive
  0xE5 messages? Does the monitor receive Vehicle 0x05 statuses (0x621, 0x228)
  without any `ctl`?
- one harmless `Get` (with `tx` for that function only) on a Vehicle property,
  to confirm commands are answered and how the answer is addressed;
- record whether anything changes after the application exits (were the
  device controls dropped?).

### A2. Host-managed subscriptions (bridge 1.2)

Design, to be adjusted by A0/A1:

- **New rule** `sub:<FB>.<II>.<FFF>`: concrete values only, at most 32 rules.
  It must be covered by an `rx` rule (the validator refuses it otherwise),
  in keeping with the existing rule that `ctl` and `tx` do not imply `rx`.
- **Host behaviour.** When the link goes up, send one `Notification.Set`
  (SetFunction) per FBlock instance, listing that instance's subscribed
  functions. Send it to the node that the registry lists for the instance,
  using the address A0 identifies. Send it again after comm-lost to up, after
  a detach, and when the FBlock reappears in the registry. If the FBlock is
  not in the registry, wait for it. These sends count against the existing
  rate limit.
- **Guard.** No application may send functions 0x000-0x002 (`FktIDs`,
  `Notification`, `NotificationCheck`): `tx` sends to them get NOT_ALLOWED
  whatever the rules say, and the parser refuses concrete `tx` rules naming
  them. Only the host talks Notification.
- **Clear.** Only if A0 shows a per-client address: `ClearFunction` on a
  clean exit, and rely on the server's cleanup otherwise. With a shared
  address the host never clears, because that would cancel iDrive's own
  subscriptions.
- **Initial value.** MOST FBlocks normally answer a `Set` with the current
  status. If A1 shows they do not, the host sends one `Get` per subscribed
  function after subscribing.
- **Compatibility.** Bridge 1.2 (minor: a new rule kind). A manager that
  does not serve 1.2 already refuses an unknown rule at verification. The
  stored policy remains one string, so the record format does not change.
- **Tests:**
  - policy parsing: valid rules, malformed rules, limits, `sub` without `rx`,
    `tx` naming 0x000-0x002;
  - `tests/fake_dsi.c` records the Notification requests: check their
    content, re-subscription after comm-lost and after a detach, and an
    FBlock that is missing then appears;
  - the `tx` guard with wildcard rules.
- **Docs:** [most-bridge.md](most-bridge.md) (rule, behaviour, results),
  [app-bridge.md](app-bridge.md) (version 1.2).

### A3. Car validation

Subscribe to one property that iDrive does not show (chosen from the A1
registry), check that statuses arrive and stop, and check that iDrive's own
displays (speed, gear) keep updating while and after the application runs.

### Later

One shared MOST connection for every application. The host merges
subscriptions with reference counts, so N applications cost one `Set`. This
was planned as the "shared connections" step.

## Part B: parameter formats

Goal: for each function an application is likely to use, the layout of its
`Status`, plus the arguments of `Set`/`Get`/`StartResultAck`. The layouts go
in a machine-readable file, so applications stop guessing. The bridge stays
raw; decoding belongs to applications, and later to documented `car-data`
fields ([app-bridge.md](app-bridge.md) section 4).

### B1. iDrive car data (`libhmiremoting.so`), first

`/opt/hmi/ID5/data/ro/hmiplugins/libhmiremoting.so` has 288 `CCdsProperty*`
classes. Each `onStatus` reads a MOST status (or a CAN signal) and publishes
JSON under a dotted name. NBT-AAD found its formats here.

1. Decompile a few by hand with REA (`CCdsPropertyGear::onStatus`, the speed
   one, `CCdsPropertyHmiGraphicalContext`) to learn the pattern: where the
   FBlock/function is named, how fields are read (offset, width, sign,
   scaling) and which JSON keys they become.
2. If the pattern is regular, write `tools/cds_catalogue.py`, which reads the
   ELF only, like `most_catalogue.py`. Output: property name, FBlock,
   instance, function, fields (offset, type, JSON key).
3. Check it against what is known: gear 0x05/0x228, speed 0x621, GNSS
   0xCD/0x203, `hmi.graphicalContext`.
4. Save the reviewed result as `docs/car-data-properties.tsv`. It holds
   interoperability facts only, like `most-functions.tsv`; the binary and raw
   dumps stay out of the repository.

### B2. Generated function classes in HmiMain3D

Every `CPxy<FBlock>::C<Function>` class (2407 proxies and 108 stubs) is
generated code. Only the vtables are exported, with no typed method names.

1. Using `CPxyVehicle::CCarSpeed2` and `CPxyCentralControlSystem::
   CccsRotaryEncoder` (both formats known), find the vtable slot that parses
   a `Status` and the one that builds a `Set`. Check that the reads and
   writes go through typed stream helpers.
2. If the generated code is uniform, extend `tools/most_catalogue.py` with
   `status` and `set` columns listing the field types in order (u8, u16,
   s16, u32, string, stream, enum widths). If it is not, decode by hand only
   the functions an application asks for.
3. The 13 classes that initialise their members through a generic base
   (Diagnosis, Network, AudioDisk and others) are missing from the catalogue.
   Add them if B2's analysis reaches them; otherwise list them as unknown.

Types are not meaning: units, scaling and enum values come from B1 (JSON
keys) and B4.

### B3. Instances

The catalogue has no instances. Take them from the car's REGISTRY (A1) and
add them as a column, or as `docs/car-registry.md` for NBTevo_Y21432J.

### B4. Car captures (same session as A1)

Have the monitor application log every message while a fixed script runs:
ignition states, park and reverse, speed, volume, the controller, lights.
Write the script down so captures can be compared. Match the bytes to the
actions to confirm B1/B2 layouts and fill in meaning. The raw captures stay
outside the repository (they can carry the VIN and positions); only reviewed
facts go in.

### B5. Standard FBlocks

AudioMaster, the tuners, Telephone and the other standard blocks follow the
public MOST function catalogues. Use them as facts to cross-check B2; BMW's
own blocks (0xC0-0xEF) have no public description.

### Output format

`docs/most-params.tsv`, one row per function and op: FBlock, function, op,
fields as `type name` in order, source (B1/B2/B4/B5) and whether it was
confirmed in the car. `most-bridge.md` links to it, and the SDK can generate
decoders from it later.

## Order

1. A0 and B1 (static, in parallel), then B2.
2. One car session: A1 and B4, with the monitor application.
3. A2, the implementation, adjusted to A0/A1.
4. A3, car validation, and B4 again for anything A2 newly subscribes to.

## Rules for this work

- Facts only from NBT-AAD; never its code. No BMW binaries, dumps or raw
  captures in the repository; reviewed interoperability tables only.
- Every protocol claim cites its evidence (symbol, address, capture). Mark
  anything unconfirmed as such.
- Host tests and cross-compiling do not show car behaviour. The plan is done
  only after A3.
