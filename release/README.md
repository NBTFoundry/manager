# Release keys (public halves)

- `manager-public.pem`: verifies manager releases. A car trusts it once the desktop
  installer provisions it (`trust/manager.pem`).
- `apps-public.pem`: the apps key, compiled into the worker; every application package
  must be signed with its private half.

These are the current test keys (the same as the in-car test kits'). To rotate one,
replace the file here and the matching `release` environment secrets in the same
change; the sign jobs refuse a key that is not this file's pair. A car only accepts a manager signed with the key it was provisioned with, so a
new manager key means reinstalling with the desktop installer.

The private halves never enter this repository. The manager key lives in this
repository's `release` environment secret `MANAGER_SIGNING_KEY`, the apps key in
each application repository's `release` environment secret `APPS_SIGNING_KEY`
(both base64 of the PEM). Only jobs for `v*` tags get them, after a maintainer
approves the run, and only admins can create those tags.

The release workflow uses compatibility token `nbt-evo-dev`, which the test kits
provision too.
