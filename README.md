# NBT Manager

NBT Manager is a work in progress for managing signed applications on QNX 6.5 ARM head units. The repository contains the worker, host-side tools, an experimental iDrive plugin, launcher and manager preview; it is **not ready for installation on a vehicle**. Controller input for the manager GUI is implemented over MOST but unvalidated, and AAD adoption still needs to be implemented and the target behavior requires hardware validation.

Two keys sign everything: the manager key signs manager releases only, and one apps key signs every application package, present and future. The apps public key is bundled with the manager (compiled into its binaries from the signing machine's keys directory, `APPS_PUBLIC_KEY`, so the manager release's signature covers it and a manager update can replace it; `src/package/apps_key.cpp`). The worker verifies an RSA-3072/SHA-256 package against that key; an installed application's profile fixes its compatibility id, and a package for an application not installed here installs it, after local approval, with its own compatibility id. A package contains one executable and no destination path. The destination is selected by the application id under the path policy. A private control channel handles lifecycle commands and digest-bound approval. Paired phones use P-256 signed requests to upload packages, read state, request uninstall and change declared application settings; only the car approves pairing, installs and uninstall. The manager GUI controller implements local review and digest-bound approval; it reads the iDrive controller over MOST, which is unvalidated on hardware (see [docs/controller-input.md](docs/controller-input.md)). After replacement, the worker requires readiness and 10 seconds of heartbeats before confirmation, and attempts recovery of the prior executable if the installation is interrupted or unhealthy.

The native test application renders "Hello world" on its configured background using Screen and a bounded bitmap renderer; signed v1/v2 releases exercise updates. The project does not distribute the head unit's Screen or BMW framework libraries.

The [application bridge](docs/app-bridge.md) now has signed version and
capability declarations, install/manager-update dependency checks and grants
passed at launch. Its version is **1.2 with `show,status,live-settings,most`**: selecting a running
application requests that it come to the front when it declares that capability;
otherwise the manager stays open and reports it is already running. Apps granted
`status` can publish a short live label in the menu. Apps get a persistent data
directory; those granted `live-settings` can refresh up to 12 declared choices
from a bounded settings snapshot. Apps granted `most` exchange MOST messages
through their host, limited to the functions their signed package lists
([MOST bridge](docs/most-bridge.md); host-tested only). The list is enforced by
the manager, not a sandbox: applications run as the same user. The new dev41 phone access and boot listener still require in-car validation; see [phone access](docs/phone-access.md).

## Build and host verification

`make` (or `make release`) builds the host tools, optimized QNX worker, launcher,
manager GUI, both test applications, iDrive plugin/resources, signed manager
bundle and Rust desktop installer in release mode. Source your QNX environment
first and supply local target libraries and a manager-specific RSA-3072 key pair:

```sh
make release \
  QNX_SCREEN_LIBDIR=/path/to/qnx/target/libraries \
  RHMI_LIBDIR=/path/to/framework/libraries \
  MANAGER_VERSION=v1 MANAGER_SEQUENCE=1 MANAGER_COMPAT=your-target-id \
  MANAGER_SIGNING_KEY=/secure/manager-private.pem \
  MANAGER_PUBLIC_KEY=/secure/manager-public.pem
```

Choose the compatibility ID for the provisioned target and increase the sequence
for each release. Private keys remain on the signing workstation. The build
checks the key pair and verifies the signed bundle before embedding it with its
public key. Outputs are `dist/manager.nbtr` and
`dist/nbt-manager-installer` (`.exe` on Windows). Each host builds
its own desktop executable; this does not cross-build all desktop platforms.
The installer remains experimental until bench validation.

Use `make host` for host tools alone. `make test` remains independent of QNX and
signing keys. `make desktop-release` rebuilds the desktop installer from an
existing `DIST_DIR/manager.nbtr`, using the same version, sequence, compatibility
and public-key variables. `BUILD_DIR` selects the directory for intermediate
build output (objects, test programs, staging; `make clean` removes it) and
`DIST_DIR` (default `dist`) the one for files ready to ship, which `make clean`
keeps;
`DESKTOP_TARGET_DIR` can select a shared Cargo cache/output directory.
`CC` selects the native host C compiler (default `gcc`) for LVGL; `CXX` selects
the C++ compiler (default `g++`). `QCC` selects the QNX compiler driver. The
desktop build also uses `CC` as its native linker unless Cargo's host linker is
explicitly configured in the environment.

Host requirements are a C++11 compiler, a C99 compiler for LVGL, OpenSSL
development libraries, Python 3 and the `openssl` command. Clang is optional for
additional fuzzing tooling; `make fuzz` uses the included deterministic mutation
driver under ASan/UBSan. Format checks and line limit enforcement use `clang-format`,
`cargo fmt` for the installer's Rust workspace in `desktop/`, and `pre-commit` (run
`make format` or `make pre-commit`). Always invoke make with
`nice make -j $(nproc) <targets>`. Run:

