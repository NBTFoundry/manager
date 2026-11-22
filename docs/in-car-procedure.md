<!-- SPDX-License-Identifier: MPL-2.0 -->
# In-car test procedure (Windows)

This procedure checks a test kit (its `VERSION.txt` names it) with the NBT
Manager phone app. Do it parked,
with a battery charger connected, and stop at the first step that does not
behave as described. Note what you saw, save the debug log (step 9) and keep
it: that is what finds the problem.

Nothing here touches iDrive's own files. Everything the kit installs can be
removed again (step 12). The installer makes the system partition writable
only while it installs or removes, then read-only again.

## What you need

- This kit, unzipped on the laptop: `NBT-Manager-Installer.exe`,
  `phone\NBT-Manager-0.1.0.apk`, `phone\test-v1.nbt` (the installer's), `phone\test-v2.nbt`, `phone\test-v3-broken.nbt`,
  `phone\test-runner-v1.nbt`, `phone\test-runner-v2.nbt`, `phone\test-runner-v3-broken.nbt`,
  `controller.conf.example`, `usb\car_inventory.sh` with `CAR-INVENTORY.md`
  (optional: collects head unit files for the bridge's car-data work),
  `VERSION.txt`, `SHA256SUMS.txt`.
- An OBD-to-ENET cable, and an Ethernet port or USB Ethernet adapter.
- Internet access on the laptop, to enable SSH (the installer uses the same
  online service as the Bavarian SSH Client). The installer has the head unit's
  address, user and root password built in; nothing else is needed.
- An Android 10+ phone with `phone\NBT-Manager-0.1.0.apk` installed (copy it to the
  phone and open it; allow installing from that source when Android asks). A newer
  APK installs over an older one and keeps the paired cars; uninstalling the app
  deletes its keys, so each car must be paired again.
- `phone\test-v2.nbt`, `phone\test-v3-broken.nbt` and the three `phone\test-runner-*.nbt` copied to the phone
  (cloud drive, email to yourself or USB), where NBT Manager can pick or receive them.
- Car parked, ignition on, engine off, battery charger connected.

## Never

- Use the manager while driving.
- Pull power or switch the car off during an installation.
- Set `release=1` in the controller settings, or **Hand back controller** to
  `on` in the manager's **Settings**, before everything else works.
- Run destructive tests (power loss, full storage). Those wait for a spare
  head unit.

## 1. Connect

1. Plug the ENET cable into the OBD port and the laptop. Leave the Windows
   Ethernet adapter on automatic addressing; after a minute it gets a
   `169.254.x.x` address.
2. Switch the ignition on and wait for iDrive to finish starting.
3. Start `NBT-Manager-Installer.exe`. Windows warns about an unknown
   publisher: **More info → Run anyway** (the kit is not code-signed). The
   address (`169.254.199.119`), port `22`, user `root` and the password are
   built in; **Connection settings** shows them if you need to change one.
   Tick **Debug log** (next to **Copy log**) to see each command and answer
   and to save the log to a file.
4. Press **Connect**. If SSH is not enabled on the head unit yet, a window
   "Enable SSH?" appears: press **Enable SSH**. The installer finds the car's
   gateway on the cable, enables SSH through the online service (its messages
   appear in the log), then connects. Above the log, the first bar shows the
   step ("Step 2 of 6: Searching for the vehicle gateway"), the second the
   step's own progress.
5. The log should end with "QNX and host processor responded" and the
   **Install** button becomes active. The head unit's key is accepted without
   a question (its fingerprint is in the log).

### With a Wi-Fi ENET adapter instead of the cable

A Wi-Fi ENET adapter (connected over its RJ45 socket) bridges, so the car's
gateway and the head unit are on the same link but on different networks: the
gateway takes an address from the adapter's DHCP server (for example in
`192.168.16.0/24`) while the head unit may keep only its link-local
`169.254.199.119`. The gateway refuses SSH; only the head unit answers it.

The installer first looks for the head unit on the adapter's
network by itself (the car's modules ask the adapter for addresses too), so try
**Connect** before setting any address. If the log says it found none, the
computer needs an address on both networks, and Windows only adds
addresses once DHCP is off. On the adapter's connection, choose **Use the
following IP address** and set:

- `169.254.1.10`, mask `255.255.0.0`, gateway empty — reaches the head unit;
- then **Advanced → IP Addresses → Add**: `192.168.16.50`, mask
  `255.255.255.0` — reaches the car's gateway, which enabling SSH needs.

With only the second one, Connect fails immediately and the installer says
which addresses to set. With only the first, SSH works but SSH cannot be
enabled. Keep the computer's own internet on another connection: the activation
service is reached over the internet, and the adapter provides none.

Record: whether SSH had to be enabled, and the log messages. If it says no
vehicle gateway answered, check the cable and the ignition. After a Wi-Fi ENET
adapter was used on the car, the car can keep the address the adapter's DHCP
server gave it (often `192.168.16.x`) and stop answering on the cable's
`169.254` network, until the lease runs out. Then either type the gateway's
address in **Connection settings** → Gateway, or give the computer's Ethernet
adapter a second, fixed address in that network (for example `192.168.16.50`,
mask `255.255.255.0`): the search asks from every address the computer has. A
Wireshark capture on the cable just after ignition-on shows the address the
car uses. If SSH was enabled
but the connection still fails, your head unit may use another address: the
Bavarian SSH Client's **Search ZGW** shows it; type it in **Connection
settings** → Address and note it.

After any failure, tick **Debug log**: the log then shows each step's command
and, when it failed, the head unit's exit status and error text (also for
steps before you ticked it), and is written to
`Documents\NBT Manager logs\installer-<time>.log` (the log says where). Send
that file, or use **Copy log**.

### From the phone instead (Wi-Fi ENET adapter)

The phone app does what the Windows installer does, over a Wi-Fi ENET adapter
and without setting any address. Keep the laptop and the cable at hand.

1. Plug the adapter into the OBD port, ignition on, and join the adapter's
   Wi-Fi in Android's Wi-Fi settings. Android says the network has no
   internet: stay connected. Keep mobile data on; enabling SSH needs it.
2. In NBT Manager: **Cars → Install on a car → Connect**. The address, user
   and password are built in (**Connection settings** shows them).
3. The app looks for the head unit on the adapter's network (the built-in
   address cannot answer over Wi-Fi). If nothing accepts SSH there, it asks
   **Enable SSH**; nothing is sent to the activation service before you press
   it. After enabling, it keeps looking for up to a minute.
