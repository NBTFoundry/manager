---
name: bridge-change
description: Add or change what applications or the phone see of NBT Manager - a bridge capability, a phone route (/v1/...) or a GUI-worker request - with every side that must follow in the manager, the SDK and the phone app, and their tests. Use for any interface change across those repositories.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Changing the manager's interfaces

Each interface has sides in several repositories. Find the closest earlier
change and follow it file by file with `git show --stat`:

- capability `files` (bridge 1.5): manager `98844ee`, mobile-app `2848597`, sdk `1b76afc`;
- route `/v1/autostart` and GUI request `AUTOSTART`: manager `65ebc53`, `9a60a29`, mobile-app `4be6071`.

Rules: minors only add, so existing applications and older phone apps keep
working. Ask the user before a new capability, a changed format or raising
`NBT_BRIDGE_OLDEST_MINOR` (that disables older applications). One branch
per repository; commit each step. Nothing names one application.

## A bridge capability (applications see it)

1. Contract first: `docs/app-bridge.md` (the table and a section), a
   `docs/<name>-bridge.md` when it is large, `PROTOCOL.md` where the worker
   takes part.
2. Version: `nbt_bridge_current()` in `src/model/bridge_api.cpp` (minor and
   capability list) and `CURRENT` in `tools/bridge.py` hold the same values.
   A new package field: `tools/bridge.py` checks it, `src/model/bridge*.cpp`
   parses it, the installed record keeps it.
3. Host: environment and descriptors in `src/process/bridge_environment.cpp`,
   `host_channels.cpp`, `host_start.cpp`.
4. SDK: `Lifecycle` reads it (`has_cap`, the descriptor), `docs/api.md`. A wire
   format the manager defines is a reader copied verbatim into the SDK
   (`READERS`; `make check-readers NBT_MGR_DIR=../manager` there).
5. `apps/test` and `tools/build_windows_kit.py` declare it when the kit should exercise it.
   The self test should check it too: a probe in `apps/selftest` (self-test
   skill), so every car can see it work without a kit.
6. Tests on each side; a `docs/bench-checklist.md` row for what only the car shows.

## A phone route

- Manager: the route in `include/nbt/worker/http.hpp`, its signed path in
  `src/worker/http_auth.cpp`, dispatch in `src/worker/phone_api.cpp`, the
  handler in `src/worker/phone_routes.cpp` (bounded body, exact status codes).
  Decide whether the door answers it before the manager is open. A row and a
  section in `PROTOCOL.md`; tests in `tests/test_phone.cpp` and
  `tests/test_manager_ui.py`.
- Phone app: `ROUTES` in `src/protocol/signing.ts`; `RouteGuard.kt` (worker
  and door) with `RouteGuardTest.kt`; the reply parser in
  `src/protocol/replies.ts` with `tests/parsers.test.ts`; the action in
  `src/protocol/actions.ts`; `scripts/contract.ts`. An older manager answers
  404 or `400 route`: hide the feature, never fail.

## A GUI-worker request

The worker's `src/worker/ipc_*.cpp`; the GUI's request and
`src/gui/reply_parser.cpp` (malformed replies rejected); `tools/simulator.cpp`
answers it; `PROTOCOL.md`; tests in `tests/test_ui.cpp` and the worker's. Logic
the GUI and the phone share lives in one worker function
(`src/worker/autostart_request.cpp`).

## Checks

- Manager: `nice make -j"$(nproc)" pre-commit test`; the ASan build and
  `make fuzz` for new parsers (README); `make qnx-check stack-report` with the
  QNX environment sourced.
- SDK: `make test test-asan check-static-init check-generic check-readers`.
- Phone app: the checks in its `AGENTS.md` (`yarn typecheck`, `lint`, `test`,
  `test:contract`, the Gradle unit tests).
- New texts: gui-text.
