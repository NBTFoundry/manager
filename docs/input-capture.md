# Input capture: Stage 1 feasibility & ABI investigation

Goal: while the manager or one of its applications is in front, `HmiMain3D`
(iDrive) receives no controller input at all—every knob turn, push, tilt, and
hard key reaches the manager only, without duplicates
([controller-input.md](controller-input.md)).

**Status (2026-11-18):** the shim is in the car, loaded into iDrive and kept
(dev107). The car build is **active** from kit dev111 on, at the user's
request, without the Stage 5 gates below being proven: rotary continuity and
key reconciliation are known effects to watch for (section 6), and controller
health over long captures is not established.

## Ruled-out approaches

- **Registering a second MOST client (`request 0x1`)**: both the manager's client
  and iDrive still receive every MOST input message (`dev28`).
- **Wrapping process startup**: `HmiMain3D` connects directly to
  `DSYSMOSTServer` over DSI rather than inheriting controller input from a parent
  process.
- **Suspending `HmiMain3D` (`SIGSTOP` / stopping main loop)**: peer heartbeat
  timeouts trigger a watchdog reset within ~20 s
  ([car-issues-2026-11-17.md](car-issues-2026-11-17.md)).
- **`CRemoteControlService` (`libhmiremoting.so`)**: `rotary` and `button` inject
  events into iDrive (`hmiplugin::getTCDevCCS35up()`) rather than intercepting
  physical controller input.

---

## 1. Static ARM Evidence (`HmiMain3D` & DSI libraries)

Nothing in this section has been executed on ARM hardware; all findings are
static observations from the supplied firmware dump.

### Artifact & REA evidence identity

| Artifact (private `/opt` dump) | SHA-256 |
| --- | --- |
| `sys/lib/libsys_dsi_base.gpp.so.4` | `6a25bdb99fdfabccf5a8189c1fc21b5f74083229d8db179110357f64300871ed` |
| `sys/lib/libsys_dsi_colibry.gpp.so.8` | `f559c5c1455ed62e376ec72fdb77d5211b748bc9167f9c6866083db1515d19b4` |
| `hmi/ID5/bin/HmiMain3D` | `f397906974addd57c946dfd04243477506560893f3ea405e81b4369ce7a47df5` |

REA evidence IDs (via `inspect-with-objdump` + GNU `arm-linux-gnueabihf-objdump`,
with original hashes preserved):

- `ev_4a9bd6a3f69e4f5355a9e1f31255fd3188b12535e797976e9dfc59a7031d07d5` (`libsys_dsi_base.gpp.so.4` full disassembly)
- `ev_f22ae90c856a2bfce028056afc76f6a918e249d0489d8c84cfeba861bc533d20` (`libsys_dsi_colibry.gpp.so.8` full disassembly)
- `ev_498e4acdcbb194cbebc9757feb0698625a0b8aeb555fa025c97b860140876f49` (`HmiMain3D` dynamic symbols)

Additional `HmiMain3D` inspection used `ntoarmv7-objdump` (binary load base
`0x100000`, non-PIC executable).

### `DSIClientDataResponse` signature, return semantics, and callback sequencing

- **Symbol (`libsys_dsi_base.gpp.so.4`)**: `DSIClientDataResponse` at `0x3d20`
  (size `0xac`):

```c
void DSIClientDataResponse(void *client, void *stream);
```

- **Call site (`DSIClientRun`, `0x47cc`)**: when `cmd == 8`, `DSIClientRun`
  replies to the QNX message **first** (`MsgReply(rcvid, 0, 0, 0)` at
  `0x4ab0`–`0x4ac0`), then calls `DSIClientDataResponse@plt` (`0x4acc`; no
  `-Bsymbolic`), passing `(client, stream)` in `r0`, `r1`.
- **Return semantics**: `DSIClientDataResponse` has no consistent return value
  (`r0` may be `0` on null `client`, `client` on null `stream`/callback, or
  whatever the `client+0xd4` callback leaves in `r0`), and `DSIClientRun`
  ignores `r0` immediately after `0x4acc` (`0x4ad0: ldr r0, [r4, #12]`).
  **Implication:** inventing a nonzero return value when `dlsym` fails is **not**
  fail-open; the shim must resolve the real function cleanly (via `RTLD_NEXT`
  plus explicit `dlopen(..., RTLD_NOLOAD | RTLD_LAZY)` fallback) so every
  forwarded/unhandled call reaches the original `DSIClientDataResponse`.
