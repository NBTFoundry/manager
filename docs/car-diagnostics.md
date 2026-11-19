<!-- SPDX-License-Identifier: MPL-2.0 -->
# Head unit diagnostics

What the head unit records when something goes wrong, how to read it, the tools
it ships for looking at a running system, and its storage layout (NBT Evo,
`hu-omap`).

Dumps, watchdog reports and listings are BMW's files from a car: keep them on
your own computer and never add them to this repository (as for
[car-inventory.md](car-inventory.md)). Cores hold process memory: stacks, and
whatever a process had in them.

## The dumper and the watchdog

- `/boot/bin/dumper -m` starts at boot (starter package `DUMPER`). It writes a
  core file when a process faults, and when the watchdog asks.
- `watchdog5-omap` (`WD[<pid>]` in the system log) watches heartbeat probes of
  iDrive's processes. A missed heartbeat gives `WATCHDOG WARNING RECEIVED`, a
  longer one `WATCHDOG ERROR RECEIVED`; `Failure analysis is active` then dumps
  the processes involved. In `sloginfo` each dump shows as
  `run fault pid <pid> tid 1 why 0 what 0 ip <address> <program>`: a dump on
  request, not a crash (`why 0`). A process that stays stuck is then reset, up to
  iDrive as a whole.
- The dump folder holds one file per program name, overwritten by the next dump
  of that name, so a folder mixes events of different boots. File times after
  copying are the copy's. Pids do not tell boots apart either: the boot is
  deterministic, so a program gets the same pid every time. Match a watchdog
  report by its `Lifecycle Count` and `Monotonic time` (below), and a core by what
  is in it: the `ip` of the `run fault` line in that boot's `sloginfo`, the
  objects its threads wait on (the same addresses as in the report), the plugin
  release in its frames.
- BMW's `/sbin/copy_corefiles2usb.sh` copies the dumps to a USB stick; the boot
  log links `/opt/sys/etc/HEADER.txt` to `/mnt/quota/sys/core/HEADER.txt`. Write
  down the folder's path with each copy.

### Files in the dump folder

| File | Content |
| --- | --- |
| `<program>.core` | Core of a process dumped on a watchdog error (or a fault) |
| `<program>.warning.core` | Core taken on a watchdog warning |
| `wd_err_omap_<pid>.log`, `wd_warn_omap_<pid>.log` | Every thread of every process on the OMAP side: pid, parent, program, tid, thread name, priority, state, the object it is blocked on, and CPU seconds used. The best overview of a hang |
| `wd_*_<pid>_mem.log` | Per thread: code, data and stack sizes |
| `wd_err_omap_<pid>_Systemload.log` | The watchdog message, `Monotonic time` (seconds:ms since boot), `Lifecycle Count`, and the threads that used the most CPU in the last seconds |
| `wd_*_<pid>_servicebroker.log` | The DSI service broker's state (`servicebroker master`): registered services and clients |
| `wd_warn_DSI_jacinto_<pid>.log` | The thread table of the second processor (`hu-jacinto`), with CPU times |
| `fileArchive.log` | An index of dump files (name, size, times, a lifecycle number) of an archive kept elsewhere; it does not describe the files beside it |
| `save/dumperResetHist.txt` | The current lifecycle count |
| `save/Lifecycle.log` | `Current Lifecycle:` |

`Lifecycle Count` goes up by one per boot, which dates a report relative to the
manager's own log.

## Reading a core

`tools/qnx_core.py CORE` prints the loaded objects with their load addresses and
each thread's state, the object it is blocked on, and pc/lr as object+offset.
`--threads 1,24` limits the threads; `--stack [BYTES]` adds the words on each
stack that point into a loaded object.

- The note layout is in the script's header; `ntoarmv7-readelf -n` names only
  some of the notes.
- The dumper runs with `-m`: thread stacks are in the core, most library data is
  not. A mutex's owner, which lives in the mutex, cannot be read when the mutex is
  in a library's `.bss`.
- Every thread is stopped by the dump, so a spinning thread shows `STOPPED` at a
  libc call; its CPU use is in the watchdog report, not the core.
- The stack scan is heuristic: a return address stays on the stack after its call
  returned. Check a candidate frame against the code (the instruction before it
  must be a call).
