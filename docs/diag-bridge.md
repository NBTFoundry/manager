<!-- SPDX-License-Identifier: MPL-2.0 -->
# Diagnostics bridge

Capability `diag` (bridge 1.8): an application sends UDS requests to the car's
control units through its host, which is the car's one tester on the central
gateway ([engine-diagnostics.md](engine-diagnostics.md)).

## Policy

`bridge-diag=<ecu>:<class>[+<class>...][,<ecu>:...]` in the signed package
(`tools/package.py --bridge-diag`): 1-8 rules, ECU addresses as two lowercase
hex digits in ascending order, classes in this order.

| Class | Requests | |
| --- | --- | --- |
| `read` | `22` read data, `2C 01/02/03` dynamic identifiers, `3E 00` tester present | |
| `codes` | `19` read fault codes | |
| `clear` | `14` clear fault codes, `10 01/03` session | the car standing still |

Every other request is refused on the head unit: writes (`2E`), routines
(`31`), input/output control (`2F`), security access (`27`), resets (`11`),
programming and transfers. Example: `12:read+codes+clear` (the engine).
The install review on the car says when a policy holds `clear` ("Can clear
fault codes (only when parked)").

## Wire

AF_UNIX datagrams on `NBT_DIAG_FD=10` (`include/nbt/diag_wire.h`, copied into
the SDK): one request, one reply, in order.

| | Bytes |
| --- | --- |
| Request | `01`, ECU, id (u16 LE), timeout ms (u16 LE, 0: 2000, at most 10000), UDS (1-1024) |
| Reply | `81`, ECU, id, status, transport (`0` none, `1` HSFZ, `2` MOST), UDS (status 0 only) |

| Status | |
| --- | --- |
| 0 | The control unit's answer, positive or `7F` negative |
| 1 | No answer in time |
| 2 | Not allowed by the policy |
| 3 | No route: no gateway answered on its diagnostic port |
| 4 | A clear while the car is not known to stand still |
| 5 | Busy: another tester held the gateway for 2 s |
| 6 | Malformed request |

## Host

- HSFZ to `169.254.21.3:6801` (`NBT_DIAG_GATEWAY` in tests), tester `F4`; the
  gateway's alive checks are answered and "response pending" waits up to five
  times.
- A lease: `state/manager/diag.lock` and the loopback lock `127.0.0.1:42861`
  other testers on the head unit hold, held while requests come, released
  150 ms after the last one, or after 1 s when another tester waits.
- A policy with `clear` adds `driving.speedActual` to the car data the host
  asks for; a clear is sent only when it reads 0. Each clear is logged.
- Not yet: the head unit's MOST tunnel to the engine computer when the gateway
  has no route ([engine-diagnostics.md](engine-diagnostics.md)).