4. The log should end with "QNX and host processor responded"; **Install**,
   **Update** and the tools appear below, as in the Windows installer's steps 2
   and 3. **Share the log** sends the whole log, details included.

Record: the phone and Android version, whether the head unit was found on the
adapter's network (the log names the address: "SSH answers at …"), whether
SSH had to be enabled, and the shared log. If the log says the head unit was
not found, also try the laptop over the same adapter without fixed addresses:
that tells whether the head unit takes an address from the adapter at all.

## 2. Install or update NBT Manager

**Already installed** (any earlier kit): press **Update**, not Install. The log
says "Update staged", then compares the launcher and iDrive entry with the new
release by SHA-256 and replaces only what differs, between its two remounts.
It then says either "iDrive entry replaced", and the installer asks **Restart
the head unit?** (press **Restart the head unit**: the whole system restarts;
reconnect if you need the installer again), or "iDrive entry unchanged (same checksums): no
restart needed". Then open **App Manager** and keep
it open 10 seconds: the new version runs on trial and is kept. No removal is needed;
installed applications and paired phones stay. If the log instead notes that
the launcher and iDrive entry were not updated, the manager does it itself:
open App Manager, wait about 15 seconds, and accept **Restart head unit?**
(Later is focused). Then continue at step 3.

**First installation**:

1. Press **Install**. The first bar goes through five steps (making the
   iDrive plugin folder writable, uploading the launcher, installing the
   manager and plugin, removing the staging folder, making the folder
   read-only again); the second shows the
   bytes sent, speed and time left. The log says "Manager and iDrive plugin
   installed". If it fails, the log says why; you can press **Install**
   again without reconnecting.
