// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/android_auto_wifi.hpp"

#include "nbt/worker/wifi_direct.hpp"

#include <memory>
#include <new>
#include <string.h>
#include <strings.h>

namespace nbt {
namespace worker {
namespace {

const char kEnvPath[] = "/opt/aa/nbt-aad.env";
const char kFlagPath[] = "/var/opt/aa/wifi-direct";
const char kManualFlagPath[] = "/opt/aa/wifi-direct";
const char kKey[] = "NBT_ALT_WIFI=";
const size_t kEnvMax = 4096;
const unsigned kEnvSettings = 15; // NBT-AAD's app hands its daemon the first 15

bool directWord(const char *value, size_t length) {
    while (length && (*value == ' ' || *value == '\t')) {
        ++value;
        --length;
    }
    while (length && (value[length - 1] == ' ' || value[length - 1] == '\t')) {
        --length;
    }
    return (length == 6 && !strncasecmp(value, "DIRECT", 6)) ||
           (length == 3 && (!strncasecmp(value, "WFD", 3) || !strncasecmp(value, "P2P", 3)));
}

} // namespace

AadWifi aadWifiSetting(const char *envText, size_t size, bool flagFile) {
    const size_t key = sizeof(kKey) - 1;
    unsigned settings = 0;
    for (size_t pos = 0; envText && pos < size && settings < kEnvSettings;) {
        const char *line = envText + pos;
        const char *end = static_cast<const char *>(memchr(line, '\n', size - pos));
        size_t length = end ? static_cast<size_t>(end - line) : size - pos;
        pos += length + 1;
        while (length && (line[length - 1] == '\r' || line[length - 1] == ' ')) {
            --length;
        }
        if (!length || line[0] == '#' || !memchr(line, '=', length)) {
            continue;
        }
        ++settings;
        if (length >= key && !memcmp(line, kKey, key)) {
            if (length == key) {
                break; // set but empty: the flag file decides; a later line is never read
            }
            return directWord(line + key, length - key) ? kAadWifiDirect : kAadCarplayHotspot;
        }
    }
    return flagFile ? kAadWifiDirect : kAadCarplayHotspot;
}

bool androidAutoUsesWifiDirect() {
    std::unique_ptr<char[]> text(new (std::nothrow) char[kEnvMax]);
    size_t length = 0;
    if (!text || !readCarFile(kEnvPath, text.get(), kEnvMax, &length)) {
        length = 0;
    }
    const bool flag = carFileExists(kFlagPath) || carFileExists(kManualFlagPath);
    return aadWifiSetting(text ? text.get() : NULL, length, flag) == kAadWifiDirect;
}

} // namespace worker
} // namespace nbt