- **Stream cleanup**: on return from `DSIClientDataResponse`, `DSIClientRun`
  loops to `DSIReceive` -> `DSITimedReceive` (`0x339c`), which calls
  `DSIClose(stream)` (`0x3438`–`0x343c`) before awaiting the next message. Thus
  an early void return from the shim suppresses `client+0xd4` cleanly without
  missing a `MsgReply` or leaking stream buffers (and without calling `DSIClose`
  inside the shim).

### Stream layout and 16-byte DSI header identity

`DSIOpen` (`0x2dd4`) zeroes a 64-byte (`0x40`) stream struct:

| Offset | Field / observed use in `libsys_dsi_base.gpp.so.4` |
| --- | --- |
| `+0x00` | Allocated buffer capacity |
| `+0x04` | Populated byte count (accumulated by `DSITimedReceive`) |
| `+0x08` | Channel identifier set on receive |
| `+0x0c` | Current read position |
| `+0x10` | Payload buffer pointer |
| `+0x14` | Stream error flag |
| `+0x18..+0x34` | QNX message/pulse metadata & chunk reassembly flags (`*(uint32_t*)(stream + 0x34) & 1`) |

- `DSISetCapacity` (`0x2ffc`) may allocate more capacity than `+0x04`, so a
  non-destructive peek must require `error == 0`, non-null `buffer`,
  `0 <= read_pos <= populated <= capacity`, and bound reads strictly to
  `populated - read_pos` without mutating `read_pos` or buffer contents.
- `DSIClientDataResponse` reads 16 bytes via `DSIRead(stream, hdr, 16, 1)`
  (alignment `1` -> exact byte offset `+0x00..+0x0f` from `read_pos`):
  - `hdr[0]` (`+0x00`): unused by `HmiMain3D`'s DSI callback (`0x02a32580` does
    not load `[r2, #0]`).
  - `hdr[1]` (`+0x04`): result/command code (`0`, `0x201`, `0x204`, `0x205`).
  - `hdr[2]` (`+0x08`): information/response ID; `0x80000001` selects
    `InformationProcessMessage` (`0x02a32790` -> `0x02a32cf8`).
  - `hdr[3]` (`+0x0c`): request/sequence token copied to `client+0xc4` during
    the `client+0xd4` callback and cleared to `0` on return (`0x3da8`–`0x3db4`).
- Immediately after the 16-byte header (`read_pos + 16`), `0x02a32cf8` calls
  `0x02a34ba8` (`InformationProcessMessage` unpacker) on the DSI wire format
  corresponding to `_DSYSMOSTTypes_MessageType` (matching `src/most/codec.cpp`
  payload layout: destination, source, FBlock `0xE5`, instance, function `uint16_t`,
  op, and data vector).

### Process identification, client recognition, and reconnect behavior

- **Launch & process name (`opt/sys/etc/starter.cfg`)**: process 17 is
  `<Name>/opt/hmi/bin/zipped/HmiMain3D</Name>` with
  `<Args>-c=DSI,MOSTSubSystem ...</Args>`, using environment 6
  `System_HMI_PRELOAD`
  (`LD_PRELOAD=libEGL_X.gpp.so:libGLESv2_X_FORWARD.gpp.so:libKD_X.gpp.so`).
  No other process in `starter.cfg` uses environment 6. Its `<EnvFile/>` is
  empty; the shim is now loaded through that entry instead
  ([hmi-restart.md](hmi-restart.md)): the process manager reads the named
  file at every start of iDrive and ignores a missing one.
- **Client name (`client+0x20`)**:
  - `DSIClientInit` (`0x4c44`) clears `0xe0` bytes and formats `"%s.%s"` into
    `client+0x20` (`0x4c80`–`0x4c94`, with `inst`, `name`).
  - `HmiMain3D` (`0x239907c` -> `0x2398b48` -> `0x2398e0c` -> `0x2a324c4`)
    passes `"MOSTSubSystem"` (parsed from `-c=DSI,MOSTSubSystem`) as `inst` and
    `"DSYSMOSTServer"` (`0x030a9f30`) as `name` (`major = 4, minor = 5`).
  - Therefore the MOST client's `client+0x20` string is
    `MOSTSubSystem.DSYSMOSTServer`, with handler `client+0xd4 == 0x02a32580`.
