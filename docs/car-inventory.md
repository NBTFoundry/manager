<!-- SPDX-License-Identifier: MPL-2.0 -->
# Car inventory: copying head unit files to a USB stick

Step 1 of "Discovering more car data and interfaces" in
[bridge-design.md](bridge-design.md): copy the files that describe the head unit's
car data and interfaces to a USB stick, to study them on a computer. Nothing
on the head unit is changed; the script only reads, and writes to the stick.

The files are BMW's and come from your car: keep them on your own computer,
for interoperability work only. Never add them to this repository. Do not
share `sloginfo.txt` or `pidin-arg.txt` without reading them first: the
system log can contain phone names and Bluetooth or Wi-Fi addresses.

Left out on purpose: Wi-Fi credentials (`/var/opt/conn/WFDPass.conf`,
`WFDWps.conf`), DHCP leases, `/var/opt/sys/persistence`, the phone book,
navigation history and the certificate files (`.p12`, `.p7b`) of BMW's coded
plugins. `pidin-arg.txt` still contains the head unit's MAC address, in the
DHCP client's host name.
Without a USB stick, the installers' **Collect everything** (Test tools;
`NBTFoundry/installer` README) gets the part that changes from run to
run over SSH, as one text file: the system log, `pidin arg`, `audioconn -l`,
the process manager's overview, the mounts, and listings of the manager's
folders and `/dev/shmem`. It copies no BMW files; the stick is still needed
for those. The same caution applies before sharing it.

## Steps

The script is [tools/car_inventory.sh](../tools/car_inventory.sh); the
Windows kit has it as `usb\car_inventory.sh`. It runs from the stick rather
than being pasted: QNX's terminal can drop characters from a long paste,
which would run a damaged script.

1. Copy `car_inventory.sh` to the root of a USB stick (FAT32, at least 1 GB
   free). Copy the file as it is: opening and saving it in a Windows editor
   can change its line endings, and the head unit's shell then fails on
   every line.
2. Car parked, ignition on. Plug the stick into the car's USB port and wait
   ten seconds.
3. Open an SSH shell on the head unit (the same access the desktop installer
   uses) and find the stick:

   ```sh
   ls /mnt
   ```

   It shows as `umass` followed by letters and digits, for example
   `umass01200t7`.
4. Run the script from there (with your stick's name):

   ```sh
   sh /mnt/umass01200t7/car_inventory.sh
   ```

   It prints four steps and "Done: <folder>"; it takes about a minute,
   longer with a slow stick.
5. Wait ten seconds after "Done" before unplugging the stick.

The script writes to a new folder next to itself, `nbt-inventory-<n>`, so
running it again never overwrites an earlier run. Paths that do not exist on
your firmware are recorded in `errors.txt` instead of stopping it.

If you see `: not found` or `syntax error` on every line, the line endings
were changed: copy the file again from the kit or the repository.

What it collects:

- listings: `uname -a`, `mount`, `ls` of `/opt/hmi`, `/opt/sys`,
  `/opt/car` and `/fs/sda0/opt/car`, the program folders, `/dev`, `/pps`,
  `/dev/shmem` and `/var/opt/hmi`, plus `pidin arg` and `sloginfo`;
- `audioconn-l.txt`: `audioconn -l`, the audio connections the
  car can build with their numbers (read only; stopped after 10 s if the car's
  ConnectionMaster does not answer). The browser source `0xb8` that Doom and
  NBT-AAD's native audio use stops at volume 25; another number from this
  list can be tried with Doom's `audio-conn` file (the nbt-doom README);
- files: `nbt_version.txt`, `starter.cfg`, the two MOST libraries,
  `carplay.ini`, the iDrive libraries
  `libhmi*.so`, `libAppFramework.A.so` and `libCoreFramework.A.so`, the
  car-data library `hmiplugins/libhmiremoting.so`, the manifests and libraries
  of the coded plugins (`PluginsCoded`), and the `Plugins` folder;
- running programs: `HmiMain3D`, `NBTCarHU` and `NavNBTEvoController`,
  located through `pidin arg`. iDrive runs as `/opt/hmi/bin/zipped/HmiMain3D`,
  a path served by `ziploader` that is not a regular file, and the navigation
  as `./NavNBTEvoController`; the script then looks for the name in the
  program folders (`/opt/hmi/ID5/bin`, `/opt/nav/*/bin`, ...). `NBTCarHU` is
  copied once, from the path it runs from: on NBTevo_Y21432J `/opt` is a link
  to `/fs/sda0/opt`, so `/opt/car/bin` and `/fs/sda0/opt/car/bin` are the
  same folder.

The stick is usually FAT32 or exFAT, which have no symbolic links. The script
copies single files and the `Plugins` folder (no links in it on the first
run); a whole-`/opt`
copy to such a stick turns each followed link into a duplicate file.

With a full `/opt` dump of the same car, the copied files are redundant (the
dump has `hmi/ID5/bin/HmiMain3D` and `nav/eceusa/bin/NavNBTEvoController`);
what only the car gives is the listings, `pidin arg`, `sloginfo` and what
lives outside `/opt`. What a run shows is summarized in
[head-unit-interfaces.md](head-unit-interfaces.md), "At run time".


## Afterwards

Copy the `nbt-inventory-<n>` folder to your computer. `contents.txt` lists
what was copied and `errors.txt` what was missing. A file missing there
usually lives elsewhere on your firmware: search `ls-opt-hmi.txt` for it and
copy it by hand with `cp <path> /mnt/<stick>/nbt-inventory-<n>/files/`.

`ls-etc-ssh.txt` and `files/etc/inetd.conf` show how `sshd`
is started and whether `/etc` is a RAM copy (with `mount.txt`): what "SSH on"
consists of, which decides whether SSH can be turned on while iDrive does not
start. SSH is
turned on by a diagnostic job relayed through the car's gateway (port 6801 is
the gateway's, not the head unit's: `NBTFoundry/installer:
core/src/activation.rs`). No ssh keys are copied.

`sloginfo.txt` holds only the last few minutes (the system log is a 1 MB ring
buffer, mostly video decoder messages): run the script right after the event
you want to see, such as opening a plugin.

The folder is then studied on the computer for the car-data names, their
formats and the MOST functions ([bridge-design.md](bridge-design.md)).