2. The installer asks **Restart the head unit?**: press **Restart the head
   unit** (Test tools has it for later too). The whole head unit restarts,
   as when the car is switched off and on. If it does not come back within two minutes, switch the car off
   and on again.
3. In the iDrive menu, look for **App Manager** (with the other apps).

If **Install** says "A different NBT Manager iDrive plugin is installed. Remove
NBT Manager first.", NBT Manager is already installed: use **Update** as above.

Record: did iDrive restart normally? Is the entry there? If iDrive restarts
over and over, reconnect and use **Remove NBT Manager** (step 12).

### Capture shim and gamepad

Install and Update put the capture shim on the head unit after testing it
there; while the manager or an application is shown it keeps the controller's
knob and keys from iDrive ([input-capture.md](input-capture.md)). If its tests
refuse it, the rest of the update still goes through and the installer says
"The capture shim failed its tests": save the debug log and report it.

How it works: the shim is armed for one start of iDrive at a time. 60 seconds
after iDrive starts with it, the manager arms it for the next start. If a start
with the shim does not reach 60 seconds (iDrive fails, or the car is switched
off early) the next start is without it; after two such starts in a row the
shim stays off until a new one is installed or you use **Shim on**.

1. Switch the debug log on (step 3), then **Update**.
2. Switch the car off and on, so the head unit reads the new start entry
   (**Restart the head unit** does the same). iDrive then starts
   with the shim.
3. Use iDrive for at least a minute: menu, controller, sound. Then save the
   debug log (step 9): its `capture:` lines say whether the shim was kept.
4. If iDrive misbehaves or the head unit resets itself, it comes back without
   the shim: report it with the debug log.
5. Open App Manager, turn and press the controller for a few seconds, then
   plug a USB gamepad in and try it. Save the debug log either way: its
   `gamepad:` lines show how far it got, and the system log (**Save system
   log**) shows whether the head unit dropped the pad.
6. Before a BMW firmware update, **Remove NBT Manager** first (step 12).

**Shim off** and **Shim on** (Test tools) turn the automatic shim off and on.

## 3. Debug log and test application

1. Press **Switch on** (Head unit debug log). From now on the manager, its worker and
   the iDrive entry write to the head unit's debug log.
2. Press **Install test application v1**. The log says it is installed. If it
   says the test application is already installed, uninstall it in the
   manager first (OPTION on `test`, then **Uninstall test**) and press
   it again.

## 4. First start

1. Select **App Manager** in iDrive. The NBT Manager window should appear
   within a few seconds. Its banner reads "NBT App Manager" and the version,
   with "Push to start, hold to stop; OPTION for settings" under it; the menu lists `test`,
   then **Settings**. (A paired phone sends packages while this menu shows,
   and the car shows the review.)
2. Turn the knob and tilt it. Focus should move in the manager, and nothing
   should happen in iDrive underneath.
3. Hold the push on `test` (not running) for two seconds: nothing shows, and
   letting go starts nothing. Press OPTION on `test`: "test settings" opens
   with "Changes apply at once", "Background color: blue" and "Use the
   controller: on". Push on the
   background, choose `green` and push: it now reads "Background color: green".
   Press BACK: the main menu, on `test`.
4. Push on **Settings**: the banner changes to "Settings" and shows "Debug
   log: on" (switched on in step 3), "Open from phone: off", "Invert knob:
   off", "Hand back controller: off" and **Paired phones**. Push on **Invert knob**, choose `on`, then press BACK
   and MENU to close the manager. Open it again: turning the knob now moves
   the focus the other way. Set **Invert knob** back to `off` the same way.
   Leave **Hand back controller** off. Press BACK to return to the main
   menu.
5. Press BACK on the main menu: the manager closes. Open it again and press
   MENU: the manager closes and iDrive's menu opens.
