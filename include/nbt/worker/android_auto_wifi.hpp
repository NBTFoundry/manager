#ifndef NBT_WORKER_ANDROID_AUTO_WIFI_HPP
#define NBT_WORKER_ANDROID_AUTO_WIFI_HPP

#include <stddef.h>

namespace nbt {
namespace worker {

/* Which Wi-Fi NBT-AAD (wireless Android Auto) offers the phone, read as its
 * sources document it (no code taken). NBT_ALT_WIFI among the first 15
 * settings of /opt/aa/nbt-aad.env wins: DIRECT, WFD or P2P (any case, spaces
 * around) is the Wi-Fi Direct group, any other word the CarPlay hotspot. Unset
 * or empty, the "Use WiFi Direct Network" flag file (/var/opt/aa/wifi-direct,
 * or /opt/aa/wifi-direct made by hand) means the group; otherwise the hotspot.
 * No NBT-AAD at all reads as the hotspot: nothing then needs the group.
 *
 * When Android Auto uses the group, the manager leaves it on after Pair a
 * phone even if it switched it on: on a phone, Android shares one connection
 * to the same network between the two apps (Android 12 and later). */
enum AadWifi { kAadCarplayHotspot, kAadWifiDirect };

AadWifi aadWifiSetting(const char *envText, size_t size, bool flagFile);
bool androidAutoUsesWifiDirect();

} // namespace worker
} // namespace nbt

#endif
