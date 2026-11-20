// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/reply_parser.hpp"

#include "nbt/util.h"

#include <string.h>

namespace nbt {
namespace gui {
namespace {

static const size_t kFieldsMax = kAppsMax + 2;

bool hex64(const char *s) {
    for (size_t i = 0; i < 64; ++i) {
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) {
            return false;
        }
    }
    return s[64] == 0;
}

bool digits(const char *s, size_t min, size_t max) {
    size_t n = 0;
    for (; s[n]; ++n) {
        if (n >= max || s[n] < '0' || s[n] > '9') {
            return false;
        }
    }
    return n >= min;
}

bool validHost(const char *s) {
    char copy[32];
    unsigned parts = 0;
    uint64_t value = 0;
    if (strlen(s) >= sizeof(copy)) {
        return false;
    }
    strcpy(copy, s);
    char *colon = strchr(copy, ':');
    if (!colon || strchr(colon + 1, ':')) {
        return false;
    }
    *colon = 0;
    if (!digits(colon + 1, 1, 5) || nbt_parse_u64(colon + 1, &value, 0) || value > 65535) {
        return false;
    }
    char *cursor = copy;
    char *part = NULL;
    while ((part = cursor) != NULL) {
        char *dot = strchr(part, '.');
        if (dot) {
            *dot = 0;
            cursor = dot + 1;
        } else {
            cursor = NULL;
        }
        if (++parts > 4 || !digits(part, 1, 3) || nbt_parse_u64(part, &value, 0) || value > 255) {
            return false;
        }
    }
    return parts == 4;
}

int split(char *copy, const char *reply, char **fields, size_t want) {
    const size_t length = strlen(reply);
    size_t count = 1;
    if (!length || length >= kReplyMax || want > kFieldsMax) {
        return -1;
    }
    memcpy(copy, reply, length + 1);
    fields[0] = copy;
    for (char *p = copy; *p; ++p) {
        const unsigned char ch = static_cast<unsigned char>(*p);
        if (ch != '\t' && (ch < 32 || ch > 126)) {
            return -1;
        }
        if (*p != '\t') {
            continue;
        }
        if (count == want) {
            return -1;
        }
        *p = 0;
        fields[count++] = p + 1;
    }
    return count == want ? 0 : -1;
}

int optionalField(const char *field, char *out, size_t size, size_t max, unsigned flags) {
    if (!strcmp(field, "-")) {
        out[0] = 0;
        return 0;
    }
    if (!nbt_token_valid(field, max, flags) || strlen(field) >= size) {
        return -1;
    }
    strcpy(out, field);
    return 0;
}

int parseNames(const char *reply, const char *tag, char *copy, char **f, uint64_t *count) {
    char number[8];
    const size_t tagLength = strlen(tag);
    if (strncmp(reply, tag, tagLength) || reply[tagLength] != '\t') {
        return -1;
    }
    const char *tab = reply + tagLength;
    size_t i = 0;
    for (; tab[1 + i] && tab[1 + i] != '\t'; ++i) {
        if (i + 1 >= sizeof(number)) {
            return -1;
        }
        number[i] = tab[1 + i];
    }
    number[i] = 0;
    return !digits(number, 1, 2) || nbt_parse_u64(number, count, 0) || *count > kAppsMax ||
                   split(copy, reply, f, static_cast<size_t>(*count) + 2)
               ? -1
               : 0;
}

// "<application>" or "<application>|<value>": the value ("" without), NULL if malformed.
const char *entry(char *field, size_t valueMax) {
    char *value = strchr(field, '|');
    if (value) {
        *value++ = 0;
    }
    const bool ok = !value || (*value && strlen(value) <= valueMax);
    return ok && nbt_token_valid(field, kIdMax, 0) ? (value ? value : "") : NULL;
}

} // namespace

bool ReplyParser::isError(const char *reply) {
    return !strncmp(reply, "ERR ", 4) && nbt_token_valid(reply + 4, 32, 0);
}

bool ReplyParser::samePackage(const WorkerStatus &a, const WorkerStatus &b) {
    return a.state == W_VERIFIED && b.state == W_VERIFIED && !strcmp(a.app, b.app) &&
           !strcmp(a.proposed, b.proposed) && a.sequence == b.sequence &&
           !strcmp(a.digest, b.digest);
}

int ReplyParser::parseStatus(const char *reply, WorkerStatus &s) {
    char copy[kReplyMax], *f[8];
    if (!reply || split(copy, reply, f, 8) || strcmp(f[0], "STATUS")) {
        return -1;
    }
    memset(&s, 0, sizeof(s));
    if (strlen(f[1]) != 1 || f[1][0] < '0' || f[1][0] > '6') {
        return -1;
    }
    s.state = f[1][0] - '0';
    if (optionalField(f[2], s.app, sizeof(s.app), kIdMax, 0) ||
        optionalField(f[3], s.installed, sizeof(s.installed), kIdMax,
                      NBT_TOKEN_UPPER | NBT_TOKEN_DOT) ||
        optionalField(f[4], s.proposed, sizeof(s.proposed), kIdMax,
                      NBT_TOKEN_UPPER | NBT_TOKEN_DOT) ||
        !digits(f[5], 1, 20) || nbt_parse_u64(f[5], &s.sequence, 0) || !f[7][0] ||
        strlen(f[7]) >= sizeof(s.message)) {
        return -1;
    }
    if (!strcmp(f[6], "-")) {
        s.digest[0] = 0;
    } else if (hex64(f[6])) {
        memcpy(s.digest, f[6], 65);
    } else {
        return -1;
    }
    if (strcmp(f[7], "-")) {
        strcpy(s.message, f[7]);
    }
    if (s.state == W_VERIFIED || s.state == W_INSTALLING) {
        if (!s.app[0] || !s.proposed[0] || !s.digest[0]) {
            return -1;
        }
    } else if (s.proposed[0] || s.digest[0] || s.sequence) {
        return -1;
    }
    return 0;
}

int ReplyParser::parseApps(UiController &ui, const char *reply) {
    char copy[kReplyMax], *f[kFieldsMax];
    uint64_t count = 0;
    if (parseNames(reply, "APPS", copy, f, &count)) {
        return -1;
    }
    ui.appCount = 0; // a malformed list is a protocol error: the GUI stops
    for (size_t i = 0; i < count; ++i) {
        const char *value = entry(f[i + 2], 6); // "update": disabled until updated
        if (!value || (value[0] && strcmp(value, "update")) ||
            ui.appIndex(f[i + 2]) != ui.appCount) { // or listed twice
            return -1;
        }
        ui.needsUpdate[i] = value[0] != 0;
        strcpy(ui.apps[ui.appCount++], f[i + 2]);
    }
    memset(ui.running, 0, sizeof(ui.running));
    memset(ui.appStatus, 0, sizeof(ui.appStatus));
    return 0;
}

int ReplyParser::parseRunning(UiController &ui, const char *reply) {
    char copy[kReplyMax], *f[kFieldsMax];
    unsigned char running[kAppsMax];
    char status[kAppsMax][kAppStatusMax + 1];
    uint64_t count = 0;
    if (parseNames(reply, "RUNNING", copy, f, &count)) {
        return -1;
    }
    memset(running, 0, sizeof(running));
    memset(status, 0, sizeof(status));
    for (size_t i = 0; i < count; ++i) {
        const char *value = entry(f[i + 2], kAppStatusMax);
        const size_t j = value ? ui.appIndex(f[i + 2]) : ui.appCount;
        if (j == ui.appCount || running[j]) {
            return -1;
        }
        running[j] = 1;
        strcpy(status[j], value);
    }
    memcpy(ui.running, running, sizeof(running));
    memcpy(ui.appStatus, status, sizeof(status));
    return 0;
}

int ReplyParser::parseSettings(UiController &ui, const char *reply) {
    char copy[kReplyMax], *f[NBT_SETTINGS_MAX + 3], number[3];
    const char *end = NULL;
    nbt_settings parsed;
    uint64_t count = 0;
    if (strncmp(reply, "SETTINGS\t", 9) || !(end = strchr(reply + 9, '\t'))) {
        return -1;
    }
    const size_t length = static_cast<size_t>(end - (reply + 9));
    if (!length || length >= sizeof(number)) {
        return -1;
    }
    memcpy(number, reply + 9, length);
    number[length] = 0;
    if ((length > 1 && number[0] == '0') || nbt_parse_u64(number, &count, 0) ||
        count > NBT_SETTINGS_MAX || split(copy, reply, f, static_cast<size_t>(count) + 3)) {
        return -1;
    }
    // "live" or "restart", then any of "+files", "+autostart", "+autostart-paused", "+update".
    char *rest = NULL, *token = strtok_r(f[2], "+", &rest);
    const bool live = token && !strcmp(token, "live");
    bool files = false, autostart = false, paused = false, update = false;
    if (!live && (!token || strcmp(token, "restart"))) {
        return -1;
    }
    while ((token = strtok_r(NULL, "+", &rest))) {
        paused = paused || !strcmp(token, "autostart-paused");
        autostart = autostart || paused || !strcmp(token, "autostart");
        files = files || !strcmp(token, "files");
        update = update || !strcmp(token, "update");
        if (strcmp(token, "files") && strcmp(token, "autostart") &&
            strcmp(token, "autostart-paused") && strcmp(token, "update")) {
            return -1;
        }
    }
    memset(&parsed, 0, sizeof(parsed));
    for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
        if (nbt_settings_add(&parsed, f[i + 3])) {
            return -1;
        }
    }
    ui.settings = parsed;
    ui.liveSettings = live;
    ui.files.offered = files;
    ui.autostart = autostart;
    ui.autostartPaused = paused;
    ui.settingsUpdate = update;
    return 0;
}

// No credential in the session reply: the QR contains Wi-Fi details only.
int ReplyParser::parseSession(UiController &ui, const char *reply) {
    char copy[kReplyMax], *f[4];
    const bool wifi = !split(copy, reply, f, 4);
    if ((!wifi && split(copy, reply, f, 2)) || strcmp(f[0], "SESSION") || !validHost(f[1]) ||
        (wifi && (!wifiSsidValid(f[2]) || !wifiPassValid(f[3])))) {
        nbt_clear(copy, sizeof(copy));
        return -1;
    }
    ui.go(Waiting);
    ui.message[0] = ui.app[0] = 0;
    strcpy(ui.host, f[1]);
    const bool ok = !wifi || ui.setWifi(f[2], f[3]);
    nbt_clear(copy, sizeof(copy));
    return ok ? 0 : -1;
}

} // namespace gui
} // namespace nbt
