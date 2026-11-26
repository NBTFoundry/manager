---
name: car-logs
description: Get and read logs from the car after a test or a problem - an application's log, the iDrive system log, NBT Manager's debug log, crash lines, watchdog dumps - and turn them into findings. Use when the user reports something from the car or brings logs.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Logs from the car

The user usually drops files in the kit folder (workspace `CLAUDE.md`). Ask
for what is missing, saying how to get it:

The user has only the phone in the car: ask for phone paths. With the Wi-Fi
ENET adapter, one file holds nearly everything: phone app, **Install NBT
Manager on a car** › Connect › More tools › **Collect everything** › Share
(installer `core/src/collect.rs`).

| Log | How the user gets it | What it holds |
| --- | --- | --- |
| Collect everything (`collect-<time>.txt`) | phone app over the ENET adapter, as above (or the Windows installer's Test tools) | the debug log, `sloginfo`, every application's log, `audioconn -l`, `pidin arg`, the process manager's overview, the capture shim's state, `ls -lR` of the manager's folders and `/dev/shmem` |
| An application's log (`state/<app>/output.log`, 64 KiB) | phone app: the application's page, **Show log**, **Share** | its output; how it started and ended (`ended by signal 11 (SIGSEGV, ...)`, `stopped: no heartbeat ('R' on fd 3) for 3 s`); crash lines `[tag] FATAL sig=... pc=...` and `pc in <object>+0x...` |
| Crash reports (`state/<app>/crash.<n>`, `state/manager/crash.<n>`, five each) | phone app: the car's page, **Crash reports**, **Share all** (one file); or one report, **Share** | one per crash, kept after later runs: version, how it was started and ended, the run's last 32 KiB of output (crash lines included), the system log's `run fault pid` lines; NBT Manager's own: the GUI's crash record and the debug log's end |
| iDrive system log (`sloginfo`, a few minutes) | phone app: the car's page, **iDrive log**; desktop installer: **Save to this computer** (Head unit system log); SSH `sloginfo` | QNX and iDrive messages, `run fault pid ...` dumps, the watchdog |
| NBT Manager's debug log (`debug-<n>.log`) | the debug log switched on before the test; the installer's **Save to this computer**, or the phone app | `<seconds> <process>[pid] <event>` lines of GUI, worker, host, launcher, plugin |
| Cores and watchdog reports | SSH or USB (`docs/car-diagnostics.md`) | where each thread was; hangs |
| An application's own log | its README (some keep a larger one in `/dev/shmem` or on USB) | |
| Self test results | the car's screen (Settings › Self test › Last results), the debug log's `self test:` lines | what works on this car: release, runner, files, each form's screen, sound, car data, diagnostics, controller (self-test skill) |

`/dev/shmem` is RAM: logs there are gone after the car sleeps.

A problem that may be the platform's rather than one application's: ask for
a self test run first.

Reading:

1. Put events in order by time since boot, and match them to what the user did.
2. A crash in our code: the unstripped binary of the same commit
   (`build/*-unstripped`) and `ntoarmv7-addr2line -f -C -e <binary> <offset>`
   with the QNX environment sourced. In a QNX library: the SP1 debug symbols
   (workspace `CLAUDE.md`). In iDrive's: `ntoarmv7-nm -D -n` on a copy.
3. A core: `tools/qnx_core.py CORE` (`--threads`, `--stack`). A hang: the
   watchdog reports first (`docs/car-diagnostics.md`).
4. Report: what happened, the lines that show it (quoted), the cause, and the
   fix or the next test.

Logs and dumps stay out of every repository: they can hold the VIN,
positions and BMW's files. Quote only the lines that matter.
