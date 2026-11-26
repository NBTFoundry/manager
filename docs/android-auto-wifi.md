<!-- SPDX-License-Identifier: MPL-2.0 -->
# The car's Wi-Fi beside Android Auto

How NBT Manager shares the car's Wi-Fi with wireless Android Auto (NBT-AAD),
on the car and on the phone.

## The two networks

| Network | Interface, address | Who starts it | Android Auto uses it when |
| --- | --- | --- | --- |
| CarPlay hotspot | `uap0`, 172.16.224.1 | Connectivity, at boot (CarPlay coded) | NBT-AAD's default |
| Wi-Fi Direct group | `wfd0`, 172.16.222.1 | Connectivity at boot when Miracast is coded (`autogo`); otherwise the manager, through MOST A0.03 `SelectWLANState3` | "Use WiFi Direct Network" is ticked, or `NBT_ALT_WIFI=DIRECT` |

The manager pairs and serves phones on `wfd0`, and also on `uap0` (same ports,
the worker's and door's second listener, firewall labels `nbt-manager-uap0`
and `nbt-door-uap0`) whenever it has an address, with or without `wfd0`. The pairing code always names
`wfd0`'s group; the phone reaches `uap0` only through a connection another app
(Android Auto) made. It never switches the CarPlay hotspot (MOST A0.01 is
outside its MOST policy).

Without Miracast coded, only the CarPlay access point starts at boot; `wfd0`
starts when the manager asks for it. Android Auto uses `wfd0` whenever the group
is up and NBT-AAD is set to use it.

## Car side

- **Switching the group on.** Only when WLANState3 is 0. A group already on
  is used as it is and never switched off.
- **Switching it back off.** The manager switches the group off at the end
  of the session (Cancel, expiry, worker exit) only if it switched it on, and
  only if Android Auto does not use it. `androidAutoUsesWifiDirect()`
  (`src/worker/android_auto_wifi.cpp`) reads NBT-AAD's choice as NBT-AAD
  does:
  - `NBT_ALT_WIFI` among the first 15 settings of `/opt/aa/nbt-aad.env`
    wins. `DIRECT`, `WFD` or `P2P` means the group; any other word means the
    hotspot.
  - Otherwise `/var/opt/aa/wifi-direct` (the checkbox) or
    `/opt/aa/wifi-direct` (made by hand) means the group.
  - Otherwise, and without NBT-AAD, it is the hotspot.

  When Android Auto uses the group, the debug log says `A0.03 group left on:
  Android Auto (NBT-AAD) uses it`.
- **Packet filter.** The manager adds and removes only its own labelled rule
  (`nbt-manager`, TCP 8080 on `wfd0`) through `/dev/pf`. It never enables,
  disables or reloads the filter, so NBT-AAD's `pfctl -d` and its `pf_aa`
  rules for TCP 5277 are untouched.
- **Ports.** The manager uses 8080 and Android Auto uses 5277.
- **Radio.** A car with both CarPlay and Miracast coded runs `uap0` and
  `wfd0` together from boot, so both access points at once is a normal state
  for this chip (Marvell 8787).

## Phone side

Wireless Android Auto joins the car's Wi-Fi with a `WifiNetworkSpecifier`
request. Android's Wi-Fi service (AOSP `WifiNetworkFactory`,
`packages/modules/Wifi`) serves one such request at a time: a request from
the app on screen replaces Android Auto's (`canNewRequestOverrideExistingRequest`),
and adds the app to Android Auto's connection only when everything matches
(same interface, access point and credentials,
`handleClientModeManagerRetrieval`). In the car the app's request reconnected
the phone to the same group and cut Android Auto.

So before each action the app (`modules/car-link`, `CarNetwork.join`):

1. Uses the network it already holds for that car.
2. Otherwise uses a Wi-Fi network already up on either of the car's links
   (`SharedNetwork`): an address on the subnet of 172.16.222.1 or
   172.16.224.1, and the manager accepting a TCP connection on that address
   (worker or door; a refusal is not enough, since a manager older than the
   `uap0` listeners refuses there). It does not hold that network and makes
   no request, so Android Auto is untouched. `join` returns the car's
   address on that link and every request goes there, whatever address the
   car was paired at (same ports). `setUids` on a specifier network
   only limits which requests it satisfies and its callbacks; any app can
   list it (`getAllNetworks`) and bind sockets to it, since it is not
   restricted (netd `checkUserNetworkAccess`). A binding refused by a
   future Android fails the probe and the app joins with its own request.
3. Otherwise joins with its own request, held as before.

- **Android Auto on the group (`wfd0`) or the CarPlay hotspot (`uap0`).**
  Case 2: shared, no reconnection. On the hotspot this needs a manager with
  the `uap0` listeners; with an older one the app joins the group with its
  own request and **Android Auto loses its Wi-Fi** while the app holds it.
- **Android Auto joining after the app.** Android Auto takes the connection
  over; the app sees `lost` and drops its request at once (kept, Android
  would serve it again by taking the connection back, and they would take
  turns). The next action shares Android Auto's network.
- **Android Auto going away while shared.** The next request finds the
  network gone, reports `lost` and fails with `no-network`; the next action
  joins with the app's own request.
- **No `wfd0` group up** (Miracast not coded, no pairing session): only the
  `uap0` listeners exist, so the phone reaches the manager only while
  Android Auto holds the hotspot connection.

## Open

- Car test: Android Auto running wirelessly, then Pair a phone and an
  install from the app, once with NBT-AAD on the group and once on the
  hotspot.