- **Reconnect / pointer caching**:
  - `DSIClientHandleServerDisconnect` (`0x3df0`) unregisters, detaches, and
    re-registers the same `client` struct without clearing `client+0x20`.
  - However, `DSIClientFree` (`0x3e5c`) zeroes all `0xe0` bytes and the memory
    may be reused by a subsequent `DSIClientInit` for a different service.
  - Therefore permanent pointer-only caching is unsafe; the shim must re-check
    `client+0x20` boundedly (or invalidate caches when identity changes).
- **GUI's colibry path (`libsys_dsi_colibry.gpp.so.8`)**:
  - `CDSIConnection::handleMessage` (`0x38d4c`) -> `CDSIClient::handleDataResponse`
    (`0x2fdc0`) uses C++ objects and does **not** import or call
    `DSIClientDataResponse`. Still, child-process `LD_PRELOAD` stripping must
    remove the shim entry when spawning manager GUI/apps so unrelated processes
    never load it.

### Segmentation, rotary continuity, key retention, and health checks

- **Segmented controller input**:
  - Transport-layer DSI chunking is reassembled inside `DSITimedReceive` (`0x35fc`–
    `0x36f4`) before `DSIClientDataResponse` is ever invoked.
  - Application-layer MOST unpacking (`0x02a34ba8` -> `0x023985c8`) dispatches
    each `DSYSMOSTTypes_MessageType` directly to `CDevCCS35up` / `CHmiCCSController`.
    No separate multi-message segmentation was observed for controller functions
    `0xC46`, `0xC4F`, `0xC47`, `0xC10`, `0xC20`; any segmented/unexpected frame
    is forwarded unchanged.
- **Rotary counter & reset behavior (`CHmiCCSController::onCCSRotaryEncoder`,
  `0x249ec8`–`0x24b200`)**:
  - `HmiMain3D` persists `prevPos` (`uint16_t` at `[state+20]`, where
    `state = *(r4+148)`) and compares each incoming `curPos` (`uint16_t` in
    `*(r4+152)`) against `prevPos`:
    - `0x24a760`: `detents == 0` resets `curPos`/`prevPos` to invalid sentinel
      `0x7fff` (`mvn r2, #0x8000`) and triggers `sendRotaryEncoderConfig`
      (`0x246360(this, 0)`).
    - `0x24a784`: `curPos == prevPos` is ignored.
    - `0x24ab14`: computes `abs(curPos - prevPos)` and direction (`13` vs `14`),
      allocates rotary `CHmiEvent` (`0x249e64`), and updates `prevPos = curPos`
      (`0x24aed4`).
    - `0x24b0a0`–`0x24b0f0`: when `detents != 1` and
      `(prevPos == 0 || prevPos == detents - 1)`, updates `prevPos` and calls
      `0x246360` to reconfigure the rotary window.
    - `0x248970`: `ReadynachHW_Reset` resets rotary state and re-sends CCS config.
  - **Consequence**: Dropping `0xC46` messages leaves `HmiMain3D`'s `prevPos`
    frozen at the pre-capture count, causing a large `abs(curPos - prevPos)` jump
    when capture ends unless rotary compensation (counter rewriting / re-anchor)
    is implemented and proven on hardware.
- **Retained key state (`clearAllKeyStates` at `0x24bd18` & `0x7267ec`,
  `onCCSPushButton` at `0x24d800`–`0x24df10`)**:
  - `HmiMain3D` maintains a per-key pressed-state map (`1..12`, `17..18`,
    `48..51`, `53`) and logs/drops anomalous transitions
    (`"Unexpected key event: (key, os, ns)=(%d, %d, %d)"` at `0x24df04`).
  - Suppressing a `Release` whose `Press` reached `HmiMain3D` leaves that key
    stuck pressed; forwarding an unpressed `Release` hits unexpected-transition
    handling. Active suppression therefore requires shim-tracked press/release
    reconciliation across capture enter/exit boundaries.
