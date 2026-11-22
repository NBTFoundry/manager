<!-- SPDX-License-Identifier: MPL-2.0 -->
# Tools: restarting the head unit, enabling SSH

The manager's Settings end with **Tools**, two actions the driver can choose on
the car's own screen (`src/gui/tools_ui.cpp`). Both ask before they act and
come back to the menu.

## Restart head unit

The manager's existing `IDRIVE RESTART`: `nbt_system_restart_idrive()` runs
`OnOffDSICommander appreset`, which restarts the whole head unit, as switching
the car off and on does — none of the tool's other modes restarts iDrive alone
([hmi-restart.md](hmi-restart.md)). The manager restarts with it and SSH, if it
was enabled, goes down with it. The same question follows a manager update that
replaced the iDrive plugin; there the driver is asked first as well.

## Enable SSH

SSH, telnet and qconn are enabled by the head unit's own process manager, which
runs everything in `/opt/sys/etc/starter.cfg`. **Package 0 is `OPT_DEBUG`**, its
configured state `STOP`, and it hosts (NBTevo_Y21432J, the firmware in the car's
dump):

| Process | Program | What it gives |
| --- | --- | --- |
| 91 | `/bin/devc-pty` | pseudo terminals for sshd |
| 92 | `/bin/inetd /etc/inetd.conf` | the services `/etc/inetd.conf` lists: `telnet … /bin/telnetd` and `ssh … /bin/sshd sshd -i`, both as **root** |
| 95 | `/bin/qconn` | the QNX remote IDE connection |
| 96 | `/opt/sys/bin/qnxcommd` | the QNX communication daemon |
| 72 | `/opt/sys/bin/srm` | the removable media service |

A package request is a `devctl` on the process manager's `/dev/starter/start`
node, command `0x800c050f`, with `{uint32 domain, uint32 package, uint32
command}` and `RUN` = 2 — the interface the launcher's `--restart-hmi` already
uses for package 0.14 (`src/launcher/hmi_restart.cpp`). The worker opens that
node, requests `RUN` for domain 0, package 0, and closes it
(`nbt_system_enable_ssh`, `src/worker/system_tools.cpp`).

**Nothing is written, on any filesystem.** Not `/fs/sda0`, not `/var/opt`, not
`/dev/shmem`: the request only starts processes that the running process manager
already knows. Because `OPT_DEBUG`'s configured state stays `STOP`, a restart of
the head unit closes SSH, telnet and qconn again. That matches the commercial
SSH activation service, whose `ssh_enable` job also lasts until a reboot
(ssh.bavarian-aa.uk); a persistent unlock instead rewrites `starter.cfg`, which
the manager never does.

The head unit's address, user and root password are the published defaults the
desktop installer also carries, so the menu shows no credentials; the
confirmation says instead that SSH, telnet and qconn open as root to the car's
network, which is why it is asked. `/etc` is not a mounted filesystem on this
firmware (the car's `mount` lists none), so `/etc/inetd.conf` comes from the
boot image: starting the services installs nothing.

## What only the car can confirm

- That the running process manager accepts `RUN` for package 0: the services
  appear in `pidin arg` (`/bin/inetd /etc/inetd.conf`, `sshd -i`, `/bin/qconn`)
  and SSH and telnet accept the published password as root.
- That nothing was written: `mount` still shows `/fs/sda0` read-only and no file
  under `/var/opt/nbt-manager` changed (`ls -l` before and after).
- That a restart closes it again: after the restart `pidin arg` has no `sshd`
  and Tools can enable it again.
- If the request is refused (`ERR ssh`), the fallback considered is the worker
  starting `/bin/inetd /etc/inetd.conf` itself, which writes nothing either; it
  is not implemented pending that result.
