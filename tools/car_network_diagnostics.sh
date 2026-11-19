#!/bin/sh
# Read-only Wi-Fi Direct and manager state, printed to stdout. Run on the HU.
# Deliberately omit passphrases, WPS PINs, leases and binary profile contents.

echo "== Wi-Fi interfaces =="
ifconfig wfd0 2>&1
ifconfig uap0 2>&1

echo "== Network processes =="
pidin arg 2>/dev/null | grep -E '(Connectivity|mwud|dhcpd|nbt-manager-launcher|nbt-managerd)'

echo "== Credential and generated configuration metadata =="
ls -l /var/opt/conn/WFDPass.conf /var/opt/conn/WFDWps.conf \
  /var/opt/conn/wifidir_init.conf /var/opt/conn/dhcpd.wfd0.conf \
  /var/opt/sys/persistence/early/pers_ConnectivityWlanPersistencyPersMapper 2>&1

echo "== Wi-Fi Direct device and channel configuration =="
for file in /var/opt/conn/wifidir_init.conf /opt/conn/bin/wifidir_init.conf; do
  if [ -f "$file" ]; then
    echo "$file"
    grep -E '^[[:space:]]*(DEVICE_NAME|SSID_SUFFIX|INTENT|LISTEN_CHANNEL_NUMBER|OP_CHANNEL_NUMBER)[[:space:]]*=' "$file"
  fi
done

echo "== Connectivity defaults (configuration, not live group state) =="
for file in /var/opt/conn/ConfigGen_Options.ini /opt/conn/data/ConfigGen_Options.ini; do
  if [ -f "$file" ]; then
    echo "$file"
    grep -E '^[[:space:]]*(NetworkingService_wfd0\.(autogo|startdhcps|ipmode|ipaddress|ipnetmask|dhcpsrange)|ConnMngr\.dhcpmode_wfd0)[[:space:]]*=' "$file"
  fi
done

echo "== Manager activation state =="
cat /var/opt/nbt-manager/state/manager/activation 2>&1
echo "== End =="