- **Controller health / consistency checks (`checkCcsNotifications` `0x246538`,
  `onConsistencyTimerElapsed` `0x248080`)**:
  - Both inspect MOST notification-registration status (`MNS_SET` / config
    state) and re-issue `sendRotaryEncoderConfig` (`0x246360`) if missing, rather
    than faulting on input-message inactivity. Whether prolonged suppression of
    input functions triggers any other timeout/fault in target firmware remains
    an open bench/car gate.

---

## 2. QNX 6.5 x86 VM Loader Observations

Executed in a private qcow2 overlay (`/tmp/nbt-capture-stage1/vm.qcow2`) backed
by the preserved read-only VMDK (`QNX650NTO-1.vmdk`), running QNX Neutrino
6.5.0 x86 (`ldqnx.so.2`). Test binaries were built with QNX 6.5 `qcc -Vgcc_ntox86`
and run under `/tmp/probes/run.sh`, capturing stdout, stderr, and exit status
separately (`/tmp/nbt-capture-stage1/serial.log`).

| Case | Scenario | Exit | Observed behavior (exact stdout/stderr summary) |
| --- | --- | --- | --- |
| `01` | Baseline (no preload) | `0` | Calls `DSIClientDataResponse` in `libfake_dsi_base.so` directly (`base_id=0x42415345`, `base_calls=1`). |
| `02` | Normal `LD_PRELOAD=libshim_next.so` | `0` | Intercepts PLT call from `libfake_dsi_base.so`; `pthread_once` resolves real fn (`0xb82036e0`) via `dlsym(RTLD_NEXT)` and forwards cleanly (`base_calls=1`). |
| `03`, `04` | Ordering `libaux1.so:libaux2.so` and reversed | `0` | Constructors execute **right-to-left** (`CTOR aux2` then `CTOR aux1` for `aux1:aux2`); `RTLD_NEXT` symbol chain runs **left-to-right** (`AUX1 -> AUX2 -> BASE`), reversed when preload order is swapped. |
| `05` | Empty colon entries `:aux1::next:` | `1` | Fails before `main`: `stderr: ldd:FATAL: Could not preload library `. **Stripping our shim entry from `LD_PRELOAD` must never leave empty `::` or leading/trailing `:` segments.** |
| `06`, `07` | Missing preload library (alone / before valid shim) | `1` | Fails before `main`: `stderr: ldd:FATAL: Could not preload library /tmp/probes/no_such_lib.so`. |
| `08`, `09` | Malformed ELF in `LD_PRELOAD` | `1` | Fails before `main`: `stderr: ldd:FATAL: Could not preload library /tmp/probes/libbad_elf.so`. |
| `10` | Missing `DT_NEEDED` dependency in preloaded lib | `1` | Fails before `main`: `stderr: ldd:FATAL: Could not load library libnonexistent_dep_999.so`. |
| `11` | Unresolved PLT symbol (lazy mode) | `1` | Process starts `main` (`stdout` printed), then aborts at first call: `stderr: ldd:FATAL: Unresolved symbol "missing_external_func_xyz" called from libundef_sym.so`. |
| `12`, `13` | Unresolved PLT (`LD_BIND_NOW=1`) / unresolved data symbol | `1` | Both abort before `main`: `stderr: unknown symbol: ...` followed by `ldd:FATAL: Could not resolve all symbols `. |
| `14` | Concurrent `pthread_once` across 4 threads | `0` | All 4 threads serialize cleanly through `pthread_once` (`init` once, `calls=4`, no missed forwards). |
| `15`, `16`, `24` | `dlopen("libfake_dsi_base.so", RTLD_NOLOAD \| RTLD_LAZY)` fallback | `0` | Returns existing library handle (`0x80562c0`) without duplicate load (`base_id=0x42415345`), resolves real fn (`0xb82046e0 != self`), forwards cleanly even when `dlsym(RTLD_NEXT, missing)` returns `NULL` (`Symbol not found`). |
| `17`, `18` | `dlopen("libfake_dsi_base.so", RTLD_LAZY \| RTLD_LOCAL)` | `0` | Internal PLT call inside `RTLD_LOCAL`-opened library executed `BASE` directly without invoking `LD_PRELOAD` shim (note `HmiMain3D` links `libsys_dsi_base.gpp.so.4` via `DT_NEEDED`). |
| `19`, `20`, `23` | `posix_spawn` child inheritance / unset / selective strip | `0` | Unmodified `environ` inherits `LD_PRELOAD`; stripping only `libshim_next.so` passes `libaux1.so:libaux2.so` cleanly in exact original order (`CHILD calls=1`). |
| `21`, `22` + procfs check | Process identity (`__progname` & `/proc/self`) | `0` | `/proc/self/exefile` and `/proc/<pid>/exefile` **do not exist** on QNX 6.5 (`No such file or directory`; `/proc/<pid>` only contains `as`), and `__progname` tracks `argv[0]` basename (changes to `symlink_app` on symlink execution). |

