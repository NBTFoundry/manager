<!-- SPDX-License-Identifier: MPL-2.0 -->
# Background services (capability `service`, bridge 1.9)

An application with a screen never runs in the background: when iDrive takes
the screen it is paused, then stopped (PROTOCOL.md, "Paused while iDrive is
shown"). What must keep working (a player, a logger, a server for the phone)
is its **service**: the same executable started a second time, as its own
process, with `NBT_ROLE=service`, no screen and no input. Any number of
services run at once.

An application without a screen (no `show`) needs no `service`: its one
process already runs in the background.

## Declaring it

```
--bridge 1.9 --bridge-uses show,... --bridge-optional service
```

`service` needs `show` (`tools/bridge.py` and the manager refuse it
otherwise). The package carries one executable; it reads `NBT_ROLE`:

| `NBT_ROLE` | Process |
| --- | --- |
| `app` | The screen process, as without `service` |
| `service` | The service: no `NBT_SURFACE`, controller, gamepad, focus hold or status line; every other granted capability (`most`, `car-data`, `net`, `files`, `diag`, `live-settings`) |

Without the grant (an older manager) there is no `NBT_ROLE` and no service:
the application runs as before.

## Its life

- **Started** when the application starts (by hand, from the phone or a
  shortcut): its host starts the service's host first, unless it runs. With
  the car (Settings › Start with the car on): only the service starts.
- **Keeps running** when the application leaves the screen, is paused or ends.
- **Stopped** with its application: holding the push on its row, the phone's
  stop, an update or an uninstall stop both. An update starts the service
  again when it was running.
- **Restarted** when it ends on its own or crashes: after 1, 5 and 30 seconds,
  three times per start; then it stays stopped (its log says so) until the
  application starts again.
- Its host: `nbt-manager --service <app>`, holding `state/<app>/service.lock`;
  the service process holds `state/<app>/service.lease`, as the application
  holds `lease`. Its output goes to `state/<app>/service.log`.
- The readiness and heartbeat contract is the application's (descriptor 3).

## Talking to its application

Both processes have the same `NBT_DATA_DIR`: a local socket there
(`<data>/service.sock`, the SDK's choice) joins them. Nothing else of the
manager is involved.

## Audio

Several processes may play at once. Until the manager mixes them (planned:
each process's audio through the manager, one output on the head unit), the
second to open the output may fail: a service that plays should own the
output and its application send it what to play.

## On the car

The GUI lists an application whose service runs alone as "Service running";
`SERVICES` (PROTOCOL.md) names them. `RUNNING` keeps listing applications only.
