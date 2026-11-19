// SPDX-License-Identifier: MPL-2.0
#include "nbt/gui/files_ui.hpp"

#include "nbt/gui/reply_parser.hpp"
#include "nbt/gui/view_builder.hpp"
#include "nbt/util.h"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace gui {
namespace {

const unsigned kAll = ~0u;

bool usbListed(const FilesState &f) {
    return (f.usbPhase == UsbScanned || f.usbPhase == UsbImported || f.usbPhase == UsbFailed) &&
           f.usbCount;
}

bool usbMore(const FilesState &f) {
    return f.usbOffset + f.usbShown < f.usbCount;
}

void firstPage(UiController &ui) {
    ui.files.next = false;
    ui.files.pageNumber = 0;
    ui.queued = R_FILES;
}

/* Splits a reply copy at its tabs; the field count, or -1 past `max`. */
int fields(char *copy, char **out, int max) {
    int count = 0;
    for (char *p = copy;; ++p) {
        if (count == max) {
            return -1;
        }
        out[count++] = p;
        p = strchr(p, '\t');
        if (!p) {
            return count;
        }
        *p = 0;
    }
}

bool number(const char *text, uint64_t max, uint64_t *out) {
    return text[0] && !nbt_parse_u64(text, out, 0) && *out <= max;
}

/* FILES\t<count>\t<used>\t<quota>\t<free>\t<more>, then name and size per file. */
bool parsePage(FilesState &f, const char *reply) {
    char copy[kReplyMax], *field[6 + 2 * kFilesPage + 1];
    uint64_t v[5];
    if (strlen(reply) >= sizeof(copy)) {
        return false;
    }
    strcpy(copy, reply);
    const int n = fields(copy, field, static_cast<int>(sizeof(field) / sizeof(field[0])));
    if (n < 6 || (n - 6) % 2 || strcmp(field[0], "FILES") ||
        !number(field[1], NBT_FILES_COUNT_MAX, &v[0]) || !number(field[2], UINT64_MAX, &v[1]) ||
        !number(field[3], UINT64_MAX, &v[2]) || !number(field[4], UINT64_MAX, &v[3]) ||
        !number(field[5], 1, &v[4])) {
        return false;
    }
    const unsigned shown = static_cast<unsigned>(n - 6) / 2;
    for (unsigned i = 0; i < shown; ++i) {
        uint64_t size = 0;
        if (!nbt_files_name_valid(field[6 + 2 * i]) ||
            !number(field[7 + 2 * i], UINT64_MAX, &size)) {
            return false;
        }
        memcpy(f.names[i], field[6 + 2 * i], strlen(field[6 + 2 * i]) + 1);
        f.sizes[i] = size;
    }
    f.count = static_cast<unsigned>(v[0]);
    f.used = v[1];
    f.quota = v[2];
    f.room = v[3];
    f.more = v[4] != 0;
    f.pageCount = shown;
    return true;
}

/* USB\t<phase>\t<reason>\t<done>\t<total>\t<percent>\t<count>, then name,
 * size and stored per file. */
bool parseUsb(FilesState &f, const char *reply) {
    static const char *const kPhases[] = {"idle",      "scanning", "scanned",
                                          "importing", "imported", "failed"};
    char copy[kReplyMax], *field[7 + 3 * kFilesPage + 1];
    uint64_t v[4];
    if (strlen(reply) >= sizeof(copy)) {
        return false;
    }
    strcpy(copy, reply);
    const int n = fields(copy, field, static_cast<int>(sizeof(field) / sizeof(field[0])));
    int phase = -1;
    for (int i = 0; n >= 7 && i < 6; ++i) {
        phase = !strcmp(field[1], kPhases[i]) ? i : phase;
    }
    if (n < 7 || (n - 7) % 3 || strcmp(field[0], "USB") || phase < 0 ||
        strlen(field[2]) >= sizeof(f.usbReason) || !number(field[3], 64, &v[0]) ||
        !number(field[4], 64, &v[1]) || !number(field[5], 100, &v[2]) ||
        !number(field[6], 64, &v[3])) {
        return false;
    }
    const unsigned shown = static_cast<unsigned>(n - 7) / 3;
    for (unsigned i = 0; i < shown; ++i) {
        uint64_t size = 0, stored = 0;
        if (!nbt_files_name_valid(field[7 + 3 * i]) ||
            !number(field[8 + 3 * i], UINT64_MAX, &size) || !number(field[9 + 3 * i], 1, &stored)) {
            return false;
        }
        memcpy(f.usbNames[i], field[7 + 3 * i], strlen(field[7 + 3 * i]) + 1);
        f.usbSizes[i] = size;
        f.usbStored[i] = stored ? 1 : 0;
    }
    f.usbPhase = phase;
    memcpy(f.usbReason, field[2], strlen(field[2]) + 1);
    f.done = static_cast<unsigned>(v[0]);
    f.total = static_cast<unsigned>(v[1]);
    f.percent = static_cast<unsigned>(v[2]);
    f.usbCount = static_cast<unsigned>(v[3]);
    f.usbShown = shown;
    return true;
}

bool pressFiles(UiController &ui, Key key) {
    FilesState &f = ui.files;
    const size_t more = 1 + f.pageCount;
    if (key == KeyBack) {
        if (f.pageNumber) {
            firstPage(ui);
        } else {
            ui.go(Settings);
            ui.focus = ui.filesItem();
        }
    } else if (ui.focus == 0) {
        f.usbPhase = UsbScanning;
        f.usbCount = f.usbOffset = f.usbShown = 0;
        ui.message[0] = 0;
        ui.go(UsbFiles);
        ui.queued = R_FILES_SCAN;
    } else if (ui.focus < more) {
        f.selected = static_cast<unsigned>(ui.focus - 1);
        ui.go(FileDelete);
    } else if (f.more) {
        f.next = true;
        ++f.pageNumber;
        ui.queued = R_FILES;
    }
    return true;
}

bool pressUsb(UiController &ui, Key key) {
    FilesState &f = ui.files;
    if (key == KeyBack) {
        ui.message[0] = 0;
        ui.go(Files);
        firstPage(ui); // what was imported shows
        return true;
    }
    if (!usbListed(f) || ui.focus >= filesItemCount(ui)) {
        return true;
    }
    ui.message[0] = 0;
    if (ui.focus == 0 || ui.focus <= f.usbShown) {
        const unsigned entry = ui.focus ? static_cast<unsigned>(ui.focus - 1) : 0;
        if (ui.focus && f.usbStored[entry]) {
            return true; // already on the car
        }
        f.selected = ui.focus ? f.usbOffset + entry : kAll;
        ui.queued = R_FILES_IMPORT;
        return true;
    }
    f.usbOffset = usbMore(f) ? f.usbOffset + kFilesPage : 0;
    ui.focus = 0;
    ui.queued = R_FILES_USB;
    return true;
}

} // namespace

