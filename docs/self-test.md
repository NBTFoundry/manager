<!-- SPDX-License-Identifier: MPL-2.0 -->
# Self test

Settings › **Self test** checks, in about a minute, that NBT Manager and
what it gives applications work on this car: the manager's files, then a
small test application in each form (native program, module and guest, both
run by nbt-runner), started as any application is. Nearly everything is
automatic; the driver turns and pushes the controller once and answers two
questions (the test screen, the beeps).

## The bundled applications

The release carries them as named components (`docs/manager-release.md`),
each an ordinary signed package (`apps/selftest/`), signed with the
**manager key**, not the apps key:

| Component | Application | Form |
| --- | --- | --- |
| `selftest_native` | `manager-selftest-native` | native program |
| `selftest_guest` | `manager-selftest-guest` | guest |
| `selftest` | `manager-selftest` | module (the one the driver interacts with) |

- Application ids starting with `manager-` are the manager's: never from a
  phone, a session or provisioning; left out of `LIST`, the phone's lists,
  autostart and the slot check (`nbt_app_bundled`, `nbt/bundled.h`).
- Installed when a self test starts, from the running slot's
  `release.nbtr`, verified again against the manager key; signer `manager`
  in their profiles (`nbt/signers.h`). A newer release's components replace
  them at the next self test; an unchanged one is kept.
- A release without them (a step release, an older manager) reports
  "not in this release".

## The run (GUI, `src/gui/selftest_ui.cpp`)

1. Each application that runs is stopped first, as holding its row would
   (`STOP`, no fault counted): it would hold the screen. One that does not
   stop within 20 s is left, and its screen may fail the test.
2. `SELFTEST PREPARE` (worker's job): verifies the release, installs or
   keeps each bundled application, clears their fault guards and last
   reports. `SELFTEST\tready\t<app>,<app>,...` or `ERR selftest <reason>`.
3. Each application in turn, as `START` does (`OK screen`, the panel handed
   over, back when it ends or after 10 s without a frame). Before the
   module: "Watch and listen: colour bars, three beeps; then turn the
   controller and push it."
4. Two questions: the colour bars, the beeps (Yes / No).
5. `SELFTEST REPORT <saw> <heard>` (`y`/`n`): the worker gathers its own
   checks, each application's report and the answers, keeps them in
   `state/manager/selftest.txt` and answers with the counts and what did
   not pass. `SELFTEST LAST`: the same for the kept report. A paired phone reads the
   kept report itself (`GET /v1/selftest`, worker and door), every check
   with its result and detail, and shares it.

## The application's report

`$NBT_DATA_DIR/selftest.txt`, written once (temporary file and rename) when
it ends; the worker reads at most 4 KiB, 24 lines:

```
SELFTEST1	<form>
<check>	<pass|fail|skip>	<detail>
```

`<check>` is `[a-z]{1,12}`, `<detail>` 0-40 printable ASCII. Checks:

| Check | Pass | Skip |
| --- | --- | --- |
| `bridge` | every declared capability granted | |
| `screen` | frames drawn on the manager's surface (detail: fps) | |
| `sound` | the audio output (mixer or device) opened | |
| `status` | the status line written | |
| `files` | its files folder listed | no files folder (hard disk) |
| `cardata` | a value of the speed or the outside temperature within 5 s | none (ignition off) |
| `diag` | the engine computer answered a read | no gateway, no answer, busy |
| `phone` | its phone page published (module) | |
| `controller` | a turn and a push within 15 s each (module) | BACK pushed |

A missing report is `start fail` ("no report"). The worker adds
`manager.release` (verified, version), `manager.runner`, `manager.files`
(the hard disk's files root), `manager.space` (free space) and the answers
`you.screen`, `you.sound`.

## Reply

`SELFTEST\t<passed>\t<failed>\t<skipped>`, then for each check that did not
pass, at most 16, `\t<group>.<check>|<f|s>|<detail>` (groups `manager`,
`native`, `guest`, `module`, `you`). The GUI names groups and checks in the
car's language; details stay as written.
