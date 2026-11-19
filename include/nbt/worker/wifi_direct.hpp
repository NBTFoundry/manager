// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_WIFI_DIRECT_HPP
#define NBT_WORKER_WIFI_DIRECT_HPP

#include <stddef.h>

namespace nbt {
namespace worker {

/* Saved credentials for the car's Wi-Fi Direct group (wfd0). Reading these
 * files does not create a group or establish that the radio advertises it.
 * Read as NBT-AAD documents it (its alt_wifi notes; no code taken):
 *
 * - The WPA2 passphrase is /var/opt/conn/WFDPass.conf (else the copy under
 *   /fs/sda1/opt/conn): hex, each byte XORed with "NBTEvo0715" repeated.
 * - The name is "DIRECT-03-" + Connectivity's WLAN device name without
 *   whitespace. The car's ifconfig and mwud do not report it, so it comes
 *   from the CarPlay configs (iap2_Wifi.cfg "SSID=<name> CarPlay", iap2.cfg
 *   [accessory] name=), else from Connectivity's WLAN persistency, which
 *   holds the "DIRECT-" name itself, "<name> CarPlay", or the name just
 *   before the encrypted passphrase. */

static const size_t kWifiSsidMax = 32;
static const size_t kWifiPassMax = 63;

bool wfdPassphrase(const char *text, size_t size, char out[kWifiPassMax + 1]);
bool wfdNameFromCarplayConfig(const char *text, size_t size, char out[kWifiSsidMax + 1]);
bool wfdSsidFromPersistency(const char *text, size_t size, char out[kWifiSsidMax + 1]);
bool readSavedWfdPassphrase(char out[kWifiPassMax + 1]);

/* A file of the car's own (under NBT_TEST_CONN_ROOT in host tests): up to
 * capacity bytes of a regular file; false when missing or empty. */
bool readCarFile(const char *path, char *buffer, size_t capacity, size_t *length);
bool carFileExists(const char *path);

/* The SESSION reply: SESSION, host, then SSID and passphrase when known.
 * There is no HTTP credential. Clear the output after sending. */
bool sessionReply(char *out, size_t size, const char *host);
bool sessionReply(char *out, size_t size, const char *host, const char *ssid, const char *pass);

} // namespace worker
} // namespace nbt

#endif
