#!/bin/sh
# NBT Manager car inventory: see docs/car-inventory.md.
#
# Run on the head unit from a USB stick: sh /mnt/umass<...>/car_inventory.sh
# It only reads on the head unit. It writes a new folder, nbt-inventory-<n>,
# next to itself (on the stick). Missing paths are recorded in errors.txt.
# Plain POSIX/ksh for QNX 6.5's /bin/sh; keep LF line endings.

case "$0" in */*) U="${0%/*}" ;; *) U=. ;; esac
if ! ( : > "$U/.nbt-write-test" ) 2>/dev/null; then
  echo "Cannot write next to this script ($U). Run it from the USB stick."
  exit 1
fi
rm -f "$U/.nbt-write-test"
N=1
while [ -e "$U/nbt-inventory-$N" ]; do N=$((N+1)); done
O="$U/nbt-inventory-$N"
E="$O/errors.txt"
mkdir -p "$O/files" || { echo "Cannot create $O"; exit 1; }
echo "Writing to $O"

# Copies one file, keeping its path under files/.
copy() {
  if [ -f "$1" ]; then
    mkdir -p "$O/files${1%/*}" && cp "$1" "$O/files${1%/*}/" 2>>"$E" || echo "copy failed: $1" >>"$E"
  else
    echo "missing: $1" >>"$E"
  fi
}

echo "1/4 listings"
uname -a >"$O/uname.txt" 2>>"$E"
mount >"$O/mount.txt" 2>>"$E"
ls -lR /opt/hmi >"$O/ls-opt-hmi.txt" 2>>"$E"
ls -lR /opt/sys/etc /opt/sys/lib >"$O/ls-opt-sys.txt" 2>>"$E"
ls -lR /opt/car /fs/sda0/opt/car >"$O/ls-opt-car.txt" 2>>"$E"
ls -l /opt /var/opt /opt/sys/bin /opt/core/bin /opt/os/bin /opt/conn/bin /opt/mm/bin /dev >"$O/ls-bin-dev.txt" 2>>"$E"
ls -lR /pps >"$O/ls-pps.txt" 2>>"$E"
ls -l /dev/shmem >"$O/ls-shmem.txt" 2>>"$E"
ls -lR /var/opt/hmi >"$O/ls-var-hmi.txt" 2>>"$E"
pidin arg >"$O/pidin-arg.txt" 2>>"$E"
# The process manager's live view (docs/hmi-restart.md): package and process
# states. Read only; /dev/starter/processproblems is single-client, left alone.
ls -l /dev/starter /dev/starter/status >"$O/starter-dev.txt" 2>>"$E"
for f in version variant status/overview; do
  echo "== /dev/starter/$f" >>"$O/starter-dev.txt"
  cat "/dev/starter/$f" >>"$O/starter-dev.txt" 2>>"$E"
done
sloginfo >"$O/sloginfo.txt" 2>>"$E"
# The audio connections the car can build, with their numbers (read only:
# `-l` lists; only -b/-r build or remove). The browser source Doom and NBT-AAD
# play through (0xb8) stops at volume 25; this shows the others to try.
# Stopped after 10 s if the ConnectionMaster does not answer.
for a in /fs/sda0/opt/mm/bin/audioconn /opt/mm/bin/audioconn; do
  if [ -x "$a" ]; then
    LD_PRELOAD= "$a" -l >"$O/audioconn-l.txt" 2>>"$E" &
    p=$!
    n=0
    while kill -0 "$p" 2>/dev/null && [ "$n" -lt 10 ]; do
      sleep 1
      n=$((n+1))
    done
    kill "$p" 2>/dev/null && echo "audioconn -l: stopped after 10 s" >>"$E"
    break
  fi
done

echo "2/4 configuration and libraries"
ls -l /lib /usr/lib /proc/boot 2>>"$E" | grep -E 'stdc|cpp' >"$O/libstdc++.txt" 2>>"$E"
for f in /lib/libstdc++.so.6 /usr/lib/libstdc++.so.6 /proc/boot/libstdc++.so.6; do
  if [ -f "$f" ]; then
    ls -l "$f" >>"$O/libstdc++.txt" 2>>"$E"
    cksum "$f" >>"$O/libstdc++.txt" 2>>"$E"
  fi
done
# How sshd is started (inetd) and whether "SSH on" lives in /etc: for recovery
# while iDrive is down. Only listings and inetd.conf; no ssh keys are copied.
ls -ld /etc /etc/inetd.conf >"$O/ls-etc-ssh.txt" 2>>"$E"
ls -l /etc/ssh >>"$O/ls-etc-ssh.txt" 2>>"$E"
# The media launcher (vdev-medialaunch) probes every new USB device and can
# reset it, removing a gamepad right after it is plugged in. Its configuration
# decides which devices it handles (docs/gamepad-bridge.md).
ls -l /etc >"$O/ls-etc.txt" 2>>"$E"
for f in /etc/*.cfg /etc/*medialaunch* /etc/system/config/*medialaunch*; do
  [ -f "$f" ] && copy "$f"
done
for f in /etc/inetd.conf /opt/sys/etc/nbt_version.txt /opt/sys/etc/starter.cfg \
         /opt/sys/lib/libimp_sys_most_base_nbt.cpp-ne.so.17 \
         /opt/sys/lib/libsys_dsi_colibry.cpp-ne.so.8 \
         /var/opt/conn/carplay/carplay.ini; do
  copy "$f"
done
# The plugin framework, the car-data library and the coded plugins' manifests
# and libraries. Their certificate files (.p12/.p7b) are left out on purpose.
for f in /opt/hmi/ID5/lib/libhmi*.so /opt/hmi/ID5/lib/libAppFramework.A.so \
         /opt/hmi/ID5/lib/libCoreFramework.A.so \
         /opt/hmi/ID5/data/ro/hmiplugins/libhmiremoting.so \
         /opt/hmi/ID5/data/ro/PluginsCoded/*/*.manifest \
         /opt/hmi/ID5/data/ro/PluginsCoded/*/lib*.so; do
  copy "$f"
done

echo "3/4 iDrive plugins"
if [ -d /opt/hmi/ID5/data/ro/Plugins ]; then
  mkdir -p "$O/files/opt/hmi/ID5/data/ro" &&
    cp -R /opt/hmi/ID5/data/ro/Plugins "$O/files/opt/hmi/ID5/data/ro/" 2>>"$E" ||
    echo "copy failed: Plugins" >>"$E"
else
  echo "missing: /opt/hmi/ID5/data/ro/Plugins" >>"$E"
fi

echo "4/4 running programs"
# iDrive, the car service and the navigation's GPS sender, by their running
# processes. A process path is not always a regular file: iDrive runs as
# /opt/hmi/bin/zipped/HmiMain3D, served by ziploader, and the navigation as
# ./NavNBTEvoController. Those are looked up by name in the program folders.
find_copy() {
  if [ -f "$1" ]; then copy "$1"; return; fi
  for d in /opt/hmi/ID5/bin /opt/hmi/ID5/bin/zipped /opt/hmi/bin /opt/nav/bin \
           /opt/nav/*/bin /fs/sda0/opt/car/bin /opt/car/bin /opt/sys/bin; do
    if [ -f "$d/${1##*/}" ]; then copy "$d/${1##*/}"; return; fi
  done
  echo "missing: $1" >>"$E"
}
pidin arg 2>/dev/null | while read pid exe rest; do
  case "${exe##*/}" in
  HmiMain3D|NBTCarHU|NavNBTEvoController) find_copy "$exe" ;;
  esac
done

ls -lR "$O" >"$O/contents.txt" 2>>"$E"
sync 2>/dev/null
echo "Done: $O"
echo "Wait 10 seconds before unplugging the stick."
