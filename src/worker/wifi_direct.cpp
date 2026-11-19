// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/wifi_direct.hpp"

#include "nbt/core/secure.hpp"
#include "nbt/log.h"

#include <ctype.h>
#include <fcntl.h>
#include <memory>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nbt {
namespace worker {
namespace {

const size_t kFileMax = 16384;
const char kKey[] = "NBTEvo0715"; // Connectivity's CWifiSecurity key (NBT-AAD's notes)
const char kPrefix[] = "DIRECT-03-";
const char kCarplay[] = "CarPlay";
const char *const kPassPaths[] = {"/var/opt/conn/WFDPass.conf", "/fs/sda1/opt/conn/WFDPass.conf"};
const char *const kConfigPaths[] = {
    "/var/opt/conn/carplay/iap2_Wifi.cfg", "/var/opt/conn/carplay/iap2.cfg",
    "/fs/sda1/opt/conn/carplay/iap2_Wifi.cfg", "/fs/sda1/opt/conn/carplay/iap2.cfg"};
const char kPersistency[] =
    "/var/opt/sys/persistence/early/pers_ConnectivityWlanPersistencyPersMapper";

int hexValue(unsigned char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}

bool endsWith(const char *text, size_t size, const char *suffix) {
    const size_t n = strlen(suffix);
    return size > n && !memcmp(text + size - n, suffix, n);
}

// The name without whitespace, as "DIRECT-03-<name>"; false when empty or too long.
bool ssidFromName(const char *name, size_t size, char out[kWifiSsidMax + 1]) {
    size_t used = strlen(kPrefix);
    memcpy(out, kPrefix, used);
    for (size_t i = 0; i < size; ++i) {
        if (isspace(static_cast<unsigned char>(name[i]))) {
            continue;
        }
        if (used >= kWifiSsidMax) {
            return false;
        }
        out[used++] = name[i];
    }
    out[used] = 0;
    return used > strlen(kPrefix);
}

// The next run of printable ASCII in a binary blob.
bool nextRun(const char *text, size_t size, size_t &pos, const char *&run, size_t &length) {
    while (pos < size) {
        const size_t start = pos;
        while (pos < size && text[pos] >= 0x20 && text[pos] < 0x7f) {
            ++pos;
        }
        if (pos > start) {
            run = text + start;
            length = pos - start;
            return true;
        }
        ++pos;
    }
    return false;
}

bool secretRun(const char *run, size_t length) { // 20 uppercase hex: the stored passphrase
    for (size_t i = 0; i < length; ++i) {
        if (!isdigit(static_cast<unsigned char>(run[i])) && (run[i] < 'A' || run[i] > 'F')) {
            return false;
        }
    }
    return length == 20;
}

// The path on the car; host tests put the car's files under NBT_TEST_CONN_ROOT.
bool carPath(const char *path, char *where, size_t size) {
    const char *root = "";
#ifdef NBT_TESTING
    if (getenv("NBT_TEST_CONN_ROOT")) {
        root = getenv("NBT_TEST_CONN_ROOT");
    }
#endif
    const int n = snprintf(where, size, "%s%s", root, path);
    return n > 0 && static_cast<size_t>(n) < size;
}

} // namespace

bool readCarFile(const char *path, char *buffer, size_t capacity, size_t *length) {
    char where[256];
    struct stat st;
    const int fd = carPath(path, where, sizeof(where)) ? open(where, O_RDONLY) : -1;
    *length = 0;
    if (fd < 0) {
        return false;
    }
    if (!fstat(fd, &st) && S_ISREG(st.st_mode)) {
        ssize_t got;
        while (*length < capacity && (got = read(fd, buffer + *length, capacity - *length)) > 0) {
            *length += static_cast<size_t>(got);
        }
    }
    close(fd);
    return *length > 0;
}

bool carFileExists(const char *path) {
    char where[256];
    struct stat st;
    return carPath(path, where, sizeof(where)) && !stat(where, &st);
}

bool wfdPassphrase(const char *text, size_t size, char out[kWifiPassMax + 1]) {
    size_t used = 0;
    int high = -1;
    for (size_t i = 0; i < size; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const int value = hexValue(c);
        if (c <= ' ') {
            continue;
        }
        if (value < 0 || (high >= 0 && used >= kWifiPassMax)) {
            core::secureClear(out, kWifiPassMax + 1);
            return false;
        }
        if (high < 0) {
            high = value;
            continue;
        }
        const char ch = static_cast<char>((high << 4 | value) ^ kKey[used % (sizeof(kKey) - 1)]);
        if (ch < 0x20 || ch > 0x7e) {
            core::secureClear(out, kWifiPassMax + 1);
            return false;
        }
        out[used++] = ch;
        high = -1;
    }
    out[used] = 0;
    if (high >= 0 || used < 8) { // WPA2: 8 to 63 printable characters
        core::secureClear(out, kWifiPassMax + 1);
        return false;
    }
    return true;
}

bool wfdNameFromCarplayConfig(const char *text, size_t size, char out[kWifiSsidMax + 1]) {
    bool accessory = false;
    for (size_t pos = 0; pos < size;) {
        const char *line = text + pos;
        const char *end = static_cast<const char *>(memchr(line, '\n', size - pos));
        size_t length = end ? static_cast<size_t>(end - line) : size - pos;
        pos += length + 1;
        while (length && isspace(static_cast<unsigned char>(line[length - 1]))) {
            --length;
        }
        while (length && isspace(static_cast<unsigned char>(*line))) {
            ++line;
            --length;
        }
        const char *equals = static_cast<const char *>(memchr(line, '=', length));
        if (length && line[0] == '[') {
            accessory = length >= 11 && !strncasecmp(line, "[accessory]", 11);
        } else if (equals && length && line[0] != '#' && line[0] != ';') {
            size_t key = static_cast<size_t>(equals - line);
            while (key && isspace(static_cast<unsigned char>(line[key - 1]))) {
                --key;
            }
            const char *value = equals + 1;
            const size_t valueLength = length - static_cast<size_t>(value - line);
            // SSID=<name> CarPlay is the CarPlay AP: the device name plus the suffix.
            if (key == 4 && !strncasecmp(line, "ssid", 4) &&
                endsWith(value, valueLength, kCarplay) &&
                ssidFromName(value, valueLength - strlen(kCarplay), out)) {
                return true;
            }
            if (accessory && key == 4 && !strncasecmp(line, "name", 4) &&
                ssidFromName(value, valueLength, out)) {
                return true;
            }
        }
    }
    return false;
}

bool wfdSsidFromPersistency(const char *text, size_t size, char out[kWifiSsidMax + 1]) {
    const char *run = NULL, *previous = NULL;
    size_t length = 0, previousLength = 0, pos = 0;
    // The group's own name, when Connectivity stored it.
    while (nextRun(text, size, pos, run, length)) {
        if (length > 7 && length <= kWifiSsidMax && !memcmp(run, "DIRECT-", 7)) {
            memcpy(out, run, length);
            out[length] = 0;
            return true;
        }
    }
    // "<name> CarPlay", the device name by construction.
    for (pos = 0; nextRun(text, size, pos, run, length);) {
        if (endsWith(run, length, kCarplay) && ssidFromName(run, length - strlen(kCarplay), out)) {
            return true;
        }
    }
    // Cars without CarPlay: the name stored just before the encrypted passphrase.
    for (pos = 0; nextRun(text, size, pos, run, length); previous = run, previousLength = length) {
        if (secretRun(run, length) && previous) {
            const size_t name = endsWith(previous, previousLength, kCarplay)
                                    ? previousLength - strlen(kCarplay)
                                    : previousLength;
            return ssidFromName(previous, name, out);
        }
    }
    return false;
}

bool readSavedWfdPassphrase(char out[kWifiPassMax + 1]) {
    if (!out) {
        return false;
    }
    std::unique_ptr<char[]> buffer(new (std::nothrow) char[kFileMax]);
    if (!buffer) {
        return false;
    }
    size_t length = 0;
    bool havePass = false;
    for (size_t i = 0; !havePass && i < sizeof(kPassPaths) / sizeof(kPassPaths[0]); ++i) {
        havePass = readCarFile(kPassPaths[i], buffer.get(), kFileMax, &length) &&
                   wfdPassphrase(buffer.get(), length, out);
    }
    core::secureClear(buffer.get(), kFileMax);
    return havePass;
}

bool sessionReply(char *out, size_t size, const char *host, const char *ssid, const char *pass) {
    if (!out || !size || !host) {
        return false;
    }
    const bool wifi = ssid && ssid[0] && pass && pass[0];
    const int n = snprintf(out, size, "SESSION\t%s%s%s%s%s\n", host, wifi ? "\t" : "",
                           wifi ? ssid : "", wifi ? "\t" : "", wifi ? pass : "");
    if (n < 0 || static_cast<size_t>(n) >= size) {
        core::secureClear(out, size);
        return false;
    }
    return true;
}

bool sessionReply(char *out, size_t size, const char *host) {
    char pass[kWifiPassMax + 1] = {0}, ssid[kWifiSsidMax + 1] = {0};
    const char *source = NULL;
    std::unique_ptr<char[]> buffer(new (std::nothrow) char[kFileMax]);
    size_t length = 0;
    const bool havePass = readSavedWfdPassphrase(pass);
    for (size_t i = 0; buffer && !source && i < sizeof(kConfigPaths) / sizeof(kConfigPaths[0]);
         ++i) {
        if (readCarFile(kConfigPaths[i], buffer.get(), kFileMax, &length) &&
            wfdNameFromCarplayConfig(buffer.get(), length, ssid)) {
            source = kConfigPaths[i];
        }
    }
    if (buffer && !source && readCarFile(kPersistency, buffer.get(), kFileMax, &length) &&
        wfdSsidFromPersistency(buffer.get(), length, ssid)) {
        source = kPersistency;
    }
    if (buffer) {
        core::secureClear(buffer.get(), kFileMax);
    }
    const bool wifi = havePass && source;
    if (wifi) {
        nbt_logf("Wi-Fi Direct credentials for %s (name from %s); group availability unchecked",
                 ssid, source);
    } else {
        nbt_logf("Wi-Fi Direct: %s", !havePass ? "no usable passphrase in WFDPass.conf"
                                               : "no group name in the CarPlay configs or "
                                                 "Connectivity's persistency");
    }
    const bool ok = sessionReply(out, size, host, wifi ? ssid : NULL, wifi ? pass : NULL);
    core::secureClear(pass, sizeof(pass));
    return ok;
}

} // namespace worker
} // namespace nbt