**Key QNX 6.5 loader conclusions:**

1. A missing, unreadable, malformed, or unsatisfied `LD_PRELOAD` library **aborts
   `HmiMain3D` startup** (`exit=1`). Stage 2 release validation and Stage 4
   storage installation must verify ELF architecture, `NEEDED` dependencies
   (libc only), and zero undefined symbols before updating `starter.cfg`, and
   uninstallation must restore `starter.cfg` **before** deleting the shim.
2. Process verification in Stage 3 must **not** rely on `/proc/self/exefile`
   (absent on QNX 6.5) or `__progname` alone; it must query
   `devctl(DCMD_PROC_MAPDEBUG_BASE, ...)` on `/proc/self/as` (from
   `<sys/procfs.h>`) to verify the actual executable path, matching the
   repo's existing `/proc/<pid>/as` `devctl` usage in `plugin/hmi_threads.cpp`.
3. `dlsym(RTLD_NEXT, "DSIClientDataResponse")` with explicit
   `dlopen("libsys_dsi_base.gpp.so.4", RTLD_NOLOAD | RTLD_LAZY)` fallback (plus
   self-address check `fn != &DSIClientDataResponse` and reentrancy guard) gives
   reliable, single-instance forwarding without recursion.

---

## 3. Unresolved ARM / Target Gates

### Open for Stages 2–4 (observer mode — does NOT block proceeding)

- Target ARM QNX 6.5 `ldqnx.so.2` preload & fallback behavior under the real
  `/opt/sys/etc/starter.cfg` environment (`System_HMI_PRELOAD` ordering alongside
  `libEGL_X.gpp.so:libGLESv2_X_FORWARD.gpp.so:libKD_X.gpp.so`).
- Confirmation on car storage that `/opt/sys/etc/starter.cfg` and
  `/opt/hmi/ID5/data/ro/` mount read-write/read-only cleanly around install and
  survive firmware inspection, plus `DCMD_PROC_MAPDEBUG_BASE` path returned for
  `/opt/hmi/bin/zipped/HmiMain3D` at runtime.

### Blocking before Stage 5 (active suppression)

- Live observer traces confirming expected controller MOST messages (`0xC46`,
  `0xC4F`, `0xC47`, `0xC10`, `0xC20`) traverse `DSIClientDataResponse` under
  `"MOSTSubSystem.DSYSMOSTServer"` with `hdr[2] == 0x80000001`.
- Proven key-state press/release reconciliation across capture enter/exit and
  5-second hard-`BACK` escape (including non-`Release` edge cases).
- Proven rotary delta/continuity compensation (avoiding `abs(curPos - prevPos)`
  jumps and boundary mis-centering when resuming iDrive input).
- Five-minute continuous capture stability check confirming no controller DTC /
  health fault occurs when input functions are suppressed while other `0xE5`
  messages forward.

---

## 4. Implementation & Gate Status (Stages 2–5)

- **Stage 2 (Release compatibility):** Implemented `NBTRLS2` (9 components
  including `capture_lib`) alongside `NBTRLS1`, launcher `--capabilities`
  (`formats=NBTRLS1,NBTRLS2`), desktop/phone compat gating, and Windows kit
  embedding of `NBTRLS1` compat release (`tools/build_windows_kit.py`); since
  kit dev114 the installers stage with the release's own launcher instead and
  kits carry one release.
- **Stage 3 (Observer shim & control protocol):** Implemented C99 libc-only
  `libnbt-capture.so` (`DSIClientDataResponse` only), `DCMD_PROC_MAPDEBUG_BASE`
  process check, `MOSTSubSystem.DSYSMOSTServer` client check, `100 ms` bounded
  control cache with per-message liveness expiry, lockless shared-memory
  diagnostic ring drained by the plugin, child `LD_PRELOAD` stripping, and the
  unified 5-second hard-`BACK` escape. The shim's escape and the 1.5 s
  watchdog apply during installs too (only the GUI ignores its own escape
  then); see "Risk mitigations" below.
