# Independent NBT Evo application updater: project brief

This document is a starting point for a **separate, reusable updater project** for user-installed applications on BMW NBT Evo iDrive 6 head units. It summarizes the evidence and design work available on 9 November 2026. It is not a finished implementation or a statement that the proposed updater works on a car.

## Goal and intended experience

Provide an iDrive menu entry for an independent updater that can install signed application updates from a phone or laptop over the head unit's existing Wi-Fi. It should work while the application being updated is stopped. No dedicated mobile app or internet connection should be required.

The intended flow is:

1. Open the updater in iDrive and start a temporary update session.
2. Connect a phone to the car's accessible Wi-Fi network. Scan a QR code on the car screen to open a temporary local URL in the phone's browser.
3. Select an update file. Upload it to the head unit; the updater verifies its signature, integrity and compatibility.
4. Show the application identity, installed and proposed versions, and verification result. Confirm installation on the head unit.
5. Stop the target application if needed, stage and install the files, preserve a recoverable previous version and report the result. If a reboot is needed, ask the user to perform it manually.

The same design should support applications other than Android Auto. Application identifiers, target files, trust keys, compatibility constraints, stop/start procedures and health checks must come from trusted local application profiles, not be arbitrary instructions supplied by the uploaded package. A first implementation can support a single executable per update; plugin updates and multi-file transactions can follow once recovery is proven.

## Platform and existing knowledge

The target is a BMW NBT Evo ID6 head unit, including the Toyota GR Supra J29. The relevant target environment is QNX Neutrino 6.5, ARMv7 little-endian. Variants and software releases can differ; check the actual unit rather than assume a path, privilege or interface is universal.

The existing application project, [Supra-iDrive6-Android-Auto](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto), demonstrates a native iDrive `PluginApp`:

- The iDrive app is a shared library loaded into `HmiMain3D`, with a manifest, menu XML, text database and image database. The project's `milestone-7/rhmiapp/` directory is a practical reference for the PluginApp entry point, action registration, models and menu resources.
- The plugin's manifest points to its library and XML/resource files. Menu navigation can be specified in XML, while actions that execute code require handlers registered by the library. The project has not established that one PluginApp's menu can directly dispatch actions to a second independent PluginApp.
- A separate updater would have its own library, manifest, menu and identifiers. Installing and loading two independent PluginApps together, entry placement, identifier collisions and behavior across iDrive versions **have not been verified**. A plugin is independent of the target application's executable, but it still shares the HMI process and cannot recover an HMI that fails to run.
- Existing menu images are prebuilt PNGs in an image database. A small QR encoder can generate an image, but displaying a **runtime-generated** image in the iDrive framework is not yet demonstrated. Investigate the framework or a separate display approach; provide a usable text URL and code if QR display is unavailable.
- Existing project code uses QNX `spawn`, a local UDP control interface, a state file, timeouts and process checks to manage its own daemon. These are examples, not a generic lifecycle API: each supported application needs its own trusted adapter. Avoid duplicate starts and preserve application-specific cleanup.

## Networking and session security

The existing project documents a CarPlay access point `uap0`, commonly `172.16.224.1`, and a Wi-Fi Direct group `wfd0`, commonly `172.16.222.1`. These addresses, group availability, credentials, DHCP behavior and whether a phone or laptop can join are **device-specific and require validation**. The group can be used as Wi-Fi by a suitable client; Wi-Fi Direct does not itself require a dedicated companion app.

The head unit's packet filter can block incoming connections. The existing project has a car-tested, narrowly scoped rule for its own TCP port; that is not a rule for an updater. Inspect the actual firewall configuration, add only the updater port on the required interface/subnet if necessary, and test with the target application stopped. Do not rely on disabling the packet filter. Check behavior if the access point is inactive without the target app, and fail clearly if networking is unavailable.

Use a temporary, cryptographically random, high-entropy bearer token in the QR URL. Start the listener only when the user opens a session; limit its lifetime, concurrent requests and upload size; invalidate the token on completion, cancellation or timeout. A person who sees the QR code can use the session. Keep the token out of logs, browser referrers and external content; use no third-party page resources. Restrict request origins/hosts and protect state-changing operations against cross-site requests. Authenticate every upload and action, not just the initial page load.

