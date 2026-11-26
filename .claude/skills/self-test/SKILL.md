---
name: self-test
description: NBT Manager's self test (Settings › Self test) - add or change a check, a bundled test application or its report, run it on the desktop and in the car, and read its results. Use when a bridge capability or manager feature should be covered by the self test, when its results come back from the car, or when it misbehaves.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# The self test

Contract: `docs/self-test.md` (read it first). In short: the release carries
three packages signed with the manager key (`selftest_native`,
`selftest_guest`, `selftest`: one app in three forms); `SELFTEST PREPARE`
installs them from the running slot's `release.nbtr` under hidden
`manager-...` ids; the GUI starts each as `START` does, asks two questions,
then `SELFTEST REPORT` gathers the worker's checks and each app's
`data/<app>/selftest.txt`.

| Where | What |
| --- | --- |
| `apps/selftest/` | The app: `main.cpp` (phases, drawing, beeps, controller), `probes.cpp` (bridge, files, car data, diag), `report.cpp`; `common.args` + `<form>.args`; `selftest.mk` |
| `include/nbt/bundled.h`, `src/model/bundled.cpp` | Components, ids and forms, in run order |
| `src/worker/self_test.cpp` | Prepare: release verified, packages installed or kept (signer `manager`) |
| `src/worker/self_test_report.cpp` | The worker's checks (`manager.*`), the apps' reports read with bounds, the kept report, the reply |
| `src/worker/ipc_selftest.cpp` | `SELFTEST PREPARE`/`REPORT`/`LAST` |
| `src/gui/selftest_*.cpp` | The flow (`selftest_ui`), reply parsers (`selftest_reply`), screens and labels (`selftest_view`) |
| `tools/bundled_apps.py` | Signs the three for `build_release.py` and the release workflow |
| `GET /v1/selftest` (`selfTestPhone`) | The kept report for the phone, worker and door; the phone app's `src/protocol/selfTest.ts`, `src/car/selfTest.ts`, `src/screens/SelfTestPanel.tsx` (labels `selfTest.*` in its seven catalogs) |

## Adding a check

1. **In an app** (most checks: what an application gets): a probe in
   `probes.cpp` that ends in `report.set("<check>", kPass|kFail|kSkip,
   "<detail>")`. Names `[a-z]{1,12}`, details 40 printable characters.
   **Fail** = the manager is at fault; **skip** = the car or the driver did
   not allow it (ignition off, no gateway, BACK). Never block the main loop
   past the 3 s heartbeat (diag runs on a thread). Never put a VIN, position
   or anything identifying in a detail or a log line.
2. A capability it needs: declared in `common.args` (all forms) or
   `<form>.args`; `selftest.mk` passes the declared list to the bridge check.
3. **In the worker** (the manager's own state): `managerChecks()` in
   `self_test_report.cpp`.
4. **Label**: `stc_<check>` (or `stg_<group>`) in all seven catalogs
   (gui-text skill) and in `checkLabel()` (`selftest_view.cpp`), and
   `selfTest.check.<check>` in the phone app's catalogs and `CHECKS`
   (`src/car/selfTest.ts`); an unknown one shows as its raw name.
5. A new interactive step: the instruction (`st_next`/`st_turn`) and the
   in-car procedure must say what the driver will see and do.
6. Docs: the table in `docs/self-test.md`, `docs/in-car-procedure.md` 7f,
   `docs/bench-checklist.md` 11b.

## Checks

- `make selftest-apps` (the three forms, module and guest checks, QNX
  imports) and `make selftest-host` (desktop build of the module).
- Manager: `nice make -j"$(nproc)" pre-commit test` covers
  `tests/test_self_test.py` (worker) and `test_self_test` in
  `tests/test_ui.cpp` (GUI). `make run-simulator`: Settings › Self test shows
  the screens with a canned report.
- Desktop run of the app alone: `build/selftest/selftest-host` with
  `NBT_DATA_DIR=<scratch dir>` (X11; no bridge descriptors = user mode),
  then read `<scratch dir>/selftest.txt`.

## Results from the car

- The phone app: car page › Self test › Share (every check, in the phone's
  language). The car's screen, or the debug log: one `self test: <group>.<check>
  pass|FAIL|skip <detail>` line per check, after `self test: ... installed`
  lines on the first run of a release.
- Each app's output: `state/manager-selftest*/output.log` (Collect
  everything over the ENET adapter has them; the phone lists no hidden app).
- `start fail no report`: the app never wrote its report: its log says how it
  ended (car-logs skill). `install failed`: the debug log's `self test:
  <app>: ...` line. `release not verified`: the slot's release does not
  match the manager key or the active state.
