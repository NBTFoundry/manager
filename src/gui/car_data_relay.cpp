// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/car_data_relay.hpp"

#include "nbt/gui/hmi_hold.hpp"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

namespace nbt {
namespace gui {
namespace {

const char kPrefix[] = "nbt-bridge1-";

bool appName(const char *app, size_t n) {
    if (!n || n > NBT_HOLD_APP_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        const char c = app[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

int path(char *out, size_t size, const char *dir, const char *middle, const char *suffix) {
    if (!out || !dir) {
        return -1;
    }
    const int n = snprintf(out, size, "%s/%s%s%s", dir, kPrefix, middle, suffix);
    return n < 0 || static_cast<size_t>(n) >= size ? -1 : 0;
}

} // namespace

int appRecordPath(const char *dir, const char *app, const char *suffix, char *out, size_t size) {
    char tail[16];
    if (!app || !appName(app, strlen(app)) || !suffix ||
        snprintf(tail, sizeof(tail), ".%s", suffix) >= static_cast<int>(sizeof(tail))) {
        return -1;
    }
    return path(out, size, dir, app, tail);
}

int appRecordWrite(const char *dir, const char *app, const char *suffix, uint64_t now,
                   const char *text) {
    char file[160], line[24 + NBT_CDS_POLICY_MAX + 2];
    if (!text || appRecordPath(dir, app, suffix, file, sizeof(file))) {
        return -1;
    }
    const int n =
        snprintf(line, sizeof(line), "%llu %s\n", static_cast<unsigned long long>(now), text);
    return n < 0 || static_cast<size_t>(n) >= sizeof(line)
               ? -1
               : shmemWriteLine(file, line, static_cast<size_t>(n));
}

bool appRecordRead(const char *file, uint64_t now, char *out, size_t size) {
    char text[24 + NBT_CDS_POLICY_MAX + 2];
    const int count = shmemReadLine(file, text, sizeof(text));
    if (!out || count < 4 || static_cast<size_t>(count) >= sizeof(text) ||
        text[count - 1] != '\n') {
        return false;
    }
    if (memchr(text, 0, static_cast<size_t>(count))) {
        return false;
    }
    text[count - 1] = 0;
    uint64_t ms = 0;
    size_t at = 0;
    while (at < 20 && text[at] >= '0' && text[at] <= '9') {
        ms = ms * 10u + static_cast<unsigned>(text[at] - '0');
        ++at;
    }
    if (!at || text[at] != ' ' || (at > 1 && text[0] == '0') || now < ms ||
        now - ms > NBT_HOLD_FRESH_MS) {
        return false;
    }
    const size_t n = strlen(text + at + 1);
    if (n >= size) {
        return false;
    }
    memcpy(out, text + at + 1, n + 1);
    return true;
}

bool appRecordEntry(const char *entry, const char *suffix) {
    const size_t prefix = sizeof(kPrefix) - 1;
    const size_t tail = suffix ? strlen(suffix) + 1 : 0;
    const size_t n = entry ? strlen(entry) : 0;
    return suffix && n > prefix + tail && !memcmp(entry, kPrefix, prefix) &&
           entry[n - tail] == '.' && !strcmp(entry + n - tail + 1, suffix) &&
           appName(entry + prefix, n - prefix - tail);
}

int netRecordWrite(const char *dir, const char *app, uint64_t now, const char *policy) {
    return !policy || nbt_net_policy_parse(policy, NULL)
               ? -1
               : appRecordWrite(dir, app, "net", now, policy);
}

bool netWfd0InUse(const char *dir, uint64_t now) {
    DIR *d = dir ? opendir(dir) : NULL;
    bool used = false;
    if (!d) {
        return false;
    }
    struct dirent *entry;
    while (!used && (entry = readdir(d)) != NULL) {
        char file[160], text[NBT_NET_POLICY_MAX + 1];
        nbt_net_policy policy;
        if (!appRecordEntry(entry->d_name, "net") ||
            snprintf(file, sizeof(file), "%s/%s", dir, entry->d_name) >=
                static_cast<int>(sizeof(file)) ||
            !appRecordRead(file, now, text, sizeof(text)) || nbt_net_policy_parse(text, &policy)) {
            continue;
        }
        for (unsigned i = 0; i < policy.count; ++i) {
            used = used || (policy.rules[i].interfaces & NBT_NET_WFD0) != 0;
        }
    }
    closedir(d);
    return used;
}

int carDataWantPath(const char *dir, const char *app, char *out, size_t size) {
    return appRecordPath(dir, app, "want", out, size);
}

int carDataWantWrite(const char *dir, const char *app, uint64_t now, const char *names) {
    return !names || nbt_cds_policy_parse(names, NULL)
               ? -1
               : appRecordWrite(dir, app, "want", now, names);
}

bool carDataWantRead(const char *file, uint64_t now, char names[NBT_CDS_POLICY_MAX + 1]) {
    return appRecordRead(file, now, names, NBT_CDS_POLICY_MAX + 1) &&
           !nbt_cds_policy_parse(names, NULL);
}

bool carDataWantEntry(const char *entry) {
    return appRecordEntry(entry, "want");
}

int carDataValuePath(const char *dir, const char *name, char *out, size_t size) {
    if (!name || !nbt_cds_name_valid(name, strlen(name))) {
        return -1;
    }
    char middle[4 + NBT_CDS_NAME_MAX + 1];
    snprintf(middle, sizeof(middle), "cds-%s", name);
    return path(out, size, dir, middle, "");
}

bool carDataAscii(const unsigned short *text, char out[NBT_CDS_VALUE_MAX + 1]) {
    if (!text) {
        return false;
    }
    for (size_t i = 0; i <= NBT_CDS_VALUE_MAX; ++i) {
        if (!text[i]) {
            out[i] = 0;
            return true;
        }
        if (text[i] < 32 || text[i] > 126) {
            return false;
        }
        out[i] = static_cast<char>(text[i]);
    }
    return false;
}

} // namespace gui
} // namespace nbt