6. Open it again and leave it alone for two minutes: it closes by itself.
7. Push on `test`: "HELLO WORLD" and "TEST v1 - native" on a green background take
   the manager's place, with "BACK TO EXIT" at the bottom. Turn the knob:
   nothing moves in iDrive underneath. Press BACK: it closes and the
   manager's list comes back within a second or two (the log says
   `test ended; the manager is shown again`).
   Press BACK again to close the manager. Open the manager, push
   on `test` again, then press MENU instead of BACK: it closes too and
   iDrive's menu opens. (If the manager comes back saying "test did not
   appear", save the debug log: the `surface 1291` lines say why.)

Steps 8 to 11 try what an application can do while it keeps running in the
background. With the controller, `test` ends on BACK or MENU, so for these
steps it runs without the controller: it stays on screen until MENU puts
iDrive back, and keeps running.

8. Open the manager, press OPTION on `test` and set **Use the controller**
   to `off`. Press BACK. Push on `test`: "TEST v1 - native" shows on green, without
   "BACK TO EXIT". Press MENU: iDrive's menu returns.
9. Open the manager: within a second the menu shows `test  (Hello v1)`.
   ("(running)" alone means the status line was not read: note it.) Push on
   `test`: the same window comes back at once (it is shown, not started
   again). In the debug log (step 9) this is `host test: show requested`,
   then `host test: show forwarded` and `handing the panel to test`, with no
   second `host test: started` line. Press MENU.
10. Open the manager, press OPTION on `test` and set the background to
    `red` while it runs. Press BACK and push on `test`: the window comes
    back already red. (The shorter value checks that the settings file,
    rewritten in place, is read correctly.) Press MENU.
11. Open the manager and press OPTION on `test`: the settings now end with
    **Stop test**. Push on it: the page says "test stopped". Set **Use the
    controller** back to `on` (it applies at the next start), then press
    BACK: `test` has no label any more. Push on `test`: "TEST v1 - native" on red
    with "BACK TO EXIT". Press BACK.

If the window shows "Starting" and then closes after 45 s without reacting
to the knob, controller input does not work: save the debug log (step 9) and
look for `DSI library` or `MOST` lines. If the knob works but the wrong keys
act as MENU or BACK, see step 10.

With the debug log on, opening the manager also records the list of the car's
MOST function blocks (`MOST registry` lines). It is only read, nothing is sent
for it; the saved log (step 9) carries it.

## 5. The car's Wi-Fi code

Only the NBT Manager app talks to the car: there is no browser page, install
link or token. Do not join the car's Wi-Fi from Android's Wi-Fi settings;
the app joins it by itself, as a local-only network, and mobile data stays on.

1. In the manager choose **Settings**: after the settings come **Pair a phone**,
   **Paired phones**, **Status** and **Tools**.
2. Push on **Pair a phone**. A Wi-Fi code shows with "Scan this Wi-Fi code in NBT
   Manager", "Confirm the matching digits here" and "Closes in 60 s". The
   password is never shown as text.
3. Let it run out once: after 60 s it closes with "Timed out; no package
   received" (the same wording as an install that timed out) and the main menu
   returns.

If **Pair a phone** shows no code, the manager could not read the car's Wi-Fi
Direct group: save the debug log (step 9), its `Wi-Fi Direct: …` line says why.
While the manager is open it lets its port through the head unit's firewall (one
rule in the running filter, removed when it closes; nothing is written to the
car's files); the debug log says `firewall: TCP 8080 open on <interface>`. If the
app cannot reach the car, note the app's message and save the debug log: a
`firewall: cannot open ...` line or missing `http ...` lines show where it
stopped.

## 6. Pair the phone once

1. Open **Settings**, then **Pair a phone**. The car shows one Wi-Fi QR and a
   60-second countdown.
2. In NBT Manager on Android, press **Pair a car** and scan that code. Grant Camera
   and the Android Wi-Fi permission when requested (Nearby Wi-Fi on Android
   13+, Fine Location limited to Android 10-12). Accept Android's Connect dialog.
   The app uses a local-only network; mobile data remains available.
3. Compare the same six digits on both screens: on the car they are drawn large
   on a highlighted panel, as in the app. On the car, **No** is focused.
   Choose **Yes, pair** only when they match. The phone shows the result and
   saves the car. It shows whether its key is hardware-backed.