- **Stage 4 (Early-boot install & recovery):** Implemented fixed-path storage
  policy & transactional install/remove (`src/storage/{starter_config,capture_io,capture_install,capture_remove}.cpp`),
  integrated into `nbt-manager-refresh` (`SETUP` and `--refresh-setup`),
  `nbt-manager-launcher --remove-capture`, and installer `RemoveAll` (which
  aborts deletion if `--remove-capture` refuses). Tested on host against the
  real vehicle `/opt/sys/etc/starter.cfg` (CRLF & `0664` bit-for-bit
  preservation), fault injection, and QNX ARM build (`qnx-capture` artifact
  and stack checks).
- **Stage 5 (active suppression):** the car build is active since kit dev111
  (`qnx-capture` in the Makefile, `-DNBT_CAPTURE_OBSERVER=0`). Active-mode
  behaviour and the 5-second escape are host-tested
  (`test_capture_shim.py`, the install's live test, which now also holds a
  favourite key's press and release). The gates above were not met first; see
  section 6.

---

## 5. Risk mitigations (review of 2026-11-18)

Fixed in code:

- **iDrive loads the shim only through its environment file, one start at a
  time, kept automatically** (2026-11-18, [hmi-restart.md](hmi-restart.md)).
  The install names `state/manager/hmi.env` as HmiMain3D's `<EnvFile>` in
  `starter.cfg` and arms a new shim by writing that file. The shim deletes
  the file when it loads into iDrive, so a start that fails is followed by a
  stock one. 60 s after iDrive starts, the plugin's `--shim-check` writes it
  again when iDrive has the shim mapped; two starts in a row that ended
  before 60 s turn the shim off until another shim or `--shim on`.
  The live test runs in `/dev/shmem` on the car, as iDrive's own files: in
  the manager's folder on disk the shim's mapped records never reached the
  test's reads (dev105: "FAILED step 2 (on, knob): forwarded, record 0"). A
  shim refused by either test no longer stops the rest of the update: the
  launcher and plugin are still replaced, and the launcher names the reason
  on standard error for the installer (`NBT_REFRESH_CAPTURE_REFUSED`).
- **The shim is test-loaded before `starter.cfg` names it** (2026-11-18,
  `src/storage/capture_probe.cpp`). Every install path runs
  `nbt_capture_refresh_from_release`, which now runs the installed launcher's
  `--capabilities` in HmiMain3D's environment as `starter.cfg` gives it (the
  Environment entries its Process block requires: on the car
  `MALLOC_ARENA_SIZE`, `GRAPHICS_ROOT`, `LD_LIBRARY_PATH`;
  `src/storage/starter_env.cpp`): alone, then with a copy of the shim
  (`state/manager/capture-probe.so`) after iDrive's own preload list, in
  iDrive's order, with `LD_BIND_NOW=1`. When BMW's list cannot bind every
  symbol outside HmiMain3D, the list is loaded with lazy binding and the shim
  alone with every symbol bound; when the list does not load at all, the shim
  alone. Every run must exit 0 within 5 s, or the install stops before anything
  on `/fs/sda0` changes: "capture shim refused, nothing changed: <reason>"
  (before dev107: "capture shim failed its load test, starter.cfg
  unchanged: <reason>"). A pass is logged with the variant used (`capture: load
  test passed: ...`). Checked against the car's own `starter.cfg` copy: the
  parser yields those three entries.
- **The shim's behaviour is tested live during the install** (2026-11-18,
  `src/launcher/capture_selftest.cpp`). After the load test, the install runs
  the installed launcher's `--capture-selftest <shim copy> <state/manager>` in
  the same environment. The launcher loads the shim with `dlopen` and calls
  its `nbt_capture_selftest_begin`, which points the shim's control,
  heartbeat and diagnostics files at `state/manager/capture-selftest.*`, its
  clock at the test's and its forward target at a counter; it is refused when
  the process is HmiMain3D, so iDrive never takes that path, and iDrive's
  `/dev/shmem` files are never touched. The launcher then writes the control
  and heartbeat files with the GUI's own writers and sends knob and BACK
  messages as the MOST client does, checking each time what is forwarded and
  what the shim records: capture off (forwarded); on with a fresh heartbeat
  (knob, BACK press and release held by the active build, recorded by the
  observer build); another client (forwarded, not recorded); heartbeat 1.5 s
  old (forwarded); switched off (forwarded); on again with BACK held 5 s (the
  escape, then forwarded for that generation); a GUI that no longer exists
  (forwarded). Any difference refuses the shim, `starter.cfg` unchanged, with
  the step ("live test: FAILED step N (...)"); a pass is logged as
  "live test OK observer" or "active". `qnx-check` allows exactly the two
  exports `DSIClientDataResponse` and `nbt_capture_selftest_begin`. Not
  covered: the real DSI library and real MOST messages inside HmiMain3D. This catches what stops a program before `main` (VM cases 05 to
  13: malformed file, missing library, unresolved symbol), not whether the shim
  finds `DSIClientDataResponse` inside HmiMain3D (that is logged by the
  plugin). Host-tested with glibc's loader (`test_load_test_uses_the_real_loader`,
  `test_failed_load_test_changes_nothing`); ldqnx's behaviour with
  `LD_BIND_NOW` comes from the VM table above, not from the car.
- **A hung or dead GUI releases capture within 1.5 s, installs included.**
  The GUI no longer writes a 120 s watchdog with the escape off while
  installing; the record always carries 1.5 s and the shim's 5 s BACK escape.
  The shim refuses any record above 1.5 s (`NBT_CAPTURE_MAX_WATCHDOG_MS`), and
  drops authorization when the record's GUI process no longer exists
  (`kill(pid, 0)`), as the plugin's own liveness check does. During an install
  the GUI still ignores its own escape and keeps installing.
- **Remove on a car with an older launcher.** The installer asked every
  launcher for `--remove-capture`; one from before NBTRLS2 rejects it (exit 2)
  and the whole removal stopped. It now asks only when a shim exists
  (`state/manager/libnbt-capture.so`, or the old
  `/opt/hmi/ID5/data/ro/NbtCapture`).
- **Lost `starter.cfg` backup.** An older installer's Remove deletes
  `/var/opt/nbt-manager`, backup included, and leaves the shim and the edited
  `starter.cfg` (iDrive keeps starting). Install and remove then refused. Since
  the editor accepts only the exact stock value or stock plus our one entry,
  the stock file is rebuilt byte for byte from the current one; anything else
  is still refused and the shim kept (`test_lost_backup_is_rebuilt_exactly`).
- **Real function not found.** The shim cannot forward anything then; it now
  records it, and the plugin logs `capture shim: real DSIClientDataResponse not
  found` instead of iDrive losing its DSI data silently.

Not fixable in code, procedures:

- **While the shim is kept on, iDrive's start depends on the shim file**
  (x86 VM: a missing preload entry stops the program before `main`). What
  could remove the shim but keep the environment file is a partial BMW
  firmware update of `/fs/sda0`: **not established**. Without the
  environment file iDrive starts as stock. Before a firmware update, remove NBT Manager with the
  installer (or `nbt-manager-launcher --remove-capture` over SSH); install it
  again afterwards.
- **Recovery when iDrive does not start** needs SSH. The installer enables SSH
  through the gateway's activation relay; whether that works while HmiMain3D
  is down is **not established**. Check it on the bench before relying on it,
  and keep the commands in [install-flow.md](install-flow.md) at hand.
- The process check (`DCMD_PROC_MAPDEBUG_BASE`) and both function lookups are
  verified on the x86 VM only. If the process check fails on the car, the shim
  passes everything through and status stays `pending-restart`.

## 6. Active capture and the favourite keys (2026-11-18, kit dev111)

The user asked for blocking after dev109: in Doom, OPTION still opened
iDrive's options menu. The knob and the keys are blocked; volume is not.

- **What is held** while the GUI's record says on (the manager's or an
  application's surface on the panel, fresh heartbeat, live GUI): FBlock 0xE5
  status messages 0xC46/0xC47/0xC4F on the knob instance and 0xC10/0xC20 on
  the older controller's, as before, and now FBlock 0x15 function 0xC02 on any
  instance (`nbt_capture_classify_bytes`, `is_input_message` in the shim).
  Presses and releases alike: the user pointed out that a favourite key's
  release reaching iDrive would run the stored favourite.
- **The favourite keys, from HmiMain3D** (`~/src/reverse/opt` dump, the
  binary of section 1). `CPxyCECenterStack` is a MOST proxy whose base
  constructor gets FBlock 0x15 (`mov r1, #0x15` at `0x21317d0`;
  `CPxyCentralControlSystem35up` passes 0xE5 at `0x2137e54`, which checks the
  pattern). It registers seven functions with `0x204d458(this, member, fkt)`
  (`0x213173c`-`0x21317b0`): 0xC01 `CAudioVolumeButtonCS`, 0xC02
  `CFunctionalBookmarkButtons`, 0xC03 `CPushButtonAmFmTP`, 0xC04
  `CPushButtonAudio`, 0xC05 `CPushButtonEject`, 0xC06 `CPushButtonMode`,
  0xC07 `CPushButtonSkip` (members matched to classes through the vtables
  their destructors load, resolved by the GOT relocations). The 0xC02 status
  decoder (`0x2131ee0`, op 0x0C) reads eight one-byte states, one per key.
  The factory instance is 0x0C (`m_FactoryInstID_NBT`, built at `0x2131e44`
  with FBlock 0x15 and instance 0x0C, the way the 35up IDs are built with 0xE5
  and 0x03/0x05/0x10/0x12/0x14).