- To name an offset: iDrive's libraries export their symbols (`ntoarmv7-nm -D -n`
  on a copy from the `/opt` dump, then the nearest symbol below); the manager's
  plugin is stripped, so disassemble the plugin of the same release (extract it
  from the `.nbtr` by the size and SHA-256 in its header, or use
  `build/qnx-unstripped/`) and read the call before the return address.

Thread names come from the watchdog report. In HmiMain3D: tid 1 is the main
thread, 5 `tHMI_App`, 16 `tHMI_Remoting`, 23 to 26 `bmw::Looper` (the plugins'
threads; which looper serves which plugin changes from boot to boot), 27
`tHMI_BMW Remoting Server`, 30 `tHMI_Executor`.

## Storage

| Mount | Device | Size | Contents |
| --- | --- | --- | --- |
| `/fs/sda0` | `sda0t179` (qnx6, on the 8 GB MMC `sda0`) | 6.3 GB | `/bin`, `/sbin` (both link to `/fs/sda0/bin`), `/etc`, `/lib`, `/opt` |
| `/fs/sda1` | `sda0t178` | 492 MB | `/var`, so the manager's `/var/opt/nbt-manager` |
| `/fs/sda31`, `/fs/sda32` | `sda0t177`, `sda0t180` | 200 MB each | — |
| `/fs/sda2` | `sda0t4` | 32 MB | — |
| `/fs/ram5` | `ramf0t77` | 14 MB | `/tmp` |
| `/ramdisk/mm` | RAM | 43 MB | Media |
| `/mnt/data` | `hd0t177` (the 186 GB disk `hd0`) | 132 GB | Navigation and media data |
| `/mnt/share` | `hd0t178` | 14 GB | — |
| `/mnt/quota/mm`, `/mnt/quota/sys` | `hd0t179`, `hd0t180` | 38 GB, 1.2 GB | Media; system (traces, cores) |
| `/mnt/umass…` | USB stick | | Plugged-in storage |

## Tools on the head unit

In `/sbin` (`/fs/sda0/bin`), among others: `pidin`, `hogs`, `showmem`,
`sloginfo`, `slay`, `on`, `use`, `ldd`, `waitfor`, `tracelogger` and
`start_kerneltrace.sh`, `tcpdump`, `netstat`, `sockstat`, `pfctl`, `ifconfig`,
`sqlite3`, `vi`, `bsdtar`, `gzip`, `pdebug` and `qconn` (QNX's remote debug and
IDE agents), `sshd`, `telnetd`, `inetd`, BMW's `copy_corefiles2usb.sh`, and the
watchdog `watchdog5-omap`.

`/lib` (`/fs/sda0/lib`) has, besides libc and the graphics stack:
`libasound.so.2` (a second copy outside `/opt/mm/lib/libasound/`),
`libcrypto.so.1` and `.so.2`, `libssl.so.1` and `.so.2`, `libbacktrace.so.1`,
`libsqlite3.so.1`, `libxml2.so.1`, `libarchive.so.12`, `libz.so.2`,
`libstdc++.so.6`, `libcpp.so.4`, `libicu*.so.46`, `libscreen.so.1`,
`libpps.so.1`, `libsocket.so.3`.

In `/dev`: `layermanager`, `screen`, `pf` and `bpf`, `tun0`–`tun3`, `tap`,
`btspp0`, `libasound/`, `io-amp/`, `hbwatchdog`, `slog`, `shmem`, `sysregs`,
`dspipc` (a link to `/net/hu-jacinto/dev/dspipc`), and the disks (`hd0t177`–`180`,
`sda0t4`, `t101`, `t102`, `t177`–`180`).

## Collecting after an event

1. Right after the event, before the next one overwrites the dumps: copy the
   dump folder, `sloginfo` (a ring buffer of a few minutes) and the manager's log.
   The installers' **Collect everything** gets the system log and the manager's
   files in one report.
2. Note the time since boot of the event, or the pids: dump files carry no useful
   date once copied.
3. For a hang, the `wd_err_omap_*.log` and `_Systemload.log` of the event say
   which threads used the CPU and what the others wait on; the cores say where
   each thread was.