4. Check **Settings → Paired phones**. Remove the phone and confirm that its
   next request fails. Pair it again. No package install is approved here.

Record the phone model/Android version, Connect/permission behavior, six-digit
agreement, pairing result, revocation result and any fixed error shown by the app.

## 7. Install, reject, update, uninstall and settings

1. Keep App Manager open manually. Import `test-v2.nbt` into the phone's
   library; select the paired car and `test`. Request an update. The car shows
   its review with **Reject** focused. Reject once; both displays report it.
2. Send the package again. Select Install (the one confirmation). The phone
   follows verification, install/health and result.
3. Send `test-v3-broken.nbt`. The health check fails; the previous executable
   returns. Both displays must report the failure/rollback. Run the old test
   application from the car to confirm its behavior.
4. From the phone, change the test application's Background color to red.
   It applies without an approval screen. An undeclared choice must fail.
5. Stop the test application on the car. Request Uninstall from the phone.
   The car names the phone and application, with No focused. Reject once,
   then request it again and approve on the car. Check the result and app list.
6. The same application run by the manager's `nbt-runner`, built without the
   QNX SDP: send `test-runner-v1.nbt`. The car reviews a new application,
   `test-runner`; install it. Push on it in the manager: "HELLO WORLD" and
   "TEST v1 - runner", with "BACK TO EXIT". Press BACK.
7. Send `test-runner-v2.nbt` and install it: the health check passes and
   "TEST v2 - runner" shows when started. Send `test-runner-v3-broken.nbt`:
   the health check fails and v2 returns. Uninstall `test-runner` from the
   phone.

Do not treat the phone's local manifest preview as verified. The car verifies
the exact package and its digest before review; only the car approves it.

## 8. Boot listener and opening from the phone

**Opening from the phone is off**: the plugin answers every wake request
"off", the setting is hidden, and the app asks you to open App Manager. Only the
boot listener is checked here: with App Manager closed, **Read applications** in
the app should still list the car's applications through the door. The steps
below apply only to a build with opening from the phone enabled.


1. The door is started by the launcher that **Install** (step 2) put on the car
   with this kit. An update sent from the phone replaces only the manager, not
   the launcher. If step 2 was refused for a different plugin, do the removal it
   describes first. Finish steps 6 and 7 before switching **Open from phone** on.
2. Switch ignition off and on. iDrive and Android Auto must start normally.
   The phone should reach the door within one minute of boot.
3. While parked on iDrive's home screen, request an update. The manager should
   open through its ordinary entry path and show the package review. Android
   Auto on screen must return busy and remain visible.
4. Try a navigation screen while parked: the car answers busy (open by hand);
   only the ID6 home context opens. Record the graphical context the debug log
   shows.
5. Unknown, stale and non-zero speeds are covered by host tests; the debug log
   records the speed values of an ordinary drive. Do not send wake requests
   while driving.
6. Switch Open from phone off and confirm wake returns off. The door's signed
   application list/settings remain available; no phone can approve an install.

Record the results with the kit version and the phone and Android versions.
Stop at a failed step; open by hand.

## 8b. Update NBT Manager from the phone

The phone accepts only a manager newer than the car's, so this needs a kit newer
than the one installed. Copy that kit's `phone\NBT-Manager-<version>.nbtr` to
the phone and add it in NBT Manager's **Library**. Open App Manager on the car,
then in the app: **Applications** → **NBT Manager** (tagged Update) → **Send to
the car**. The car shows **Review update**, "NBT Manager" with both versions,
**Reject** focused: choose **Install**. The car says
**Update ready**; the app says it starts the next time App Manager opens. Close
App Manager and open it again: the new version runs on trial, then asks
**Restart head unit?** if its iDrive entry changed. A manager too old for
this path makes the app report "update NBT Manager once with the installer
first".

## 9. Save the debug log

Press **Save to this computer** (Head unit debug log). The installer writes
`Documents\NBT Manager logs\debug-<number>.log` and shows the path. Save one
after every step that went wrong, and one at the end. Lines look like
`812.345 gui[4711] screen apps -> settings`; the log never contains Wi-Fi passwords, signatures, nonces or package
contents.