- **Volume is untouched:** AudioMaster 0x20 (fkt 0xC08, NBT-AAD's
  `VolumeInput`), the centre stack's 0xC01 and the 35up's rear volume
  functions are never candidates.
- **Not established, to watch in the car:**
  - iDrive's rotary counter misses the detents turned while captured
    (section 1, `0x24ab14`): its first turn after the manager closes moves by
    that many;
  - a key pressed before capture and released during it stays pressed in
    iDrive until pressed again;
  - whether long captures raise any controller fault.

**Car run of dev111 (2026-11-18, `debug-1795029969.log`):** the shim held the
knob, BACK, MENU and OPTION in the manager, in Doom and in the test
application (diag `kind=3`, the user: "following presses were captured
fine"). The first OPTION of the first session reached iDrive: that session's
first message, a knob push, was already an escape (`kind=4`, generation 73751),
and an escaped generation forwards everything (`kind=2` after it). By the code
the only way is a BACK hold left counting: the shim updated its hold only while
capture was on, so a BACK pressed captured and released while capture was off
(as BACK closing a screen does) kept running, and the next capture's first
message found it older than 5 s. Which press left it is not established (the
log's start is rotated out). Fixed: the hold is dropped whenever a message
arrives with capture off (`evaluate_call` in the shim) and when the capture
generation changes (`back_generation`, `nbt_capture_evaluate`); the install's
live test now presses BACK captured, lets it go by uncaptured and checks that
the next capture, 5 s later, still holds the knob
(`tests/test_capture.cpp` covers the generation case).

**Diagnostics since dev115.** Every ring record carries the FBlock
(`fblock=0x15` for a favourite key; shims before dev115 wrote 0 there) and a
detail byte. An escape (`kind=4`) records how long BACK was held, and a BACK
hold that stops counting (capture off, or a new generation, before its
release was seen) gets its own record, `kind=7 (BACK hold dropped)`, with its
age and generation. The plugin names each kind in the debug log. Whenever the
debug log moves to a new file (switched on, switched off and on, rotated:
`nbt_log_file`), the plugin logs `capture shim active` again and replays what
the ring still holds as `capture diag (earlier)`, so every fetched log starts
with the shim's state and its last 32 decisions. The car log of dev111 lacked
both: its start was gone (not rotated away: the file was 58 KB, under the 128
KB limit; it was switched off and on, or removed, beforehand). The GUI reads
the same ring once a second while capture is wanted (`CaptureWatch`) and shows
a line on the manager's list when the keys reach iDrive too: `iDrive gets the
keys too (BACK held 5 s)`, `(shim not blocking)` or `(shim not loaded)`,
logged as `capture: controller keys ...` when it changes. Settings > Status
(after dev116) shows the same state at any time, `Keys: kept from iDrive`
included, beside the MOST controller input, the gamepad and the last error
(`src/gui/status_page.cpp`; docs/gamepad-bridge.md).

