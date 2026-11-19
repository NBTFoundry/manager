# 2026-11-17: App Manager freezes iDrive on launch

The user: opening App Manager froze iDrive; the screen and the controller
stopped responding, and the watchdog eventually reset iDrive. Seen with
dev79 (about 33 s after a boot) and with release 83 (43 s after a boot, and
once 12 minutes after one).

Evidence, on the NAS: `manager.log` (the manager's debug log, five boots),
`idrive.log` (`sloginfo` of the last boot, to 1:23 after boot), the dumper's
folder `dump/` with cores and watchdog reports, plus `df.txt` and `files.txt`
(described in [car-diagnostics.md](car-diagnostics.md)). The car ran release
83; dev86 was on the NAS but not installed.

dev87 sends focus events from a thread of the plugin's own and logs a
snapshot of iDrive's threads when one does not return (below).

## What happens

The plugin, which runs inside iDrive's HMI process (HmiMain3D), asks iDrive
to move its focus to the blank state that holds iDrive while the manager is
shown (focus event 50, `PluginApp::triggerEvent(50)`). That call never
returns:

```text
43.011 launcher[622699] open
43.508 launcher[622699] GUI ready after 335 ms
43.550 plugin[258101] hold phase 2: hold
43.550 plugin[258101] focus event 50 begin
50.938 gui[622703] input menu (ignored: hidden)
```

- The plugin never gets the focus callbacks, so it never tells the GUI to
  show: the GUI stays hidden and ignores every input (`ignored: hidden`).
- In the same seconds three HmiMain3D threads named `bmw::Looper` start using
  the CPU: 55%, 54% and 24% in the watchdog's 17.6 s window at 1:19; in its
  report at 2:06, 47.0, 46.9 and 27.5 CPU seconds. iDrive draws nothing; from
  1:00 iDrive's own processes miss their heartbeats (TunerApp at 1:00,
  NBTCarHU at 1:05, EarlyICam2App at 1:19), the watchdog dumps them and, at
  1:19, HmiMain3D, LayerManagerService and Screen; its error report follows
  at 2:06, after the system log the user saved, and iDrive is then reset
  (the user).
- Elsewhere in the log the same call returns in 4 to 52 ms. Of every focus
  event in the log, two never returned, both event 50: boot 5 at 43.550 s and
  boot 3 at 756.118 s (`tools` below). Boot 3's came after an installer update
  that replaced the plugin file while iDrive had it loaded
  (`launcher replaced, plugin replaced` at 119 s); boot 5's had no update.
  Other opens of release 83 worked, also 20 and 25 s after a boot.
- dev79's freeze ([install-flow.md](install-flow.md), dev81) has the same
  signature: three `bmw::Looper` threads at 56%, 55% and 32%. Its manager log
  was not collected; that its focus event 50 also hung is likely but not
  established.

## Threads at the time (core and watchdog report)

From `HmiMain3D.core` read with `tools/qnx_core.py`, and from
`wd_err_omap_200743.log` (2:06 after boot, Lifecycle Count 11622). The core
is this freeze's: tid 1's pc `0x039a6714` is the `ip` the watchdog logged at
1:19 in this boot, tid 24 waits on the same object as in the report, and its
frames are release 83's plugin. (Its pid, 258101, is the same in every boot.)
Boot 3's freeze left `wd_err_omap_200745_Systemload.log` (Lifecycle Count
11620, 820 s after boot): the same three loopers at 51% each.

| tid | Name | State | Where |
| --- | --- | --- | --- |
| 24 | `bmw::Looper`, our plugin's thread in this boot | CONDVAR on `0x0cf37bec`, 0.009 CPU s | Our poll runnable → `apply` → `focusEvent(50)` → `PluginApp::triggerEvent` (`libAppFramework.A.so+0x54a4`) → condition wait |
| 23, 25, 26 | `bmw::Looper` | READY/RUNNING, 27.5 to 47 CPU s | Stopped by the dump at libc's condition wait (`+0x38690`, called from `+0x1ee38`); tid 25 had last waited on `0x0cf37bec`, the same object as tid 24 |
| 1 | main | MUTEX on `0x791c2638` in the core, READY at 2:06 | `hmiplugin::getInstanceCacheByIdentifier` (`libhmiplugin.so+0x17ddc`) → `CHmiSingleLock` on a static `COSMutex` in `libhmiplugin.so`'s `.bss` (`+0x30638`) |
| 27 | `tHMI_BMW Remoting Server` | CONDVAR on `0x0a2ebde4` | A pointer to `PluginProxyScope::requestPluginInstanceInternal` (`libhmiplugin.so+0x1eca0`) and `libhmiremoting.so` on its stack |

The plugin frames were named from the dev83 plugin taken out of
`NBT-Manager-dev83.nbtr` by its size and SHA-256: `+0x1a14` is
`bl focusEvent` with `r1 = 50`, `+0x14b4` the call through the PLT to
`_ZN3bmw9PluginApp12triggerEventEj`. A `libNbtManager.so+0x3c24` word higher
on tid 24's stack is the return of an earlier `kill(pid, 0)` (the liveness
check), not an active frame.

## What it means

Established: `triggerEvent(50)` blocks our plugin's thread inside iDrive's
plugin framework, waiting for work that iDrive's looper threads should
complete, while those threads spin; iDrive's main thread at the same moment
waits for the plugin framework's global lock. Our call never returns, so
iDrive stays held on nothing.

Not established:

