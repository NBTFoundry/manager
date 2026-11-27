---
name: release-notes
description: Write the notes of a release for car owners - NBT Manager, an nf-* application, the SDK, the installer - where the file lives, what it says, how it reaches the GitHub release, and how to add or correct the notes of a release already published. Use before every release tag and whenever the user asks for release notes.
---
<!-- SPDX-License-Identifier: MPL-2.0 -->
# Release notes

Every release says what changed, for a car owner. Wording rules and headings:
manager `release/notes/README.md`.

| Release | Notes file | Reaches the release |
| --- | --- | --- |
| NBT Manager | manager `release/notes/X.Y.Z.md` | `release.yml`: the build fails without it |
| An application (nf-*, a repository from `template`) | its `release/notes/X.Y.Z.md`, next to `package.args` (`examples`: `<folder>/release/notes/`) | `app-release.yml`: the build fails without it |
| `community-apps` | the developer's repository, same path, at the listed commit | used when present |
| SDK, installer, toolchains | none | by hand, right after publishing (below) |

1. **Collect** since the last tag: `git log --format='%h %s%n%b' <last tag>..HEAD`.
   For an application also what moved under it: upstream's changes when the
   submodule pin moved, and an SDK change its users notice (`release/sdk.ref`).
   What a car confirmed: the user's reports, the roadmap.
2. **Write** the file: only what an owner sees or must know. No commit
   subjects, file names or internal words. `### Good to know` carries what
   to expect: a head unit restart asked, the NBT Manager version an
   application needs, what has not run in a car yet, a version number that
   was skipped. A release that only moves a pin still says what that gives.
3. **Commit and push** it before the tag: the tag's commit must hold it.
4. **After publishing**, read the release (`gh release view <tag> -R <repo>`):
   the notes on top, the generated lines (sequence, signature, build) under a
   rule.

**A release already published** (missing or wrong notes, or a repository
without a notes file): keep the generated lines as they are.

```sh
gh release view <tag> -R <repo> --json body --jq .body >old.md   # outside the repository
{ cat notes.md; printf '\n---\n\n'; cat old.md; } >new.md       # old.md without earlier notes
gh release edit <tag> -R <repo> --notes-file new.md
```

Add the same notes file to the repository when it keeps them, so both agree.
Never move or recreate a tag for notes. Older releases only when the user asks.
