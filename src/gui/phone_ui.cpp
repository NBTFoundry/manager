// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/phone_ui.hpp"
#include "nbt/gui/autostart_ui.hpp"
#include "nbt/gui/files_ui.hpp"
#include "nbt/gui/language_ui.hpp"
#include "nbt/gui/reply_parser.hpp"
#include "nbt/gui/setup_ui.hpp"
#include "nbt/gui/shortcut_ui.hpp"
#include "nbt/util.h"
#include <stdio.h>
#include <string.h>
namespace nbt {
namespace gui {
bool phonePress(UiController &ui, Key key) {
    if (setupPress(ui, key) || filesPress(ui, key)) {
        return true;
    }
    if (ui.screen == Waiting) {
        if (key == KeyBack || ui.focus == 1) {
            ui.leaveSession(text(T_SESSION_CLOSED));
        } else if (ui.hasWifi()) {
            ui.queued = R_PHONE_OPEN;
        } else {
            textf(ui.message, sizeof(ui.message), T_WIFI_DETAILS_UNAVAILABLE);
        }
        return true;
    }
    if (ui.screen == PhoneConfirm) {
        ui.queued = key == KeyBack || ui.focus == 0 ? R_PHONE_NO : R_PHONE_YES;
        return true;
    }
    if (ui.screen == Phones) {
        if (key == KeyBack || ui.focus >= ui.phoneCount) {
            ui.go(Settings);
        } else {
            ui.phoneSelected = ui.focus;
            ui.go(PhoneForget);
        }
        return true;
    }
    if (ui.screen == PhoneForget) {
        if (key == KeyBack || ui.focus == 0) {
            ui.go(Phones);
        } else {
            ui.queued = R_PHONE_FORGET;
        }
        return true;
    }
    return false;
}
bool phoneRequest(const UiController &ui, int kind, Request &request) {
    char *out = request.command;
    const size_t size = sizeof(request.command);
    switch (kind) {
    case R_PHONE_STATUS:
        snprintf(out, size, "PHONE STATUS");
        break;
    case R_PHONE_OPEN:
        snprintf(out, size, "PHONE OPEN");
        break;
    case R_PHONE_YES:
    case R_PHONE_NO:
        snprintf(out, size, "PHONE %s %s", kind == R_PHONE_YES ? "YES" : "NO", ui.phoneAction);
        break;
    case R_PHONES:
        snprintf(out, size, "PHONE LIST");
        break;
    case R_PHONE_FORGET:
        if (ui.phoneSelected >= ui.phoneCount) {
            return false;
        }
        snprintf(out, size, "PHONE FORGET %s", ui.phoneIds[ui.phoneSelected]);
        break;
    default:
        return setupRequest(kind, request) || languageRequest(ui, kind, request) ||
               autostartRequest(ui, kind, request) || shortcutRequest(ui, kind, request) ||
               filesRequest(ui, kind, request);
    }
    return true;
}
namespace {
bool parsePhones(UiController &ui, const char *reply) {
    if (strncmp(reply, "PHONES\t", 7) || reply[7] < '0' || reply[7] > '4' ||
        (reply[8] && reply[8] != '\t')) {
        return false;
    }
    const size_t count = static_cast<size_t>(reply[7] - '0');
    const char *p = reply + 8;
    for (size_t i = 0; i < count; ++i) {
        if (*p++ != '\t' || strlen(p) < 66 || p[64] != '|') {
            return false;
        }
        memcpy(ui.phoneIds[i], p, 64);
        ui.phoneIds[i][64] = 0;
        unsigned char decoded[32];
        if (nbt_hex_decode(ui.phoneIds[i], decoded, 32)) {
            return false;
        }
        p += 65;
        const size_t n = strcspn(p, "\t");
        if (!n || n > 32) {
            return false;
        }
        memcpy(ui.phoneNames[i], p, n);
        ui.phoneNames[i][n] = 0;
        p += n;
    }
    if (*p) {
        return false;
    }
    ui.phoneCount = count;
    return true;
}
bool pending(UiController &ui, const char *reply) {
    if (!strcmp(reply, "PHONE\tnone")) {
        if (ui.phoneAction[0] && (ui.screen == PhoneConfirm || ui.screen == Uninstall)) {
            ui.phoneAction[0] = 0;
            ui.showResult(text(T_PHONE_REQUEST_ENDED), text(T_RETRY_FROM_PHONE));
        }
        return true;
    }
    char copy[256];
    if (strlen(reply) >= sizeof(copy)) {
        return false;
    }
    strcpy(copy, reply);
    char *fields[5];
    fields[0] = copy;
    size_t count = 1;
    for (char *p = copy; *p; ++p) {
        if (*p == '\t') {
            if (count == 5) {
                return false;
            }
            *p = 0;
            fields[count++] = p + 1;
        }
    }
    unsigned char id[16];
    if (count != 5 || strcmp(fields[0], "PHONE") || nbt_hex_decode(fields[2], id, 16) ||
        !fields[3][0] || strlen(fields[3]) > 32) {
        return false;
    }
    const bool pair = !strcmp(fields[1], "pair");
    if (!pair && strcmp(fields[1], "remove")) {
        return false;
    }
    if (pair) {
        if (strlen(fields[4]) != 6 || strspn(fields[4], "0123456789") != 6) {
            return false;
        }
        if (ui.screen != Pairing && ui.screen != PhoneConfirm) {
            return true;
        }
    } else if (!nbt_token_valid(fields[4], 48, 0) || !strcmp(fields[4], "manager")) {
        return false;
    }
    if (!strcmp(ui.phoneAction, fields[2])) {
        return true;
    }
    strcpy(ui.phoneAction, fields[2]);
    strcpy(ui.phoneName, fields[3]);
    ui.phonePair = pair;
    if (pair) {
        strcpy(ui.phoneCode, fields[4]);
        ui.go(PhoneConfirm);
    } else {
        strcpy(ui.phoneApp, fields[4]);
        strcpy(ui.settingsApp, fields[4]);
        ui.go(Uninstall);
    }
    return true;
}
} // namespace
int phoneReply(UiController &ui, int kind, const char *reply, uint64_t now) {
    if (kind >= R_SETUP) {
        return setupReply(ui, kind, reply, now);
    }
    bool ok = true;
    if (kind == R_PHONE_STATUS) {
        ok = pending(ui, reply);
        if (ok && ui.screen == Apps && !ui.queued && !ui.holding) {
            ui.queued = R_STATUS;
        }
    } else if (kind == R_PHONES) {
        if (ReplyParser::isError(reply)) {
            ui.showResult(text(T_CANNOT_READ_PHONES), "");
            return 0;
        }
        ok = parsePhones(ui, reply);
        if (ok) {
            ui.go(Phones);
        }
    } else if (kind == R_PHONE_OPEN && !strcmp(reply, "OK")) {
        ui.go(Pairing);
        ui.pairingDeadlineMs = now + kPairingMs;
        ui.pairingLeftS = kPairingMs / 1000;
    } else if (kind == R_PHONE_FORGET && !strcmp(reply, "OK")) {
        ui.queued = R_PHONES;
    } else if (kind == R_PHONE_YES || kind == R_PHONE_NO) {
        if (!strcmp(reply, "OK paired") || !strcmp(reply, "OK removed") ||
            !strcmp(reply, "OK cancelled")) {
            ui.showResult(text(kind == R_PHONE_NO ? T_REQUEST_REJECTED
                               : ui.phonePair     ? T_PHONE_PAIRED
                                                  : T_APP_UNINSTALLED),
                          "");
        } else if (ReplyParser::isError(reply)) {
            ui.showResult(text(T_PHONE_ACTION_FAILED), text(T_RETRY_FROM_PHONE));
        } else {
            ok = false;
        }
        ui.phoneAction[0] = 0;
        ui.queued = R_CANCEL;
    } else if (ReplyParser::isError(reply)) {
        ui.showResult(text(T_PHONE_ACTION_UNAVAILABLE), "");
    } else {
        ok = false;
    }
    if (ok) {
        return 0;
    }
    ui.fail(1, text(T_WORKER_PROTOCOL_ERROR));
    return -1;
}
} // namespace gui
} // namespace nbt
