// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_IPC_FILES_HPP
#define NBT_WORKER_IPC_FILES_HPP

/* The GUI's FILES commands (capability `files`, PROTOCOL.md "Applications'
 * files"): an application's stored files by page, and the import from a USB
 * stick, chosen on the car.
 *
 *   FILES <app> first|next      FILES\t<count>\t<used>\t<quota>\t<free>\t<more>
 *                               then <name>\t<size> per file of the page
 *   FILES DELETE <app> <i>      the page's file i
 *   FILES SCAN <app>            look for the application's files on USB sticks
 *   FILES USB <app> <offset>    USB\t<phase>\t<reason>\t<done>\t<total>\t
 *                               <percent>\t<count> then <name>\t<size>\t<0|1>
 *   FILES IMPORT <app> all|<i>  copy them (1: stored)
 *
 * Files are addressed by their index in the worker's own listing, so names
 * never cross the command line. Replies fit the GUI's 2 KiB line. */

#include "nbt/storage/managed_root.hpp"
#include "nbt/worker/control_protocol.hpp"
#include "nbt/worker/usb_files.hpp"

#include <stdint.h>

namespace nbt {
namespace worker {

class Worker;
class WebServer;

class IpcFiles {
public:
    static const unsigned kPage = 12;

    IpcFiles(storage::ManagedRoot &root, Worker &worker, WebServer &web, ControlProtocol &control);
    IpcFiles(const IpcFiles &) = delete;
    IpcFiles &operator=(const IpcFiles &) = delete;

    core::Status handle(char *cursor, uint64_t nowMs);

private:
    core::Status page(const char *app, bool next);
    core::Status remove(const char *app, const char *index);
    core::Status start(const char *app, const char *which, bool importing, uint64_t nowMs);
    core::Status usb(const char *app, const char *offset);
    core::Status refuse(const char *reason);

    storage::ManagedRoot &root_;
    Worker &worker_;
    WebServer &web_;
    ControlProtocol &control_;
    UsbFiles usb_;
    char pageApp_[NBT_ID_MAX + 1];
    char after_[NBT_FILES_NAME_MAX + 1]; // the last name of the page before this one
    char names_[kPage][NBT_FILES_NAME_MAX + 1];
    unsigned pageCount_;
};

} // namespace worker
} // namespace nbt

#endif