bool filesPolling(const UiController &ui) {
    return ui.screen == UsbFiles &&
           (ui.files.usbPhase == UsbScanning || ui.files.usbPhase == UsbImporting);
}

size_t filesItemCount(const UiController &ui) {
    const FilesState &f = ui.files;
    switch (ui.screen) {
    case Files:
        return 1 + f.pageCount + (f.more ? 1u : 0u);
    case FileDelete:
        return 2;
    case UsbFiles:
        return usbListed(f) ? 1 + f.usbShown + (usbMore(f) || f.usbOffset ? 1u : 0u) : 0;
    default:
        return 0;
    }
}

bool filesPress(UiController &ui, Key key) {
    if (ui.screen == Settings) {
        if (!ui.filesEntry() || key != KeySelect || ui.focus != ui.filesItem()) {
            return false;
        }
        ui.message[0] = 0;
        firstPage(ui);
        return true;
    }
    if (ui.screen == Files) {
        return pressFiles(ui, key);
    }
    if (ui.screen == UsbFiles) {
        return pressUsb(ui, key);
    }
    if (ui.screen != FileDelete) {
        return false;
    }
    if (key == KeyBack || ui.focus == 0) {
        ui.go(Files);
        ui.focus = 1 + ui.files.selected;
    } else {
        ui.queued = R_FILES_DELETE;
    }
    return true;
}

