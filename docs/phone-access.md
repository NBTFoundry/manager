# Phone access (dev41)

The car accepts a phone once by comparing six digits and confirming on iDrive.
Subsequent package installs/updates and uninstalls are requested with per-car
P-256 signatures and confirmed on the car. Declared application settings apply
without a confirmation. Packages come from files. A phone can remember any
number of cars; each car accepts four phones. The app never approves an action.
The wire format is defined only in [PROTOCOL.md](../PROTOCOL.md#paired-phone-http-protocol-1-dev41).

## Implementation

- `src/crypto/ec_public_key.cpp`: bounded P-256 SEC1 point import and canonical
  DER ECDSA verification through OpenSSL, beside the release/package RSA verifier.
- `src/storage/phone_store.cpp`: four private `NBTPHONE1` records under
  `trust/phones/<key-id>.conf`, managed by path policy and atomic replacement;
  name, key and pairing epoch. Local removal revokes the next signed request.
- `src/worker/phone_auth.cpp`, `http_auth.cpp`, `phone_api.cpp`,
  `phone_actions.cpp`, `phone_routes.cpp`: single-use nonce pool, exact request
  digest, routes shared with the door, streaming upload staging and local-only
  immutable action confirmations. Browser/token routes are removed.
- `src/gui/phone_ui.cpp`, `phone_view.cpp`: waiting, Wi-Fi-only pairing code,
  six-digit confirmation, named uninstall and paired-phone management. Refusal
  is focused. The existing digest-bound review/health/rollback flow remains.
- `src/worker/door.cpp`, `src/launcher/door.cpp`: known-good verified boot
  listener, independent lifetime lock/readiness, wfd0 retry, own packet-filter
  label. App settings writes take the existing app transaction lock.
- `plugin/phone_runtime.cpp`, `phone_display.cpp`, `src/gui/phone_wake.cpp`:
  periodic CDS reads, fresh-speed rule, bounded asynchronous layer query and
  wake marker. The plugin remains STL-free and imports no heap allocator.

## Decisions and assumptions (2026-11-14)

- The user redirected implementation to milestone 1 first, then a pause. The
  mobile repo contains an Expo foundation checkpoint; App 0.1 is milestone 2.
- Open from phone ships off. Pair/transfer tests with manual opening come first.
- P-256 support on the actual car's OpenSSL remains an assumption. The QNX 6.5
  cross-build is distinct from a successful dev41 launch in the car; launcher
  rollback protects a failed manager trial.
- The safe automatic-wake context is initially the observed ID6 home screen,
  context 3 / `LT_Table_ID6L_Pad_Title`. No established navigation context was
  found. Navigation and other unknown contexts return busy/manual-open.
  Cameras/invalid contexts are refused conservatively. This tightens the plan's
  broad iDrive-screen rule until actual car evidence supports expanding it.
- Static inspection of the shipped framework confirms the three CDS add/get/
  remove signatures and the UTF-16 callback delegate. Get and subscription use
  the same callback map. Polling is needed because subscription intervals are
  debounce limits, not periodic delivery. Underlying cached-data freshness is
  unverified; reception timestamps alone cannot establish physical freshness.
- Signed requests use uncompressed SEC1 keys and DER signatures in lowercase
  hex, bounded text replies and uint64 sequences. These wire choices fill in
  details absent from the original plan and are fixed in PROTOCOL.md/vectors.
- Required Android Wi-Fi permissions were approved for the mobile phase:
  Nearby Wi-Fi on Android 13+, Fine Location restricted to Android 10-12;
  no location use or car scanning.

## Verification status

Host tests exercise the real worker/door with software phone keys, pairing and
local confirmations, signature/body tampering, replay, revoked/unknown keys,
four-phone capacity, symlink rejection, staging isolation, install rollback,
first install, settings, uninstall and the pure wake rule. Shared vectors and
signature/pair/context fuzz targets are included. See the current HANDOVER entry
for exact final commands and results.

No Android handset or in-car validation is claimed by this milestone. The
mobile signed APK, hardware key behavior, local-only Wi-Fi permissions and
TypeScript-to-worker contract test belong to milestone 2. Milestone 3 tests the
car procedure. ENET installer extraction is milestone 4; Swift is milestone 5.

The stable launcher gains --door, so an existing installation also needs that
launcher through the Windows installer/setup path before boot-time door tests.
An ordinary manager slot update alone cannot add a new mode to an older stable
launcher. Keep Open from phone off until the updated launcher/plugin and manual
phone flows are confirmed on the car.
