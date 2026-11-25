<!-- SPDX-License-Identifier: MPL-2.0 -->
# Permissions

What an application may do to the car is what its signed package declares
(`bridge-uses`, `bridge-optional` and their policies), shown to the driver as
permissions before it installs. Approved once at install and again for what
each update adds; never changed in between. Any signed package may ask for
any permission: the driver decides.

`include/nbt/permissions.h` derives them; the car's review shows them
(`src/gui/review_permissions.cpp`), STATUS carries them (PROTOCOL.md).

| Permission | From | Shown |
| --- | --- | --- |
| background | `service` | Uses |
| car data | `car-data` | Uses |
| engine data | `diag` `read` or `codes` | Uses |
| your files | `files` | Uses |
| phone network | `net` | Uses |
| phone screen | `companion` | Uses |
| MOST bus | `most` `rx` | Uses |
| clear fault codes | `diag` `clear` | Can |
| control the car | `diag` `control`: writes, input/output control, routines | Can |
| send car commands | `most` `tx` | Can |
| send any car command | `most` `tx:*.*.*`, or a reserved function | Can |
| (when parked) / (even while driving) | `diag` without / with `drive` | after Can |

The screen, controller, gamepads, status line, settings and runner are not
listed: every application with a screen uses them.

- **A new application:** "Uses: …" and "Can: …", each wrapped onto a
  second line when long.
- **An update:** "Now also uses: …" and "Now also can: …" for what it adds;
  nothing when it adds nothing.
- **Still refused, whatever a package declares:** UDS security access,
  resets, programming and transfers; MOST sends to the head unit's gateway or
  broadcast addresses, and more than 100 a second.
