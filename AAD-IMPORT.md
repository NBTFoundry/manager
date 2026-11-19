# AAD adoption recipe (not implemented)

NBT-AAD's own iDrive plugin does more than launch it: reverse gear and other car data, the focus hold while Android Auto is shown, bring-to-front, status, autostart. Before it can be retired, the manager must offer these through the bridge, the documented car interface for applications designed in [docs/app-bridge.md](docs/app-bridge.md).

The existing AAD plugin launches `/opt/aa/nbt-aad`, reads `/opt/aa/nbt-aad.env`, and recognizes persistent options under `/var/opt/aa`. Several options also have manual flags under `/opt/aa`. Its firewall helper separately modifies the OEM firewall configuration. These behaviors must be considered during adoption; ordinary manager updates do not touch those locations.

An AAD release suitable for the manager must first be rebuilt and signed. It must use the managed executable and data locations, acquire the lifecycle lease, emit readiness and heartbeats, restore its display on exit, and route launches through the manager. The legacy plugin must not independently launch the managed executable. No compatibility symlinks are used.

The migration implementation should read only explicitly recognized legacy sources:

- `/opt/aa/nbt-aad.env`, interpreted according to a documented allowlist of supported configuration fields.
- The presence flags `autostart`, `usblog`, `menu-idrive`, `show-drivemode`, `kombi-nav`, `show-warnings`, and `usb-aa` under `/var/opt/aa`, and their corresponding supported manual flags under `/opt/aa`.
- The persistent AAD values `/var/opt/aa/touch-peer` and `/var/opt/aa/spp-model`, after validating their actual formats and limits against the rebuilt release.

The boot guard is operational state rather than a user setting and needs an explicit migration decision. An import must not assume that unrecognized files or arbitrary environment entries are safe to copy. Source reads need their own fixed allowlist, regular-file and ownership checks, and strict size limits.

The setup procedure should copy recognized settings into the managed data area, verify the signed executable and copied data, start the rebuilt application and retain the legacy files and recovery material until adoption succeeds. Registration changes and retirement of the old plugin or executable belong to a separate explicit setup operation. Verify that the new lifecycle handling preserves the AAD display handoff and boot-loop protection. Neither the rebuilt release nor migration fixtures have been validated yet, so this is a recipe for the remaining implementation rather than an available import command.
