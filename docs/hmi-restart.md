# Restarting iDrive (HmiMain3D) alone

Goal (the user, 2026-11-18): restart only iDrive's program after Install or
Update, while the installer is still connected, so a problem with the capture
shim shows at once and can be undone over the same SSH session, instead of
`OnOffDSICommander appreset` restarting the whole system (and switching SSH
off).

Everything below comes from the car's own files, read on this computer
(`/home/mobyfab/src/reverse/opt`, the inventory): `starter.cfg`, the process
manager `/opt/sys/bin/starter` (SHA-256 `dc6bac6d…5d4a`) and
`OnOffDSICommander`. Nothing has run on the car. Those files are BMW's and stay
out of this repository.

## Established

- **`appreset` restarts the whole head unit** (the user, 2026-11-18: "It DOES
  restart the whole OS"), although `OnOffDSICommander` names it
  `ApplicationReset` ("do an application reset"); the others are system, ECU,
  BIOS and bootloader resets. None restarts only iDrive. Until 2026-11-18 the
  installers and the manager called it "Restart iDrive"; they now say
  "Restart the head unit" and ask first.
- **The process manager runs everything in `starter.cfg`** (`starter -A -s -e
  -g -vvv -p 30 -S libnbt_starter_plugin-omap.so ... -c
  /opt/sys/etc/starter.cfg`, inventory `pidin-arg.txt`) and groups processes
  into packages, each requested to `RUN` or `STOP`.
- **Package 14, `HMI`, holds only process 17, HmiMain3D.** Its configured
  state is `STOP`, so it is requested to run during start-up. It requires
  package 13 (`HMI_BMW_OTF_AGENT`, process 16). No package requires package
  14.
- **HmiMain3D's process entry**: `OnTerminate IGNORE_ALL`,
  `MaxProcessRestarts 0`, `FinalAction IGNORE`: the process manager neither
  restarts it nor acts when it ends.
- **`inetd`, which starts `sshd`, does not depend on HmiMain3D** (it requires
  interfaces 36 and 31; HmiMain3D provides none).
- **Package requests reach the process manager as a `devctl` on its resource
  manager `/dev/starter`** (handler at `0x10ffb0`):
  - `0x800c050f` (to the manager, 12 bytes): `{uint32 domain, uint32 package,
    uint32 command}`, passed to the request function at `0x11416c`
    (`startmgr_requestPackages`). The package key is `domain << 16 |
    package`; a command above 3 is refused ("invalid comand"), an unknown
    package too ("invalid package [d].[p]").
  - Commands, from the configuration parser's table (`0x128cfc`): `RUN` = 2,
    `STOP` = 3 (the request function logs 2 as `RUN`, anything else as
    `STOP`).
  - `0xc0080508` (8 bytes both ways): a package query returning its state
    (layout still to be read from the assembly).
  - A plain write of a package command (`io_write`, `0x110614`) logs
    "Package command ... received" but requests package 0.0 with zeros: not
    usable.
- **Domains**: `starter.cfg`'s domain table lists 1 Connectivity, 2 Media, 3
  NavigationEceUsa, 4 NavigationAsn, each with its own file; the main file's
  packages are most likely domain 0 (not established).

- **Each process's `<EnvFile>` is read at every start of that process**
  (2026-11-18, the start function at `0x113aac`, the only caller of the
  spawn at `0x113e60`): when the entry is not empty the process manager
  checks the file with `stat` (`0x109080`). Missing, it logs "extern
  environment file: %s not available for process %s!" at level 3 and starts
  the process normally (`0x113b9c`). Present, it reads it (`0x1064c8`,
  `env_updater.c`): `fopen(..., "r")`, lines of up to 512 bytes, `#`
  comments and lines without `=` skipped, `sscanf("%[^=]=%[^\n]")`; a name
  or value with a space between words is rejected, leading spaces, single
  quotes and a trailing `;` are removed (`0x106394`). Each entry *replaces*
  a variable of the same name (`0x106734`, "environment replaced") after
  the `Environment` table entries are applied (`0x1069e4`), so an
  `LD_PRELOAD` line must repeat BMW's three preloads. The entries are freed
  after the start, so the next start reads the file again. Every process in
  the car's `starter.cfg` has an empty `<EnvFile/>`, HmiMain3D included.
- **The process manager also takes `LD_PRELOAD_<program name>` from its own
  environment** (`0x10694c`), applied after the file; set only when the
  process manager starts, so of no use here.
- **The lifecycle manager uses the same path**: `OnOffHuOmapApp` names
  `/dev/starter/start` and contains `0x800c050f` once; it logs
  "requestPackageState packageRequest domainId: %d packageId: %d
  requestedState: %d". It also keeps package owners of its own
  (`COnOffStarterManagerAdminHandler::setRequestedPackageState`, "AdminID"),
  which a direct request bypasses.

## Car run 2026-11-18: iDrive cannot be restarted alone this way

`--restart-hmi` on dev105 (installer log `installer-1795022708.log`, system log
`slog-1795023107.log` on the NAS):

- **The request reaches the process manager**: it logged `Package [14]: 'STOP'
  'HMI'`, then, when `RUN` followed 10 s later, `'POST_STARTING'` and `'RUN'`.
  So domain 0, package 14, the `devctl` and its command values are right, and
  the lifecycle manager did not react: no reset.
- **HmiMain3D did not stop**: its process entry has
  `<Shutdown>IGNORE</Shutdown>`, so stopping its package leaves it running.
- **Forcing it is not safe**: the process manager's start wrapper (`0x116090`)
  refuses a process that has used up its restarts (`MaxProcessRestarts` 0 for
  HmiMain3D) and can log "Shut down System caused by %d restarts of %s". Ending
  HmiMain3D ourselves and requesting `RUN` might take that path.

So the shim is loaded only at a real start of iDrive (through the environment
file, below). The Windows installer no longer tries the shim after Update and
no longer offers **Restart iDrive only** or **Try the shim**; the launcher's
`--restart-hmi` and `--try-shim` remain for diagnosis only.

## Not established

- The access mode `/dev/starter/start` needs for the `devctl`.
- Whether the lifecycle manager notices a direct request and requests `HMI`
  back (or resets) because its own record disagrees.
- That the main file's domain is 0.
- **Whether a heartbeat watchdog resets the unit while HmiMain3D is stopped**:
  the lifecycle manager (`OnOffHuOmapApp --heartbeat-interval=2`), `CSI_NBT`
  (`WatchdogHandler`) and `/dev/starter/watchdog` all exist; the user sees a
  reset about a minute after iDrive fails. A stop followed at once by a run may
  stay inside that time.
- What other processes do while HmiMain3D is gone (LayerManager, the DSI
  services, `ziploader`, which serves HmiMain3D's image).

## Not established (environment file)

- That `starter.cfg` is read only at boot (no reload was found), so the
  `<EnvFile>` entry the install adds takes effect after one restart of the
  head unit; until then the check logs "has not started with it yet".
- That `/var/opt` is mounted when the process manager first starts iDrive
  (HmiMain3D's own arguments name `/var/opt/hmi/data/rw/`, so it most likely
  is; if not, iDrive starts without the shim, logged at level 3).
- What a `STOP` then `RUN` of package 14 does after HmiMain3D ended by itself
  (the trial's recovery).

## Built (2026-11-18, not yet run on the car)

- `nbt-manager-launcher --restart-hmi` (`src/launcher/hmi_restart.cpp`):
  finds HmiMain3D (by `/proc/<pid>/as` path, as the capture shim does),
  requests `STOP` of package 0.14, waits up to 10 s for that pid to end,
  requests `RUN` (also when the stop did not complete, so the package is never
  left stopped), and waits up to 30 s for a new HmiMain3D. It prints each step
  (`HMI_RESTART ...`) as it happens; the last line is `HMI_RESTART OK ...` or
  `HMI_RESTART FAILED ...`. A refused `STOP` changes nothing. The sequence is
  host-tested against a fake process manager (`tests/test_hmi_restart.cpp`).
- The Windows installer's Test tools: **Restart iDrive only (experimental)**
  runs it, shows its lines and passes only on `HMI_RESTART OK`; a dropped
  connection is reported as a probable reset. Not in the phone app's
  installer yet (its tool list is generated separately).

- **The shim moved to iDrive's environment file** (supersedes the
  Environment 6 edit of dev91-dev99). Install and Update set HmiMain3D's
  `<EnvFile/>` to `<EnvFile>/var/opt/nbt-manager/state/manager/hmi.env</EnvFile>`
  (one edit, `src/storage/starter_config.cpp`). The file, when present, holds
  one line,
  `LD_PRELOAD=libEGL_X.gpp.so:libGLESv2_X_FORWARD.gpp.so:libKD_X.gpp.so:/var/opt/nbt-manager/state/manager/libnbt-capture.so`,
  written whole and renamed into place (`src/storage/hmi_env.cpp`). An older
  install (the shim in Environment 6) is recognised: Update moves it, Remove
  restores stock.

## The shim, kept automatically (2026-11-18, not yet run on the car)

The user asked for the shim's testing and keeping to be automatic. Each
arming of the file covers one start of iDrive:

- **The shim deletes the file when it loads into iDrive** (a constructor in
  `src/capture/shim_env.c`, before iDrive's `main`; only in HmiMain3D, by the
  same process check). So whatever happens to that start, the next one is
  stock unless the manager arms the file again. The car's own libraries use
  the same start-up mechanism (`DT_INIT_ARRAY`, e.g.
  `libsys_dsi_colibry.cpp-ne.so.8`).
- **Install and Update arm a new shim**: after it passed the load test and the
  live test, the file is written and `state/manager/shim-state` records
  `armed` with the shim's SHA-256. The same shim installed again keeps its
  state.
- **60 s after iDrive starts, the plugin runs `launcher --shim-check`**
  (`plugin/phone_runtime.cpp`, `src/launcher/shim_control.cpp`, without the
  manager's operation lock so an open manager cannot make it fail). From the
  record, whether this iDrive has `libnbt-capture.so` mapped
  (`DCMD_PROC_MAPINFO`, `DCMD_PROC_MAPDEBUG`) and whether the file is still
  there:
  - mapped, file gone: `kept`, strikes cleared, the file written again for
    the next start;
  - not mapped, file there: still waiting for a start that reads it (after an
    update the process manager most likely needs one restart of the head
    unit to read the new `<EnvFile>`);
  - not mapped, file gone: a start with the shim ended before 60 s (iDrive
    failed, or the car was switched off early). The first time it is armed
    again; the second time in a row the state becomes `failed` and the file
    stays away;
  - mapped but the file still there (the shim did not delete it, so a failed
    start would repeat): `failed`, file removed;
  - `failed` or `off`: nothing changes.
- `--shim on` arms it again with no strikes (after a failure); `--shim off`
  removes the file and records `off`. A new shim (Update) is armed whatever
  the old one's state; Remove deletes the file and the record first.
- `--try-shim` does the same at once while connected: stops iDrive, writes
  the file once iDrive has stopped, runs iDrive, checks that the new iDrive
  mapped the shim and deleted the file, and watches it 20 s. Passed: `kept`
  and armed again. Ended or not back: `failed`, and package 14 is stopped and
  run again without the shim.
- The Windows installer's Test tools: **Try the shim (restarts iDrive)**,
  **Shim on**, **Shim off**; each shows the launcher's lines and passes only
  on its OK line. Not in the phone app's installer.

**The shim lives next to the file** (dev105, the user, 2026-11-18): the only
case that could repeat a failure is a shim the loader refuses before its
constructor runs. The load test refuses that at install. Afterwards the shim
could only go missing with its environment file: both are in
`/var/opt/nbt-manager/state/manager/` (the shim `0700`, as the load test's
copy the same loader read there), so an update of `/opt` cannot remove one and
keep the other, and if `/var/opt` were not mounted yet at boot neither would
be there and iDrive would start as stock. HmiMain3D runs as root (its files in
`/var/opt/hmi/data/ID5` are root's, inventory `ls-var-hmi.txt`), as the
manager's state does. Install no longer remounts `/fs/sda0` for the shim, only
for `starter.cfg`; the old folder (`/opt/hmi/ID5/data/ro/NbtCapture`) is
removed in the same writable window. Children of iDrive get both paths
stripped from their `LD_PRELOAD`.

## Next

1. **In the car**: Update, switch the car off and on, use iDrive for a
   minute, then save the debug log (`capture:` lines).
2. **Read only, on the car**: `tools/car_inventory.sh` now saves the
   `/dev/starter` listing, `version`, `variant` and `status/overview`
   (`starter-dev.txt`), to confirm package 14's domain and state.
3. **Here**: read the query's layout, and how the lifecycle manager
   (`OnOffHuOmapApp`) requests packages, to copy its exact sequence.
4. Only after car runs may the installer use the iDrive-only restart in
   place of `appreset`.