Local HTTP over the car's WPA-protected Wi-Fi does not provide end-to-end TLS: another authorized network participant may observe or alter traffic depending on the network. A signed package prevents unauthorized payload modifications but does not by itself authenticate the displayed web page, protect session tokens, or prevent denial of service. Confirm the verified package and installation in iDrive. Evaluate usable local HTTPS or another authenticated transport before claiming protection against a hostile network peer. Never expose an unauthenticated shell, arbitrary file writes or generic command execution.

## Package and trust model

Use one portable signed update file with a versioned format. The signed metadata should identify the application, package and version, target platform/compatibility, allowed files, lengths and cryptographic hashes. Authenticate the exact metadata bytes, then hash the actual file bytes. Reject invalid signatures, unknown keys or applications, incompatible targets, malformed or duplicate fields, oversized or incomplete input, unlisted files, paths outside the trusted application profile, links and unexpected archive entry types. Enforce bounds before allocating or unpacking; never execute installer scripts supplied by a package.

The updater must obtain trusted public keys from its installed configuration, never from the same untrusted package. Each application profile should specify its authorized signer(s) and fixed installation destinations. Keep private signing keys on the developer's computer or a secure signing service, never in the repository or on the head unit. Define a controlled key-rotation process before distributing broadly. A checksum alone detects accidental corruption only if obtained from a trusted source; a signature authenticates the signer and integrity, not correctness or safety of signed code. Decide downgrade/replay policy explicitly and support a deliberate recovery path for an older trusted version.

