# Head unit diagnostics: what the car records and how to read it

What the head unit keeps when something goes wrong, the tools it ships for
looking at a running system, and its storage layout. Everything here was seen
on the user's car (NBTevo, `hu-omap`) on 2026-11-17 from files the user
copied to the NAS: `df.txt` (`df -h`), `files.txt` (`ls -l` of `/lib`,
`/sbin`, `/dev` and `/`) and `dump/` (the dumper's folder). The analysis that
used them is in [car-issues-2026-11-17.md](car-issues-2026-11-17.md).

These files are BMW's and come from the car: keep them on your own computer
and never add them to this repository (as for
[car-inventory.md](car-inventory.md)). Cores hold process memory: stacks, and
whatever a process had in them.

## The dumper and the watchdog

- `/boot/bin/dumper -m` is started at boot (starter package `DUMPER`). It
  writes a core file when a process faults, and when the watchdog asks.
- `watchdog5-omap` (`WD[<pid>]` in the system log) watches heartbeat probes
  of iDrive's processes. A missed heartbeat gives `WATCHDOG WARNING RECEIVED`,
  a longer one `WATCHDOG ERROR RECEIVED`; `Failure analysis is active` then
  dumps the processes involved. In `sloginfo` each dump shows as
  `run fault pid <pid> tid 1 why 0 what 0 ip <address> <program>`: a dump on
  request, not a crash (`why 0`). A process that stays stuck is then reset,
  and on 2026-11-17 iDrive as a whole was reset (the user's report).
- The dump folder holds one file per program name, overwritten by the next
  dump of that name; so a folder mixes events of different boots. The file
  times shown after copying are the copy's. Pids do not tell boots apart:
  the boot is deterministic, and HmiMain3D was pid 258101 in every boot of
  the manager's log. Match a watchdog report by its `Lifecycle Count` and
  `Monotonic time` (below), and a core by what is in it: the `ip` of the
  `run fault` line in that boot's `sloginfo`, the objects its threads wait on
  (the same addresses as in the report), the plugin release in its frames.
- Where the folder is on the head unit was not recorded when it was copied.
  `/sbin/copy_corefiles2usb.sh` (BMW's) presumably names it; the boot log has
  `link.sh` linking `/opt/sys/etc/HEADER.txt` to
  `/mnt/quota/sys/core/HEADER.txt`. Not established: write the path down
  with the next copy.

### Files in the dump folder

| File | Content |
| --- | --- |
| `<program>.core` | Core of a process dumped on a watchdog error (or a fault) |
| `<program>.warning.core` | Core taken on a watchdog warning |
| `wd_err_omap_<pid>.log`, `wd_warn_omap_<pid>.log` | Every thread of every process on the OMAP side: pid, parent, program, tid, thread name, priority, state, the object it is blocked on, and CPU seconds used. The best overview of a hang |
| `wd_*_<pid>_mem.log` | Per thread: code, data and stack sizes |
| `wd_err_omap_<pid>_Systemload.log` | The watchdog message, `Monotonic time` (seconds:ms since boot), `Lifecycle Count`, and the threads that used the most CPU in the last seconds |
| `wd_*_<pid>_servicebroker.log` | The DSI service broker's state (`servicebroker master`, version 4.0.57): registered services and clients |
| `wd_warn_DSI_jacinto_<pid>.log` | The thread table of the second processor (`hu-jacinto`), with CPU times |
| `fileArchive.log` | An index of dump files (name, size, times, a lifecycle number), but not of the files beside it: on 2026-11-17 its sizes differed from the copied files and its lifecycle numbers (10855 to 10894) from the reports' (11620 to 11622). Presumably the index of an archive kept elsewhere; not established |
| `save/dumperResetHist.txt` | A lifecycle count (11622 on 2026-11-17) |
| `save/Lifecycle.log` | `Current Lifecycle:` |

`Lifecycle Count` goes up by one per boot: the two watchdog errors of
2026-11-17 carry 11620 and 11622, two boots apart, matching the manager's
log.

Cores found on 2026-11-17: `HmiMain3D` (from the freeze of
[car-issues-2026-11-17.md](car-issues-2026-11-17.md), matched as above),
`LayerManagerService`, `screen`, `EarlyICam2App.warning`,
`NBTCarHU.warning`, `NavNBTEvoController` (and `.warning`), `TunerApp` (and
`.warning`), each from that freeze or the one of two boots before (same
signature); also older ones: `worker` (the manager's worker, pid 2912365, whose session ended
normally in the manager's log, so the trigger is not established), `app`
(Doom, pc `0x0121c56c`, the dev73 crash address in
[install-flow.md](install-flow.md)) and `dev-libasound` (the audio service,
its pid next to a boot's first manager session; trigger not established).

## Reading a core

`tools/qnx_core.py CORE` prints the loaded objects with their load addresses
and each thread's state, the object it is blocked on, and pc/lr as
object+offset. `--threads 1,24` limits the threads; `--stack [BYTES]` adds
the words on each stack that point into a loaded object.

- The note layout is in the script's header. It was read from these cores,
  not from QNX headers; `ntoarmv7-readelf -n` names only some of the notes.
- The dumper runs with `-m`: thread stacks are in the core, most library data
  is not. A mutex's owner, which lives in the mutex, cannot be read when the
  mutex is in a library's `.bss`.
- Every thread is stopped by the dump, so a spinning thread shows `STOPPED`
  at a libc call; its CPU use is in the watchdog report, not the core.
- The stack scan is heuristic: a return address stays on the stack after its
  call returned. Check a candidate frame against the code (the instruction
  before it must be a call).
- To put a name on an offset: iDrive's libraries export their symbols
  (`ntoarmv7-nm -D -n` on the copy in the `/opt` dump, then the nearest
  symbol below); the manager's plugin is stripped, so disassemble the
  plugin of the same release (extract it from the `.nbtr` by the size and
  SHA-256 in its header) and read the call before the return address.

Thread names come from the watchdog report. In HmiMain3D (2026-11-17):
tid 1 the main thread, 5 `tHMI_App`, 16 `tHMI_Remoting`, 23 to 26
`bmw::Looper` (the plugins' threads; which looper serves which plugin
changes from boot to boot), 27 `tHMI_BMW Remoting Server`, 30
`tHMI_Executor`.

## Storage

From `df -h` and `ls -l /`:

| Mount | Device | Size | Used | Use |
| --- | --- | --- | --- | --- |
| `/fs/sda0` | `sda0t179` (qnx6; `sda0` is the 8 GB MMC of `devb-sdmmc` in the boot log) | 6.3 GB | 82% | `/bin`, `/sbin` (both link to `/fs/sda0/bin`), `/etc`, `/lib`, `/opt` |
| `/fs/sda1` | `sda0t178` | 492 MB | 31% | `/var`, so the manager's `/var/opt/nbt-manager` |
| `/fs/sda31`, `/fs/sda32` | `sda0t177`, `sda0t180` | 200 MB each | 41% | Not established |
| `/fs/sda2` | `sda0t4` | 32 MB | 11% | Not established |
| `/fs/ram5` | `ramf0t77` | 14 MB | 1% | `/tmp` |
| `/ramdisk/mm` | RAM | 43 MB | 3% | Media |
| `/mnt/data` | `hd0t177` (the 186 GB disk `hd0`) | 132 GB | 37% | Navigation and media data |
| `/mnt/share` | `hd0t178` | 14 GB | 5% | Not established |
| `/mnt/quota/mm`, `/mnt/quota/sys` | `hd0t179`, `hd0t180` | 38 GB, 1.2 GB | 4%, 5% | Media; system (traces, presumably cores) |
| `/mnt/umass…` | USB stick | | | Plugged-in storage |

## Tools on the head unit

In `/sbin` (= `/fs/sda0/bin`), among others: `pidin`, `hogs`, `showmem`,
`sloginfo`, `slay`, `on`, `use`, `ldd`, `waitfor`, `tracelogger` and
`start_kerneltrace.sh`, `tcpdump`, `netstat`, `sockstat`, `pfctl`,
`ifconfig`, `sqlite3`, `vi`, `bsdtar`, `gzip`, `pdebug` and `qconn` (QNX's
remote debug and IDE agents: a live process could be debugged from a
computer; not tried), `sshd`, `telnetd`, `inetd`, BMW's
`copy_corefiles2usb.sh` (584 bytes, not read), and the watchdog
`watchdog5-omap`.

`/lib` (= `/fs/sda0/lib`) has, besides libc and the graphics stack:
`libasound.so.2` (a second copy outside `/opt/mm/lib/libasound/`; what it
depends on is not checked), `libcrypto.so.1` and `.so.2`, `libssl.so.1` and
`.so.2`, `libbacktrace.so.1`, `libsqlite3.so.1`, `libxml2.so.1`,
`libarchive.so.12`, `libz.so.2`, `libstdc++.so.6`, `libcpp.so.4`,
`libicu*.so.46`, `libscreen.so.1`, `libpps.so.1`, `libsocket.so.3`.

In `/dev`: `layermanager`, `screen`, `pf` and `bpf`, `tun0`-`tun3`, `tap`,
`btspp0`, `libasound/`, `io-amp/`, `hbwatchdog`, `slog`, `shmem`,
`sysregs`, `dspipc` (a link to `/net/hu-jacinto/dev/dspipc`), and the disks
(`hd0t177`-`180`, `sda0t4`, `t101`, `t102`, `t177`-`180`).

## Collecting after an event

1. Right after the event (before the next one overwrites the dumps): copy the
   dump folder, `sloginfo` (a ring buffer of a few minutes) and the manager's
   log. Write down the dump folder's path.
2. Note the time since boot of the event, or the pids: dump files carry no
   useful date once copied.
3. For a hang, the `wd_err_omap_*.log` and `_Systemload.log` of the event
   say which threads used the CPU and what the others wait on; the cores say
   where each thread was.
