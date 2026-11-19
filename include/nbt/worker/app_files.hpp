// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_WORKER_APP_FILES_HPP
#define NBT_WORKER_APP_FILES_HPP

/* An installed application's files (capability `files`), as the phone routes
 * (phone_files.cpp) and the GUI's USB import (ipc_files.cpp) reach them: its
 * signed policy from the installed record, and the files root opened. */

#include "nbt/core/status.hpp"
#include "nbt/files_policy.h"
#include "nbt/storage/files_root.hpp"
#include "nbt/storage/managed_root.hpp"

namespace nbt {
namespace worker {

struct AppFiles {
    storage::FilesRoot root;
    nbt_files_policy policy;
};

/* 0, else what refused it: 400 an invalid name, 404 no such application or
 * none declaring files, 503 the disk unavailable. */
int openAppFiles(storage::ManagedRoot &root, const char *app, AppFiles &out);

/* The word naming a FilesRoot refusal for the phone and the GUI: type, size,
 * quota, count, space, name or unavailable; and its HTTP status. */
const char *filesReason(const core::Status &status);
int filesHttpStatus(const core::Status &status);

} // namespace worker
} // namespace nbt

#endif
