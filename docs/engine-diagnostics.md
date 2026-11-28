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
- One tester at a time: that project serialises its workers through a loopback
  lock (UDP `127.0.0.1:42861`).
- **The gateway's Ethernet is off while the OBD port is empty.** `dwc0` is the
  head unit's OBD Ethernet (`/etc/pf_OBD_dwc0.conf`: "ext obd interface"); the
  head unit sits on it as a diagnostic server for a tester in the OBD port,
  most likely through the gateway's switch, which the Ethernet activation line
  (OBD pin 8, the resistor in an ENET cable or a tuning Wi-Fi adapter) turns
  on. With nothing plugged in, nothing answers ARP on `dwc0`: ID6 Apps issue
  #108 (M140i) measured unresolved ARP with ENET unplugged and readings back
  "as soon as ENET was connected"; their working reports all say "over wired
  ENET". Our G31 (2026-11-27/28, nothing in OBD, engine running): no gateway.
  Not our side: the packet filter passes TCP 6801 both ways on `dwc0`, and our
  head unit has the address of that project's reference car
  (169.254.199.119, gateway 169.254.21.3). Something left in the OBD port is
  not an option for owners (user, 2026-11-28): the MOST tunnel below is the
  way to the DME without it.

## The in-car Ethernet (G-series)

- On the G30 family the OBD port reaches the gateway (in the body domain
  controller, BDC) through its own 5-line Ethernet with the activation line;
  the head unit, cameras, telematics unit (TCB), RSE, SAS and ACC are on
  always-on 2-wire OABR Ethernet to the BDC (BMW G30 training manual,
  "Ethernet").
- The head unit's side (firmware NBTevo_Y21432J): SOME/IP on `vlan73`
  (head unit `160.48.199.119`, service discovery `239.192.255.251:30490`,
  `autoarpd` on `vlan73`); services from `BMW.ZGW`, `BMW.Kombi`, `BMW.CID`,
  `BMW.ATM`, `BMW.KAFAS` in `/etc/someip/services.conf`; `vlan77` the TCB
  (`160.48.199.225`, TCB `.226`/`.227`); `vlan86` the cameras. The packet
  filter passes all IPv4 on these VLANs (`pf_OBD_dwc0.conf`, `pf.conf`).
- In-car hosts follow the diagnostic address (`/opt/conn/etc/hosts`: head
  unit `.98`/`.99` = 0x62/0x63, RSE `.38`), so the gateway (0x10) would be
  `160.48.199.16`; iDrive's touch code sends to that address, and its
  diagnosis proxy has a gateway instance.
- Our G31 (manager 0.5.6, 2026-11-28, nothing in the OBD port): `vlan73` is
  `160.48.199.119/25` with neighbours `.6 .16 .33 .34 .93 .96 .97 .99`
  (`.93` = 0x5D KAFAS, `.96` = 0x60 KOMBI by the same convention); `vlan77`
  `.225/28` (TCB `.226`); `vlan80` `.185/29` (`.186`). `160.48.199.16`
  answers but refuses TCP 6801 (ECONNREFUSED): no HSFZ on the gateway's
  in-car side. `.99` takes HSFZ but does not answer a tester present to 0x10
  (no refusal either). Nobody answers HSFZ vehicle identification on the
  VLANs; on `dwc0` the packet filter stops it (EHOSTUNREACH) and
  `169.254.21.3` does not answer ARP.
- iDrive 7 (MGU) reads any control unit with nothing in the OBD port through
  its own SOME/IP tester service (`InternalTasu`, tester 0xF2;
  cauliflowermossgrape/idrive7-apps). The NBT Evo firmware has none: its
  `NbtEvoOmapDiagHUHighApp` diagnostics router is a flash proxy between
  Ethernet and MOST.

## Finding the gateway

When the address in use does not answer, at most once a minute, the host
(`src/process/diag_gateway.cpp`), for any car:

1. Logs what the head unit knows of each wired link (`dwc0` first, then the
   others; links to phones, the other processor and loopback left out): its
   address, up or down, the routers of its routes and its ARP neighbours.
2. Sends vehicle identification to each link's broadcast address, HSFZ (UDP
   6811, an empty frame with control 0x0011) and DoIP (ISO 13400-2, UDP
   13400, payload type 0x0001), and logs who answers and the address it names
   (`DIAGADR10`, DoIP logical address 0x0010 = the gateway). The packet filter
   blocks HSFZ on `dwc0` (logged as not sent).
3. Tries, in order: those that answered (the gateway's own answer first),
   host 0x10 of each link's network (not link-local) on HSFZ (TCP 6801) then
   DoIP (TCP 13400: routing activation, then diagnostic messages), the usual
   `169.254.21.3`, then each link's routers and neighbours on HSFZ.
4. The first that answers a tester present to the gateway's own address 0x10
   is the gateway; a host that does not answer there but answers a tester
   present to the engine computer (0x12, waited 2.5 s) forwards to it and is
   used too. When the tester address is refused (HSFZ 0x0040, DoIP routing
   activation 0x00), the others are tried: HSFZ `F5`, `F1`; DoIP `0x00F2`,
   `0x0E80`, `0x0E00` after `0x00F4`. The gateway found is remembered in
   `state/manager/diag-gateway` ("address:port tester", "doip" when it is)
   and tried first at the next start. Failing all, a neighbour on `dwc0` that
   takes the connection is used.

Each step is logged (`diag:` lines in the Manager log), so a car owner's log
says what that car's links hold even when nothing is found.

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

- Does the gateway take HSFZ on the in-car Ethernet (`160.48.199.16` or
  whoever answers vehicle identification on a VLAN)? The first Manager log
  with the search above says (manager after 0.5.5).
- With an ENET cable alone in the OBD port (no laptop), does the DME answer
  through `169.254.21.3:6801` or a neighbour the gateway search finds? That
  confirms the activation line as the cause on our car.
- Does the MOST tunnel answer the manager's own DSI client?
