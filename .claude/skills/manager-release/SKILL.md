---
name: manager-release
description: Publish a signed NBT Manager release (.nbtr) from a vX.Y.Z tag through CI, and move the application repositories' pins to the manager's release workflow when it changed. Use only when the user asks for a manager release.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Manager release

Process: `docs/manager-release.md` ("CI releases"), `.github/workflows/release.yml`.
Only when the user asks.

1. **Ready:** `main` holds what ships, its CI is green, and locally
   `nice make -j"$(nproc)" pre-commit test` and, with the QNX environment,
   `make qnx-check stack-report` pass. These use the SDK at `../sdk`, CI the
   commit in `sdk.ref`: `../sdk` must be checked out at `sdk.ref`, or
   `sdk.ref` moves to the SDK commit the kits were built and car-tested with
   (v0.3.0 failed in CI on a stale `sdk.ref`). A car-facing change not yet
   tried in the car: say so (a kit first: manager-kit).
2. **Version** `vX.Y.Z`, each part below 1000; the sequence
   `X*1000000+Y*1000+Z` must be above the last release's
   (`gh release list -R NBTFoundry/manager`). It is a pre-release while X is 0.
3. **Notes:** `release/notes/X.Y.Z.md`, committed and pushed before the tag
   (skill `release-notes`). The build fails without it.
4. **Tag and push** on `main`: `git tag -a vX.Y.Z -m "<summary>"`,
   `git push origin vX.Y.Z`. Tags are never moved or deleted.
5. **Approval:** the sign job waits on the `release` environment; the user
   approves it under the run's "Review deployments". Follow with
   `gh run watch`.
6. **Check** the published `nbt-manager-X.Y.Z.nbtr`, the public key and
   `SHA256SUMS`: download them and run
   `build/nbt-verify-release <file> release/manager-public.pem nbt-evo-dev`.
7. **Applications:** each nf-* `release.yml` pins
   `app-release.yml@<manager commit>`. When `app-release.yml` or the build
   image changed since that commit, move the pin in every application
   repository (one commit each).
8. **Report:** the release URL, its sequence, what changed for cars.
