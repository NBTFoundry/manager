// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_WIFI_CODE_HPP
#define NBT_GUI_WIFI_CODE_HPP

#include "nbt/core/span.hpp"
#include "nbt/core/status.hpp"

namespace nbt {
namespace gui {

static const size_t kWifiSsidMax = 32;
static const size_t kWifiPassMax = 63;

/* The text of the QR code phones join a Wi-Fi network from,
 * "WIFI:T:WPA;S:<ssid>;P:<passphrase>;;", with \ ; , : and " escaped by a
 * backslash. Clears `out` on failure. */
core::Status wifiCodeText(const char *ssid, const char *pass, core::Span<char> out);

/* 1..32 printable ASCII for the name, 8..63 for the WPA2 passphrase. */
bool wifiSsidValid(const char *ssid);
bool wifiPassValid(const char *pass);

} // namespace gui
} // namespace nbt

#endif
