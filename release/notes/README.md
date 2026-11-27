# Release notes

One file per release, `X.Y.Z.md`, written before the tag. The release workflows
refuse a tag without it and put it at the top of the GitHub release, above the
lines about the signature and the build. Application repositories keep theirs in
the same place, `release/notes/` next to `package.args`
(`.github/workflows/app-release.yml`).

Write for a car owner: what is new, what is fixed, what to know before updating
(a head unit restart asked, the NBT Manager version an application needs,
something not yet tried in a car). Headings `### New`, `### Improved`,
`### Fixed`, `### Changed`, `### Good to know`, only those with something under
them; one line per bullet, no commit names or file names.