bool filesRequest(const UiController &ui, int kind, Request &request) {
    char *out = request.command;
    const size_t size = sizeof(request.command);
    const FilesState &f = ui.files;
    const char *app = ui.settingsApp;
    switch (kind) {
    case R_FILES:
        snprintf(out, size, "FILES %s %s", app, f.next ? "next" : "first");
        return true;
    case R_FILES_DELETE:
        snprintf(out, size, "FILES DELETE %s %u", app, f.selected);
        return true;
    case R_FILES_SCAN:
        snprintf(out, size, "FILES SCAN %s", app);
        return true;
    case R_FILES_USB:
        snprintf(out, size, "FILES USB %s %u", app, f.usbOffset);
        return true;
    case R_FILES_IMPORT:
        if (f.selected == kAll) {
            snprintf(out, size, "FILES IMPORT %s all", app);
        } else {
            snprintf(out, size, "FILES IMPORT %s %u", app, f.selected);
        }
        return true;
    default:
        return false;
    }
}

int filesReply(UiController &ui, int kind, const char *reply, uint64_t now) {
    FilesState &f = ui.files;
    const bool error = ReplyParser::isError(reply);
    const char *reason = error && strlen(reply) > 4 ? reply + 4 : "";
    const bool valid = error || (kind == R_FILES       ? parsePage(f, reply)
                                 : kind == R_FILES_USB ? parseUsb(f, reply)
                                                       : !strcmp(reply, "OK"));
    if (!valid) {
        ui.fail(1, text(T_WORKER_PROTOCOL_ERROR));
        return -1;
    }
    switch (kind) {
    case R_FILES:
        if (error) {
            textf(ui.message, sizeof(ui.message), T_SETTING_ROW, text(T_FILES),
                  filesReasonText(reason));
            return 0;
        }
        if (ui.screen != Files) {
            ui.go(Files);
        } else if (ui.focus >= filesItemCount(ui)) {
            ui.focus = 0;
        }
        return 0;
    case R_FILES_DELETE:
        ui.go(Files);
        if (error) {
            textf(ui.message, sizeof(ui.message), T_CANNOT_DELETE_FILE, f.names[f.selected]);
        } else {
            textf(ui.message, sizeof(ui.message), T_FILE_DELETED, f.names[f.selected]);
        }
        firstPage(ui);
        return 0;
    case R_FILES_SCAN:
    case R_FILES_IMPORT:
        if (error && kind == R_FILES_SCAN) { // the screen says why
            f.usbPhase = UsbFailed;
            snprintf(f.usbReason, sizeof(f.usbReason), "%s", reason);
            return 0;
        }
        if (error) {
            ViewBuilder::formatText(ui.message, sizeof(ui.message), "%s", filesReasonText(reason));
            return 0;
        }
        f.usbPhase = kind == R_FILES_SCAN ? UsbScanning : UsbImporting;
        f.done = f.total = f.percent = 0;
        ui.nextPollMs = now; // its progress at once
        return 0;
    default: // R_FILES_USB
        if (error) {
            f.usbPhase = UsbFailed;
            snprintf(f.usbReason, sizeof(f.usbReason), "%s", reason);
        } else if (ui.focus >= filesItemCount(ui)) {
            ui.focus = 0;
        }
        return 0;
    }
}

} // namespace gui
} // namespace nbt
