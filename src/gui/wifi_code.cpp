#include "nbt/gui/wifi_code.hpp"

#include "nbt/core/secure.hpp"

#include <errno.h>
#include <string.h>

namespace nbt {
namespace gui {
namespace {

bool printable(const char *text, size_t min, size_t max) {
    const size_t length = text ? strlen(text) : 0;
    if (length < min || length > max) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        if (text[i] < 0x20 || text[i] > 0x7e) {
            return false;
        }
    }
    return true;
}

bool append(core::Span<char> out, size_t &used, const char *text, bool escape) {
    for (; *text; ++text) {
        const bool special = escape && strchr("\\;,:\"", *text);
        if (used + (special ? 2u : 1u) >= out.size()) {
            return false;
        }
        if (special) {
            out.data()[used++] = '\\';
        }
        out.data()[used++] = *text;
    }
    out.data()[used] = 0;
    return true;
}

} // namespace

bool wifiSsidValid(const char *ssid) {
    return printable(ssid, 1, kWifiSsidMax);
}

bool wifiPassValid(const char *pass) {
    return printable(pass, 8, kWifiPassMax);
}

core::Status wifiCodeText(const char *ssid, const char *pass, core::Span<char> out) {
    size_t used = 0;
    if (!out.data() || !out.size() || !wifiSsidValid(ssid) || !wifiPassValid(pass)) {
        return core::Status::error(EINVAL, "no Wi-Fi to join");
    }
    if (append(out, used, "WIFI:T:WPA;S:", false) && append(out, used, ssid, true) &&
        append(out, used, ";P:", false) && append(out, used, pass, true) &&
        append(out, used, ";;", false)) {
        return core::Status();
    }
    core::secureClear(out.data(), out.size());
    return core::Status::error(ERANGE, "Wi-Fi code too long");
}

} // namespace gui
} // namespace nbt