```sh
nice make -j $(nproc) test
```

The manager GUI renders with [LVGL](https://lvgl.io) 9.6, a git submodule in `third_party/lvgl` configured by `config/lvgl/lv_conf.h`. Run `git submodule update --init` after cloning. The build compiles it into a static library for the host and for QNX.

The test suite generates temporary signing keys. Optional QR reference tests compare the encoder with `segno` and scan rendered codes with `zxing-cpp` and Pillow; they are skipped when these Python packages are unavailable. Two socket tests require local socket traffic; environments that prohibit it will report them as skipped. An AddressSanitizer/UndefinedBehaviorSanitizer build can be run with:

```sh
NBT_TEST_ASAN=1 ASAN_OPTIONS=detect_leaks=0 nice make -j "$(nproc)" BUILD_DIR=build/asan \
  CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
  CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
  LDFLAGS='-fsanitize=address,undefined -no-pie' test
```

Instrumented testing builds omit the child data limit so ASan can reserve its shadow
mapping; production and ordinary host tests retain and verify the 256 MiB limit.

Run `make fuzz FUZZ_SECONDS=5` for a time-boxed parser and protocol fuzz pass.

`make run-simulator` rebuilds and opens it (`SCALE=3` to enlarge). `make simulator` builds `build/nbt-simulator`, an X11 desktop preview of the manager GUI. It runs the real GUI controller and LVGL renderer against a simulated worker with two applications (`media-player`, shown running, and `test`). The main menu lists them, then "Settings" (the manager's own settings, kept in memory); a push starts an application (the manager then closes; for `media-player` the simulator opens it again at once), and holding Enter on one opens a dialog over the menu whose bar fills, then its settings (`test` has a background colour), which end with "Stop media-player" while it runs. Starting `test` shows the test application's own screen, "Hello world" on the background colour chosen under Settings → test; Backspace/Escape (BACK) or M ends it and the manager opens again. Settings → Pair a phone shows a Wi-Fi-only QR for 60 seconds. U simulates a pairing request while that screen is open, or a package upload otherwise. Pairing shows matching digits with No focused. Use Up/Down or the mouse wheel, Enter or click to select, Backspace/Escape to go back, M for MENU and Q or Ctrl+C to quit. An optional argument sets the scale (1-4, default 2). It installs nothing and opens no network session.

To cross-compile, source the installed QNX 6.5 toolchain environment, then point to a local copy of the target's Screen library:

```sh
make qnx QNX_SCREEN_LIBDIR=/path/to/qnx/target/libraries
```

This creates `build/nbt-managerd-qnx`, `build/test-v1-qnx`, `build/test-v2-qnx`, `build/test-v3-broken-qnx`, `build/nbt-manager-launcher`, `build/nbt-manager-gui`, and a client object. Compiling does not establish runtime ABI or firmware compatibility. QNX executables use the `_gpp` profile and require the device's dynamic `libstdc++.so.6`; verify its presence and imported symbols with `make qnx-check` before bench use.

Build the experimental independent iDrive plugin and its resource bundle with the QNX 6.5 GNU C++ ABI and local copies of the head unit's framework libraries:

```sh
make qnx-plugin RHMI_LIBDIR=/path/to/framework/libraries
```

The result is `build/NbtManager/`. The plugin's single menu action starts `/var/opt/nbt-manager/manager/nbt-manager-launcher --open`. The launcher verifies a provisioned manager slot and supervises its GUI and worker. With controller input the GUI stays open until the user leaves or two minutes pass without input; otherwise it closes after 45 seconds. Repeated selections while it is active do not start another window; they bring a window that stepped aside back. While the window is shown, the plugin holds iDrive's focus on an inert state so the controller does not drive the iDrive menu underneath (see [docs/controller-input.md](docs/controller-input.md)). Controller instances and key slots can be set in `manager/controller.conf` (example: [config/controller.conf.example](config/controller.conf.example)). The launch contract, ABI assumptions and hardware checks are described in [plugin/README.md](plugin/README.md).

### Supervision and recovery

The launcher starts the worker, then the GUI, and gives each one end of their
private channel; the GUI is a client and never starts or restarts a worker. If
either process exits, hangs or breaks the protocol, the launcher stops the other
and restarts both with bounded backoff. Persisted restart
counts stop crash loops; a failing manager candidate returns to the known-good
slot. Fatal GUI signals write `manager/crash.gui`, which the launcher reports and
the next GUI acknowledges with a recovery notice. The plugin also releases its
focus hold when GUI liveness expires. These mechanisms are host-tested, including
worker hangs and crash loops, but restart timing, focus release and rollback still
require bench validation.

For bench work the GUI, worker and plugin write a debug log while `/var/opt/nbt-manager/manager/debug.log` exists (128 KiB, one rotated copy). Logging never creates it; it is switched on by the desktop installer or in the manager's Settings, where switching it off keeps the last log as `debug.log.1`. The same page sets **Invert knob** and **Hand back controller** (`controller.conf`, see "Manager settings" in [PROTOCOL.md](PROTOCOL.md)). It records screens, controller input, MOST registration, focus hold, sessions, uploads and installation states, never request signatures, nonces, Wi-Fi passphrases or package contents. The ordered hardware validation steps, with the log lines each should produce, are in [docs/bench-checklist.md](docs/bench-checklist.md).

`python3 tools/build_windows_kit.py --without-phone-app` for milestone 1 (`make windows-kit` after App 0.1 exists; QNX 6.5 environment sourced, `QNX_SCREEN_LIBDIR` and `RHMI_LIBDIR` set, Rust target `x86_64-pc-windows-gnu` and MinGW installed) builds `dist/nbt-manager-windows-kit-<version>.zip` for testing in a car from a Windows laptop: the installer with the signed manager release and test application v1 embedded, test application v2 and a deliberately failing v3 for the phone, and [docs/in-car-procedure.md](docs/in-car-procedure.md). Its signing keys are personal test keys created on first use in `~/.local/share/nbt-manager/keys` (override with `NBT_KEYS_DIR`); each build takes the next manager release sequence from there.

## Packages and offline initial bundle

Keep the private release key on the signing workstation. Build an initial package and verify it using the corresponding public key:

```sh
python3 tools/package.py --app test --compat demo-1 --version v1 --sequence 1 \
  --key release-private.pem --output test-v1.nbt build/test-v1-qnx
build/nbt-verify test-v1.nbt release-public.pem test demo-1
```

The test application publishes "Hello v1" (or "Hidden v1" after hiding without input) when granted `status`; the menu refreshes its label once a second. It declares one setting, its background colour; add it with `--setting 'background|Background color|blue|blue,red,green,black'` (see "Application settings" in [PROTOCOL.md](PROTOCOL.md)). Started from the manager's menu it shows "Hello world" on that colour and exits on BACK or MENU. Hold the push on an application in the manager's menu to open its settings (and, while it runs, stop it).

Repeat with `--version v2 --sequence 2` and `build/test-v2-qnx` for the update. Sequence numbers must increase for normal updates; an older signed release requires the explicit recovery operation.

The following command prepares a **new, offline workstation directory** containing the verified v1 executable, baseline record, app profile, public key and worker. It refuses to merge into an existing output directory. It does not provision the head unit:

```sh
python3 tools/prepare_bundle.py --app test --compat demo-1 \
  --package test-v1.nbt --public-key release-public.pem \
  --worker build/nbt-managerd-qnx --verifier build/nbt-verify \
  --output initial-bundle
```

Only a public key belongs in the application bundle. Provisioning that bundle
on the head unit remains a separate procedure; the desktop installer provisions
the manager and its plugin.

On a head unit where the manager is installed, the launcher can provision one
new application from the operator's SSH session:

```sh
/var/opt/nbt-manager/manager/nbt-manager-launcher --provision-app <app> <compat> \
  <key-bytes> <package-bytes> <package-sha256> < key-then-package
```

It reads the application's public key and its first signed package from
standard input, verifies the package against that key, the application id,
the compatibility id and the SHA-256, and installs it as the baseline
(executable, installed record, key `0`, then the profile). It refuses when
anything of that application already exists and removes what it created if it
fails, so it never replaces an installed application or its keys.

An application is uninstalled only from the manager's GUI, from its settings
(hold its name), after a confirmation that defaults to **No**. The worker's
`REMOVE <app>` takes the manager operation lock (or, under the launcher, relies
on the launcher's own hold of it, as installs do) and the application's host
lock, transaction lock and lease, so it refuses a running application or one
being updated, then removes the profile first (the application leaves the
list), its keys 0-7, `apps/<app>`, `state/<app>` and `data/<app>`. Every path
is built from the absolute managed root by the path policy; links are
unlinked, never followed, and nothing on another device is entered. A removal
cut short completes when run again. Its data is deleted with it; installing it
again starts from its first signed package.

Manager components and the plugin have a separate signed release format and
manager key; see [manager-release.md](docs/manager-release.md). The desktop
installer is in [desktop/](desktop/README.md); the whole install flow, with
what is known about the car, is in [install-flow.md](docs/install-flow.md).
Initial installation and manager
updates have host-tested recovery paths and still require head-unit validation.

## Storage and trust boundaries

Production paths are confined to the provisioned `/var/opt/nbt-manager` root. The path policy checks the fixed root path, its ownership, device, private parent directories, file type and link count. Application data, profiles, keys and manager components are outside executable update destinations. Transaction names are internally generated, and journal records cannot supply paths. The current QNX backend requires a writable executable `qnx6` filesystem and requests snapshots using `fsync`; its persistence and mount assumptions must be verified on the particular unit before use.

These checks assume the manager owns its private directories and that other processes running with the same effective UID are trusted. QNX 6.5 lacks the directory-relative primitives needed to close every pathname race against a malicious same-UID local process. Local HTTP is unencrypted; a hostile Wi-Fi peer may observe or interfere with a session. Package signatures and the local approval channel are therefore essential, but the in-car approval path is not usable until controller input is validated on a head unit.

Applications must cooperate with the lifecycle contract: take the shared lease on the supplied descriptor, send readiness and heartbeat bytes on the supplied pipe, and exit when the pipe closes. Existing AAD binaries do not satisfy this contract. Adoption requires a rebuilt release and a separately reviewed migration of recognized settings; this repository does not yet implement AAD migration or legacy retirement.

While an install session listens, the worker lets its port through the head unit's packet filter with one rule inserted at the head of the running main ruleset through `/dev/pf` (`src/worker/firewall.cpp`): `pass in quick on <uap0|wfd0> inet proto tcp to <session address> port <port> flags S/SA keep state`, labelled `nbt-manager`. It is removed when the session ends, and leftovers at the next worker start; `/etc/pf.conf` is never written and the filter is never switched on or off. The ioctl structure sizes match those used by BMW's own `Online` service on NBTevo_Y21432J; the rule itself needs the car to validate.

## Remaining work before a vehicle trial

- Validate the independent iDrive PluginApp and its resources against the exact firmware ABI. Validate MOST controller input, the controller configuration and the iDrive focus hold on a bench (see [docs/controller-input.md](docs/controller-input.md)).
- Validate initial enrollment and plugin registration on a bench; implement the AAD import procedure, including preservation of legacy settings and files.
- Extend fault injection to failed writes, flushes, renames and full storage, and review all transaction and path-policy edge cases.
- Validate Screen cleanup, process leases, Wi-Fi interfaces, executable storage, QNX6 synchronization and crash recovery on the actual unit. Perform destructive power-loss tests only on isolated storage or a bench setup.

Host tests and sanitizers reduce the chance of defects; they do not prove memory safety, target compatibility or power-loss durability.


## Signed phone access (dev41)

Pair a phone in the car once, using its Wi-Fi-only QR and a matching six-digit
confirmation with No focused. A separate P-256 device key identifies the phone
for that car. Private phone records are `trust/phones/<key-id>.conf`
(`NBTPHONE1`: name, SEC1 public key, pairing epoch), at most four, managed through
the path policy. Settings → Paired phones removes trust immediately.

The worker's signed `/v1/` API listens on wfd0:8080 while the GUI runs. The
boot-time door uses the same implementation on 8081 for nonces, application
list, settings and wake. Its packet-filter rule has label `nbt-door`, separate
from `nbt-manager`, and is refreshed after ruleset reloads. It has a separate
managed lifetime lock and never retains the manager operation lock.
Open from phone ships off; when enabled, the first wake gate allows only fresh
stationary speed and the known ID6 home context with iDrive alone on the panel.
Other contexts ask for manual opening. [PROTOCOL.md](PROTOCOL.md) defines all
wire details; [docs/phone-access.md](docs/phone-access.md) records assumptions.

App 0.1 lives in `~/src/mobile/nbt-manager` (milestone 2). The kit builder calls
its `scripts/build-release.sh --output <apk>`, passing the kit sequence as
`NBT_VERSION_CODE`, and verifies the APK certificate against
`config/android-release.sha256` in that repo. The app script must use the
dedicated external Android release key and refuse a debug signer. Until that
milestone exists, `--without-phone-app` explicitly produces a car-only kit with
an APP-STATUS notice. The APK joins all other kit files in SHA256SUMS.txt.
No cloud builds or OTA JavaScript updates are involved.

From a clean checkout, initialize LVGL, use the QNX 6.5 environment described
above, and run `nice make -j $(nproc) pre-commit test`, the sanitizer/fuzz
commands above, then `qnx-check stack-report`. The current workstation's
environment is `/home/mobyfab/src/tools/qnx-6.5-tools/env.sh` (not qnx660).
Keep build directories separate when switching toolchains.


## Wired gamepads

The optional [gamepad bridge](docs/gamepad-bridge.md) supports Xbox One GIP and
Sony DualShock 4 USB controllers, with experimental DualSense USB support.
One controller is active at a time. The manager GUI and foreground applications
share an exclusive USB ownership lease and detach before handing it over.
These implementations require the Xbox One and PS4 car validation recorded in
the gamepad contract before publishing a release.
