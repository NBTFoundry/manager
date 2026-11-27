<!-- SPDX-License-Identifier: MPL-2.0 -->
# Engine data from the head unit

What an application can learn about the engine on an NBT Evo, and how.
Firmware NBTevo_Y21432J unless noted; each fact says where it comes from.

## iDrive car data (available: `car-data`, bridge 1.6)

`engine.RPMSpeed`, `engine.temperature`, `engine.torque`, `engine.consumption`,
`engine.status`, `driving.acceleratorPedal`, `driving.gear`, `driving.mode`,
`driving.speedActual` and the rest of [car-data-properties.tsv](car-data-properties.tsv).
No boost, mixture, ignition timing or fault codes. nf-gauges uses them when the
engine computer (DME) does not answer.

## UDS through the central gateway (HSFZ)

- The head unit's link to the gateway is `dwc0`, a DHCP client whose host name
  starts `DIAGADR62`: the head unit is diagnostic address 0x62 (car inventory,
  `pidin arg`).
- `EthernetDiagnosticsSlaveHU` and `NbtEvoOmapDiagHUHighApp` are the head unit's
  own diagnostic servers (its IP set-up, ARP, ping); neither sends requests to
  other control units (firmware, strings).
- `/etc/pf_OBD_dwc0.conf` blocks `dwc0` except listed ports. TCP 6801 and 6811
  pass both ways (`from any to any port 6801`); UDP 6811 does not, so HSFZ
  vehicle identification by UDP broadcast is blocked on `dwc0`, while a TCP
  connection to the gateway's port 6801 passes (firmware).
- Gateway address `169.254.21.3:6801`, tester 0xF4, DME 0x12, alive check
  0x0012 answered by 0x0013: from a third-party implementation (MIT,
  NBTApps/ID6_apps_prerelease) reported working by owners of several cars. Some
  cars need another address (that project keeps a configurable one), and some
  have no route to the gateway at all. On our car (G31, 0.5.3, 2026-11-27)
  nothing answers there: `diag: no gateway answers on its diagnostic port`,
  and Gauges shows "NBT Manager reaches no gateway".
- When that address does not answer, the host looks for the gateway
  (`src/process/diag_gateway.cpp`): it logs what the head unit knows of
  `dwc0` (its address, up or down, the routers of its routes and its ARP
  neighbours: the DHCP server that gave the address is one), then tries each
  of them on port 6801. One that answers a tester present to the gateway's
  own address 0x10 is the gateway from then on; failing that, the one that
  takes a connection. Not car-tested: the log of the first run says what
  that link holds.
- One tester at a time: that project serialises its workers through a loopback
  lock (UDP `127.0.0.1:42861`).

## UDS through the head unit's MOST tunnel

- Through `DSYSMOSTServer`: FBlock 0x06 (Diagnosis), instance 0xF0, function
  0x050, routine 0x0F0B, diagnostic source 0x63, after registering target key
  0x06F0. The DME (0x12) only; requests up to 16 bytes, replies up to 512; an
  outer `7F 31` means no route; a pending reply waits up to 5 s.
- Reported working on a 2018 320i (N20) for DME `22` reads with nothing in the
  OBD port; `19`, `14` and `2F` untested there (same project's documentation).
  Not confirmed on our car.
- ID6 Apps (MIT, `source/docs/MOST.md`) waits at least 2 s for a reply; its
  long-running readers switch to the tunnel after 5 failed HSFZ connects in a
  row, never once HSFZ has connected, and share the tunnel through the same
  loopback lock (`127.0.0.1:42861`) as HSFZ. A gateway other than
  `169.254.21.3` is set in `/var/opt/hmi/data/ID5/gateway-ip.txt`, which the
  manager's `diag` and nf-gauges read too.

## Open questions for the car

- Does `dwc0` reach a gateway at all on our car? `169.254.21.3:6801` does
  not answer. The next Manager log has `diag: gateway ... did not answer
  (errno N); dwc0 is <address>/<mask> up|down, neighbours ...` and what each
  neighbour did: with no neighbour and no answer, the MOST tunnel below is
  the way left.
- Does the MOST tunnel answer the manager's own DSI client?
