<!-- SPDX-License-Identifier: MPL-2.0 -->
# Phone companions (capability `companion`, bridge 1.9)

An application shows its own page in the phone app: a few native widgets it
describes, and the driver's taps and choices sent back. Nothing of the
application runs on the phone. It works while the application, or its
service ([service-bridge.md](service-bridge.md)), runs. Shown before install
as the permission "phone screen" ([permissions.md](permissions.md)).

## Declaring it

```
--bridge 1.9 --bridge-optional companion
```

With a service, the service has the page; else the application.

## The page

The host creates a private file, `/dev/shmem/nbt-bridge1-<app>.page`, and
passes it as descriptor 11 with `NBT_COMPANION_FD=11`. The process writes the
whole page at offset 0 and truncates it to its length: a first line
`<sequence>` (canonical decimal, raised on each change), then one widget per
line, fields separated by tabs. At most 4080 bytes and 32 widgets.

| Widget | Fields |
| --- | --- |
| `title` | text |
| `text` | text |
| `button` | id, label |
| `toggle` | id, label, `0` or `1` |
| `choice` | id, label, value, choices (`a,b,c`, at most 12, the value among them) |
| `progress` | label, 0-100 |

Ids are 1-24 of `a-z 0-9 _ -`; texts and labels 1-120 bytes of UTF-8 without
tabs or control characters; choices 1-24 bytes each without commas. The
phone leaves out a malformed widget and shows the rest. The SDK writes pages
(`nbt::app::Companion`).

## The phone's events

The worker keeps the last 16 events in `/dev/shmem/nbt-bridge1-<app>.events`
(`NBT_COMPANION_EVENTS`), replaced whole on each: one `<sequence>\t<id>\t<value>`
line each, `value` empty for a button, `0`/`1` for a toggle, the chosen
value for a choice. The process reads the events after the last sequence it
saw; the SDK polls the file.

## Routes

| Route | Body | Reply |
| --- | --- | --- |
| `POST /v1/companion` | `app\n` | `COMPANION\tready\n` then the page, `COMPANION\toff\n` when its process does not run or has no page |
| `POST /v1/companion/event` | `app\tid\tvalue\n` | `OK\n`; 409 when its process does not run; 400 for a malformed event |

Both answer 404 for an application not installed or without `companion`.
Signed like every phone route, answered by the worker and the door (a
service runs while NBT Manager is closed); the phone app polls the page
every 2 s while it shows it.

## Files

| File | Written by | Removed |
| --- | --- | --- |
| `nbt-bridge1-<app>.page` | the process, on fd 11 (the host creates it) | by the host at the start and the end |
| `nbt-bridge1-<app>.events` | the worker or the door, replaced whole | likewise |

The worker reads a page until two reads agree, so a rewrite caught halfway
is not shown. Frame and event lines: `include/nbt/companion_wire.h`, shared
with the SDK.
