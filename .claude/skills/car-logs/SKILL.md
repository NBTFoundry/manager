---
name: car-logs
description: Get and read logs from the car after a test or a problem - an application's log, the iDrive system log, NBT Manager's debug log, crash lines, watchdog dumps - and turn them into findings. Use when the user reports something from the car or brings logs.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Logs from the car

The user usually drops files in the kit folder (workspace `CLAUDE.md`). Ask
for what is missing, saying how to get it:

| Log | How the user gets it | What it holds |
| --- | --- | --- |
| An application's log (`state/<app>/output.log`, 64 KiB) | phone app: the application's page, **Show log**, **Share** | its output; how it started and ended (`ended by signal 11 (SIGSEGV, ...)`, `stopped: no heartbeat ('R' on fd 3) for 3 s`); crash lines `[tag] FATAL sig=... pc=...` and `pc in <object>+0x...` |
| iDrive system log (`sloginfo`, a few minutes) | phone app: the car's page, **iDrive log**; desktop installer: **Save to this computer** (Head unit system log); SSH `sloginfo` | QNX and iDrive messages, `run fault pid ...` dumps, the watchdog |
| NBT Manager's debug log (`debug-<n>.log`) | the debug log switched on before the test; the installer's **Save to this computer**, or the phone app | `<seconds> <process>[pid] <event>` lines of GUI, worker, host, launcher, plugin |
| Cores and watchdog reports | SSH or USB (`docs/car-diagnostics.md`) | where each thread was; hangs |
| An application's own log | its README (some keep a larger one in `/dev/shmem` or on USB) | |

`/dev/shmem` is RAM: logs there are gone after the car sleeps.

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