## 9b. An application's log and the iDrive log, from the phone

Every application keeps its own log on the car, always: what it prints,
and a line for how it started and ended (for example `ended by signal 11
(SIGSEGV, invalid memory access)` or `stopped: no heartbeat ('R' on fd 3) for
3 s`). In the app, open the application's page and press **Show log**: the
end shows, newest line last; **Share** sends the whole log and **Clear** empties
it on the car (do that before a test, so the log holds only that run). The car's page has
**iDrive log**, the system log the installer's **Save to this computer**
(Head unit system log) gets over SSH. App Manager must be open on the car,
unless Open from phone and the boot listener are on. Send both logs when an
application crashes.

If **No log yet** appears after running an application, switch the head unit's
debug log on before starting another test. First use the normal test app:
start it, leave it with BACK, then press **Show log** on the phone. Save the
head unit's debug log immediately afterwards (step 9). It records whether
`output.log` is missing, empty, readable, or failed, and any host-side log I/O
failure. Keep App Manager open when fetching the log after an update, so the
request goes to the updated worker rather than a boot listener still running
the previous release. Do not clear the application log before saving evidence.

## 10. Controller settings (only if keys are wrong)

If only the knob turns the wrong way, use **Invert knob** in the manager's
**Settings** instead. If MENU or BACK are on the wrong keys, edit
the text under **Controller settings** (it starts with the defaults and
explains each line), press **Write controller.conf**, and open the manager
again. An invalid file is ignored and the application list says "Controller
config invalid; defaults used". **Remove controller.conf** returns to the
defaults.

## 11. Leave it installed, or not

If everything worked, you can leave NBT Manager installed. The system
partition was made read-only again after the install (and is after every
reboot); that does not affect the manager (it lives in `/var/opt`). Press **Switch off and delete** (Head unit debug log)
when you no longer need the log.

## 12. Remove everything

Press **Remove NBT Manager…** and confirm. This runs
`nbt-manager-launcher --remove-capture` first (restoring and verifying
`/opt/sys/etc/starter.cfg` from backup before deleting the shim), then deletes `/var/opt/nbt-manager`
(the manager, the test application and their data) and the plugin folder, and
asks you to restart (the installer makes `/fs/sda0` writable first and
read-only again afterwards). If `/opt/sys/etc/starter.cfg` was edited outside
the manager's preload entry, automatic restoration refuses and keeps the shim
and backups for manual SSH review so HmiMain3D is never left referencing a
deleted library. Then switch the car off and on. App Manager and the
preload shim are gone.

## Troubleshooting

| What you see | What to do |
| --- | --- |
| "Activation reported success, but SSH did not become reachable" | Compare the address with the Bavarian SSH Client's Search ZGW. |
| "Enabling SSH failed" | Note the messages before it; check the cable, the ignition and the laptop's internet access. The Bavarian SSH Client is the fallback. |
| "SSH authentication failed" | Check the password. |
| "Could not verify the QNX host processor" | Not an NBT-EVO as expected; stop and send the installer log (**Copy log**). |
| Install: "The head unit refused the iDrive plugin folder: …" | Stop and send the installer log with **Debug log** ticked: the text after the colon names the check. |
| Install: "refused to make the iDrive plugin folder writable" | Stop and send the installer log. |
| Install: "A different NBT Manager iDrive plugin is installed" | NBT Manager is already installed: use **Update** (step 2). |
| No **Restart head unit?** after an update | Keep App Manager open 15 s after it opens; save the debug log: `setup:` lines say what failed (for example the remount of `/fs/sda0`). |
| No App Manager entry after restarting the head unit | Save the debug log and the installer log; reboot once more. |
| "Controller config invalid; defaults used" | Fix or remove controller.conf (step 10). |
| The app cannot reach the car | Step 5 note (firewall); save the debug log. |
| An application crashes or closes at once | Step 9b: share its log and the iDrive log from the phone. |

Send the installer log (**Copy log**) and the saved debug logs with your notes.

