// SPDX-License-Identifier: MPL-2.0
/* The files screens' views (files_ui.hpp). */
#include "nbt/gui/files_ui.hpp"

#include "nbt/gui/view_builder.hpp"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace gui {
namespace {

void line(UiView &v, TextId id, const char *a = NULL, const char *b = NULL) {
    if (v.lineCount < kLines) {
        textf(v.lines[v.lineCount++], sizeof(v.lines[0]), id, a, b);
    }
}

void item(UiView &v, ItemStyle style, TextId id, const char *a = NULL, const char *b = NULL) {
    if (v.itemCount < kItems) {
        v.itemStyle[v.itemCount] = static_cast<unsigned char>(style);
        textf(v.items[v.itemCount++], sizeof(v.items[0]), id, a, b);
    }
}

void fileItem(UiView &v, const char *name, uint64_t bytes, bool stored) {
    char size[32];
    formatSize(bytes, size, sizeof(size));
    if (stored) {
        item(v, ItemEntry, T_USB_STORED, name);
    } else {
        item(v, ItemEntry, T_FILE_ROW, name, size);
    }
}

void count(unsigned value, char *out, size_t size) {
    snprintf(out, size, "%u", value);
}

void filesList(const UiController &ui, UiView &v) {
    const FilesState &f = ui.files;
    textf(v.title, sizeof(v.title), T_FILES_TITLE, ui.settingsApp);
    if (ui.message[0]) {
        ViewBuilder::formatText(v.lines[v.lineCount++], sizeof(v.lines[0]), "%s", ui.message);
    }
    if (!f.count) {
        line(v, T_FILES_NONE);
    } else {
        char files[16], used[32], quota[32], space[kTextMax + 1];
        count(f.count, files, sizeof(files));
        formatSize(f.used, used, sizeof(used));
        formatSize(f.quota, quota, sizeof(quota));
        textf(space, sizeof(space), T_FILES_SPACE, used, quota);
        line(v, T_FILES_USAGE, files, space);
    }
    item(v, ItemAction, T_FILES_IMPORT);
    for (unsigned i = 0; i < f.pageCount; ++i) {
        fileItem(v, f.names[i], f.sizes[i], false);
    }
    if (f.more) {
        item(v, ItemAction, T_FILES_MORE);
    }
}

void usbList(const UiController &ui, UiView &v) {
    const FilesState &f = ui.files;
    char a[16], b[16];
    textf(v.title, sizeof(v.title), T_USB_TITLE, ui.settingsApp);
    switch (f.usbPhase) {
    case UsbScanning:
        line(v, T_USB_SCANNING);
        v.busy = 1;
        break;
    case UsbImporting:
        count(f.done < f.total ? f.done + 1 : f.total, a, sizeof(a));
        count(f.total, b, sizeof(b));
        line(v, T_USB_IMPORTING, a, b);
        v.progress = static_cast<int>(f.percent > 100 ? 100 : f.percent);
        v.busy = 1;
        break;
    case UsbFailed:
        if (!strcmp(f.usbReason, "no-usb")) {
            line(v, T_USB_NO_STICK);
        } else {
            line(v, T_USB_STOPPED, filesReasonText(f.usbReason));
        }
        break;
    default:
        if (f.usbPhase == UsbImported) {
            line(v, T_USB_IMPORTED);
        }
        count(f.usbCount, a, sizeof(a));
        if (f.usbCount) {
            line(v, T_USB_FOUND, a);
        } else {
            line(v, T_USB_NONE, ui.settingsApp);
        }
        break;
    }
    if (ui.message[0]) {
        ViewBuilder::formatText(v.lines[v.lineCount++], sizeof(v.lines[0]), "%s", ui.message);
    }
    if (!filesItemCount(ui)) {
        return;
    }
    count(f.usbCount, a, sizeof(a));
    item(v, ItemAction, T_USB_IMPORT_ALL, a);
    for (unsigned i = 0; i < f.usbShown; ++i) {
        fileItem(v, f.usbNames[i], f.usbSizes[i], f.usbStored[i] != 0);
    }
    if (filesItemCount(ui) > 1 + f.usbShown) {
        item(v, ItemAction, T_USB_MORE);
    }
}

} // namespace

void formatSize(uint64_t bytes, char *out, size_t size) {
    char value[32];
    const uint64_t kib = 1024, mib = kib * kib, gib = mib * kib;
    if (bytes < mib) {
        snprintf(value, sizeof(value), "%llu",
                 static_cast<unsigned long long>((bytes + kib - 1) / kib));
        textf(out, size, T_SIZE_KB, value);
        return;
    }
    const uint64_t unit = bytes < gib ? mib : gib;
    const uint64_t tenths = bytes / (unit / 10);
    snprintf(value, sizeof(value), "%llu%s%llu", static_cast<unsigned long long>(tenths / 10),
             text(T_DECIMAL_MARK), static_cast<unsigned long long>(tenths % 10));
    textf(out, size, unit == mib ? T_SIZE_MB : T_SIZE_GB, value);
}

const char *filesReasonText(const char *reason) {
    static const struct {
        const char *word;
        TextId id;
    } kReasons[] = {{"quota", T_REASON_QUOTA},     {"space", T_REASON_SPACE},
                    {"count", T_REASON_COUNT},     {"size", T_REASON_SIZE},
                    {"changed", T_REASON_CHANGED}, {"read", T_REASON_READ},
                    {"busy", T_FILES_BUSY},        {"unavailable", T_FILES_UNAVAILABLE},
                    {"no-usb", T_USB_NO_STICK}};
    for (size_t i = 0; i < sizeof(kReasons) / sizeof(kReasons[0]); ++i) {
        if (reason && !strcmp(reason, kReasons[i].word)) {
            return text(kReasons[i].id);
        }
    }
    return text(T_REASON_OTHER);
}

void filesView(const UiController &ui, UiView &v) {
    if (ui.screen == Files) {
        filesList(ui, v);
    } else if (ui.screen == UsbFiles) {
        usbList(ui, v);
    } else {
        const FilesState &f = ui.files;
        const char *name = f.selected < f.pageCount ? f.names[f.selected] : "";
        textf(v.title, sizeof(v.title), T_FILE_DELETE_QUESTION, name);
        line(v, T_FILE_DELETE_NOTE);
        item(v, ItemEntry, T_NO);
        item(v, ItemEntry, T_YES_DELETE);
    }
}

} // namespace gui
} // namespace nbt