An early **application-specific prototype**, not a complete updater or a proposed generic standard, exists on branch [`feature/signed-daemon-updates`](https://github.com/fpoussin/Supra-iDrive6-Android-Auto/tree/feature/signed-daemon-updates):

- `tools/package_ota.py` creates a single-executable package with a signed manifest and SHA-256 digest.
- `milestone-7/rhmiapp/update_package.hpp` verifies it using a caller-supplied RSA public key and checks basic ARM ELF fields.
- `milestone-7/tests/test_update_package.py` exercises valid, tampered, truncated and wrong-key packages. It passed on the host; the verifier was also compiled and linked with the local QNX toolchain. Neither the prototype nor an OTA install was tested on the car.

The prototype's manifest is named for its original application and lacks the generic application identity, destination policy and compatibility metadata above. It has no web server, key provisioning, car integration or installer. Assess and adapt it, including its license and required notices, before reusing code in another project.

## Installation and recovery

Treat the verified package as untrusted until all checks complete and the user confirms. Keep uploads in a bounded staging area with restrictive permissions; recheck the staged bytes and bind confirmation to the exact verified package. Prevent another session from replacing those bytes between verification and installation.

Stop the target using its registered adapter, verify it actually exited, and prevent concurrent start/stop/update operations. The reference project warns against copying over binaries that are executing or libraries loaded by `HmiMain3D`; rename old files aside and install staged files rather than overwrite an in-use inode. Confirm filesystem boundaries before relying on `rename`: atomicity only applies to a single rename on the same filesystem. A sequence of file replacements is not a single atomic transaction.

Keep the known-good version and application settings outside locations that iDrive scans as plugin directories. Check available space and every write, flush and rename. Keep a durable transaction record that can distinguish prepared, backed-up, installed and confirmed states; on restart, recover an interrupted transaction deterministically. Test power loss at every step. Do not overwrite the only known-good backup on a repeated or failed upload. Define precisely when a new version is deemed healthy and when the previous one may be replaced. Ensure the updater's own code and recovery mechanism are not replaced in a way that prevents recovery.

The application partition may be read-only. One tester of the [USB update proposal, PR #39](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/pull/39), reported that remounting `/fs/sda0` writable from the HMI process, stopping the daemon, staging files, moving the previous plugin outside the scanned plugin directory and resetting the HMI worked on their unit. This is a useful observation, not proof for all units or for this design. The PR's own discussion notes concerns about verification and losing the backup on a repeated update. It is not a finished generic updater.

Retain a documented SSH/ENET recovery procedure throughout testing. If the updater plugin cannot load, the HMI fails, networking is unavailable, the filesystem cannot be written or its own update fails, wireless recovery cannot be assumed. Plugin updates and larger system changes should initially remain manual USB/SSH operations. Do not update stock vehicle software or modify unrelated files.

## Boot and availability

A separate iDrive plugin can request autostart through its manifest, but whether it loads automatically and independently of another failing plugin must be tested. Even if it does, it depends on `HmiMain3D`. A manager independent of the HMI would require a separate, validated boot integration; do not assume that this is necessary for the first updater.

Published [NBT Evo boot research](https://www.hakstuff.net/blog/dissecting-the-bmw-nbt-hu-evo-infotainment-unit-boot-process) describes a service starter reading `/opt/sys/etc/starter.cfg` and other domain configurations. The actual unit's active configuration, dependencies and recovery behavior have not been inspected. A separate [SSH unlock project](https://github.com/Xayanix/nbt-evo-ssh-unlocker) warns that starter configurations differ across software families and that a mismatch can prevent booting. Do not modify a boot path without a reversible, version-specific method and a proven recovery procedure.

## Development environment

A Linux-hosted cross-toolchain is available locally at `/home/mobyfab/src/tools/qnx-6.5-tools/`. Its README documents Git LFS setup and its provenance. It combines a QNX 6.5 ARM sysroot, GCC 4.8.3 target profiles, a newer ARM assembler and an existing QNX 6.6 `qcc` driver; it is **not** a stock 6.5 SDK. The compiler requires a Linux x86 host with 32-bit runtime support. An SDK 6.5 VM with GCC 4.4.2 is also available as a reference. Respect QNX and third-party licensing when moving or distributing toolchain components.

The current manager and applications use C++11 with the GNU `_gpp` profile and dynamic `libstdc++.so.6`. The reference project was built with the assembled toolchain; compare ELF architecture, interpreter, library dependencies and ABI against a known-good 6.5 build, but do not mistake matching names for runtime compatibility. The reference native plugin uses the GNU C++ runtime (`gcc_ntoarmv7le_gpp`); its daemon uses the QNX C++ runtime (`gcc_ntoarmv7le_cpp`). The available SDK headers report OpenSSL 0.9.8m; verify the actual head unit's crypto library ABI and supported algorithms before selecting dependencies. Avoid assuming newer OpenSSL APIs are available.

A BeagleBone Black with an LCD is available for general QNX testing, but it does not reproduce the head unit's BMW HMI framework, boot service, firewall or flash layout. The user's car runs a working release of the reference application and is the eventual integration target.

## Suggested implementation and acceptance sequence

1. Create a separate repository with its own identity, licensing and clean-room boundaries as appropriate. Inspect the reference project's license and notices before copying code or resources.
2. Build a minimal independent PluginApp with a distinct manifest, identifiers and menu entry. Prove it coexists with the existing app and starts when the target app is stopped. Verify plugin lifecycle and recovery without changing boot configuration.
3. Prove the UI can show a temporary session address and investigate runtime QR display. Verify phone and laptop access, firewall behavior and the no-target-app case; do not expose file installation yet.
4. Implement generic signed-package verification and application profiles. Test good and bad signatures, wrong keys and apps, incompatible versions/targets, tampering, truncation, malformed metadata, bounds and malicious paths. Verify target crypto linkage.
5. Implement a single-executable installer with application-specific stop/health adapters, verified staging, durable recovery and known-good backup. Test interruption at each stage and repeated installs on an isolated filesystem before touching the car.
6. Add the responsive upload page, temporary authenticated session and in-car confirmation. Test session expiry, concurrent requests, request tampering, interrupted uploads and browser behavior.
7. With SSH/ENET recovery available, validate remount permissions and all operations on the actual unit. Exercise a signed update, manual reboot, rollback and recovery after the target app is stopped. Record the unit version and results; expand to more applications and multi-file updates only after this works.
