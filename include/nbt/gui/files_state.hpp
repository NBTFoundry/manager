// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_FILES_STATE_HPP
#define NBT_GUI_FILES_STATE_HPP

/* What the GUI holds of an application's files (files_ui.hpp): the worker's
 * page of stored files and its USB listing (FILES, ipc_files.hpp). */

#include "nbt/files_policy.h"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace gui {

static const size_t kFilesPage = 12; // IpcFiles::kPage

/* As the worker names them (UsbFiles::Phase). */
enum UsbPhase { UsbIdle, UsbScanning, UsbScanned, UsbImporting, UsbImported, UsbFailed };

struct FilesState {
    bool offered; // the application's settings page offers Files
    bool next;    // the next FILES request continues to the next page
    unsigned pageNumber;
    unsigned count; // stored files in all
    uint64_t used, quota, room;
    bool more;
    unsigned pageCount;
    char names[kFilesPage][NBT_FILES_NAME_MAX + 1];
    uint64_t sizes[kFilesPage];
    unsigned selected; // the file to delete, or the USB entry to import (~0: all)
    int usbPhase;
    char usbReason[16];
    unsigned done, total, percent, usbCount, usbOffset, usbShown;
    char usbNames[kFilesPage][NBT_FILES_NAME_MAX + 1];
    uint64_t usbSizes[kFilesPage];
    unsigned char usbStored[kFilesPage];
};

} // namespace gui
} // namespace nbt

#endif
