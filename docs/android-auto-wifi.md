# The car's Wi-Fi beside Android Auto

How NBT Manager shares the car's Wi-Fi with wireless Android Auto (NBT-AAD),
on the car and on the phone. Written 2026-11-16, kit dev68. Nothing here is
car-verified yet.

## The two networks

| Network | Interface, address | Who starts it | Android Auto uses it when |
| --- | --- | --- | --- |
| CarPlay hotspot | `uap0`, 172.16.224.1 | Connectivity, at boot (CarPlay coded) | NBT-AAD's default |
| Wi-Fi Direct group | `wfd0`, 172.16.222.1 | Connectivity at boot when Miracast is coded (`autogo`); otherwise the manager, through MOST A0.03 `SelectWLANState3` | "Use WiFi Direct Network" is ticked, or `NBT_ALT_WIFI=DIRECT` |

The manager pairs and serves phones on `wfd0` only. It never switches the
CarPlay hotspot (MOST A0.01 is outside its MOST policy).

dev66's system log (`slog-1794848666.log`) shows one access point started at
boot (16 s, with CarPlay) and `wfd0` started only when the manager asked
(110 s). After the iDrive reset, the group no longer starts by itself on the
user's car. dev32 to dev45 show that Android Auto used `wfd0` on that car
before the reset.

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
  for this chip (Marvell 8787). Whether starting `wfd0` disturbs an Android
  Auto session already running on `uap0` is untested.

## Phone side

The app joins with a `WifiNetworkSpecifier` request. Android's Wi-Fi service
(AOSP `WifiNetworkFactory`, `packages/modules/Wifi`) serves one such request
at a time, and wireless Android Auto uses the same mechanism: AOSP has a
special case for "car mode prioritized" apps in it.

- **Android Auto on the same group (`wfd0`), Android 12 and later.** The
  app's request matches the connected network (SSID, access point and
  passphrase), so Android adds the app to that connection without
  reconnecting (`handleClientModeManagerRetrieval`, "Already connected to the
  same network"). Android Auto is untouched. When the app releases its
  request, Android ignores it, because the connection still belongs to
  Android Auto's request (`releaseNetworkFor`).
- **Android Auto joining after the app.** Android Auto, as a car-mode app,
  reconnects instead of sharing and becomes the connection's owner. The app
  sees `lost` and joins again for its next action, then shares.
- **Android Auto on the CarPlay hotspot.** The app's request is for another
  network. A request from the app on screen replaces the one connected
  (`canNewRequestOverrideExistingRequest`), so **Android Auto loses its Wi-Fi**
  while the app holds the car's group. It comes back after the app releases
  the group (background, 5 minutes idle, or the car forgotten).
- **Android 10 and 11.** No sharing: the app's join reconnects even to the
  same network, which drops Android Auto's connection for a moment.

Another app cannot use Android Auto's connection directly: from Android 12
a specifier network is limited to the apps that requested it
(`ClientModeImpl`, `setUids`).

## Open

- Car test: Android Auto running wirelessly, then Pair a phone and an
  install from the app, once with NBT-AAD on the group and once on the
  hotspot.
- If Android Auto must stay on the CarPlay hotspot, the manager could serve
  phones on `uap0` too, with the hotspot's details (`iap2.cfg`) in the pairing
  code, so the phone shares that connection instead. That needs a decision:
  it puts the CarPlay hotspot's passphrase on the phone, and dev32's link on
  `uap0` timed out from a phone on the group.