- Which thread holds the plugin framework's lock: the dumper's `-m` left the
  library's `.bss` out of the core. The remoting server thread (27) is in a
  plugin-instance request; that it holds the lock and waits for the main
  thread is a hypothesis (a lock-order deadlock between the HMI's main thread
  and its remoting thread, with our wait on top).
- Why it happens on some opens only. NBT-AAD calls `triggerEvent` from its
  posted runnables too, and focus event 52 (sent from the menu action) never
  hung; nothing so far tells the hanging opens apart.
- Whether the manager does something that makes it more likely. Ruled out:
  the volume display's second window (surface 1292, since `095487e`), created
  at every GUI start, was there in seven opens that worked.

Corrected: the dev81 handover took the missing LayerManager answers for the
cause. They are a consequence: iDrive's HMI was spinning. dev81's change only
makes the manager give up after one attempt.

## What `triggerEvent` waits for

Read from the car's `libAppFramework.A.so` and `HmiMain3D` (`/opt` dump),
following tid 24's return addresses (`HmiMain3D` is not position independent,
loaded at `0x100000`; offsets above are from there, addresses below are its
own):

- `PluginApp::triggerEvent(unsigned)` (`+0x5488`) only calls slot `0x6c` of
  the dispatcher object at `this+0x58`.
- In HmiMain3D that is a `CLocalCommAdapter` method (`0x23b71d8`, named from
  the nearest symbol): it builds an empty `Parcel` and calls `0x23b7040`,
  which, when the adapter is connected, sends the event through the
  `CHmiGenMessenger` at `adapter+0x108` (`0x23c1f20`, return `0x23b710c` on
  tid 24's stack).
- That send allocates a message (type 9) and a reply object, queues the
  message under the messenger's mutex, wakes the receiver, then waits on the
  reply's condition variable (`0x25629b0`) **with no timeout**, and returns the
  reply's result.

So a focus event is a synchronous round trip to the thread that serves the
messenger, presumably iDrive's main thread: with the main thread waiting for
the plugin framework's lock, our call cannot return. Whether something waits
for our plugin's thread in turn (a cycle through our thread) is not
established. The `requestPluginInstanceInternal` word on tid 27's stack is a
thunk's address, possibly a stale value, not proof of a live call.

## dev87: mitigation and telemetry

- **Focus events leave the plugin's thread.** The plugin sends events 50, 51
  and 52 from a thread of its own (`nbt focus`, `plugin/focus_events.cpp`,
  `src/gui/focus_sender.cpp`), queued in order, with an event already waiting
  never queued twice. The plugin's `bmw::Looper` thread no longer waits on
  iDrive, so if iDrive waits on that thread (a callback, an instance request)
  the cycle cannot close through us. If iDrive's main thread is stuck for a
  reason of its own, this changes nothing: iDrive still freezes, and the
  telemetry below says why.
- **A stuck event is detected.** After 3 s without a return, the plugin's
  poll drops the hold (`iDrive does not answer; hold dropped, manager aside`),
  tells the GUI to step aside, and drops further focus events while it lasts
  (`focus event N dropped: event 50 in iDrive for N ms`). A return logs
  `focus event N returned after N ms` (the time is new).
- **Thread snapshot** (`plugin/hmi_threads.cpp`). At 3 s and again at 10 s a
  watch thread (`nbt focus watch`) writes every HmiMain3D thread to the
  manager's debug log: id, name, state, priority, CPU time, and for a thread
  blocked on a mutex, condition variable or semaphore its address, the
  object's two words (`[count owner]`: a mutex's owner thread is in the
  second) and how long it has waited; its code address and up to 15 code
  addresses from its stack, each as object+offset (`dladdr`). Read through
  `DCMD_PROC_TIDSTATUS` on the process's own `/proc` entry, inside the
  process, so the owner of the plugin framework's lock is readable (the
  dumper's core left it out). Two snapshots 7 s apart show which threads
  spin (CPU time). Lines start `hmi threads (focus event 50, 3000 ms)`,
  `tid N '<name>' <state> ...` and `  tid N stack ...`.
- Not tested in the car. Host tests (`tests/test_focus_sender.cpp`) cover the
  order, the duplicates, a stuck event (both reports, dropped events,
  recovery) and stopping while stuck. The snapshot code runs only on QNX: no
  host test. `pthread_getname_np`, `pthread_setname_np` and `dladdr` are
  looked up with `dlsym`, so a libc without them leaves names out instead of
  failing the plugin's load; the other new imports are libc's pthread
  functions, `devctl` and `strrchr`.

## Next steps

1. If it freezes with dev87: the manager's debug log has the snapshots. The
   main thread's `on <address> [count owner]` names the lock's owner thread;
   that thread's stack says what it waits for.
2. Ask what the lock in `libhmiplugin.so` protects, and who takes it on the
   remoting side (`requestPluginInstanceInternal`).
3. Capture more: copy the dump folder right after a freeze (with its path),
   and if possible run `pidin` on HmiMain3D during one. `pdebug` is on the
   head unit; a live stack of every HmiMain3D thread would settle the owner
   of the lock (not tried).
4. Meanwhile: do not replace the plugin file while iDrive has it loaded
   (restart iDrive after a plugin change), and avoid opening App Manager in
   the first minute after a start until this is understood. Both hangs on
   2026-11-17 had one of the two, but so did opens that worked: a precaution,
   not a cause.

## Tools

- `tools/qnx_core.py`: the core reader used above
  ([car-diagnostics.md](car-diagnostics.md), "Reading a core").
- Hanging focus events were found by pairing `focus event N begin` with
  `focus event N returned` per boot in the manager's log (a boot starts where
  the time goes back).
