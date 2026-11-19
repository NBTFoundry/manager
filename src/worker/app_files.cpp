// SPDX-License-Identifier: MPL-2.0
#include "nbt/worker/app_files.hpp"

#include "nbt/fs.h"
#include "nbt/log.h"
#include "nbt/transaction.h"
#include "nbt/util.h"

#include <errno.h>
#include <string.h>

namespace nbt {
namespace worker {

int openAppFiles(storage::ManagedRoot &root, const char *app, AppFiles &out) {
    nbt_installed installed;
    if (!app || !nbt_token_valid(app, NBT_ID_MAX, 0) || !strcmp(app, "manager")) {
        return 400;
    }
    if (nbt_fs_exists(root.legacy(), NBT_FS_PROFILE, app, NULL) != 1 ||
        nbt_tx_installed(root.legacy(), app, &installed) || !installed.bridge.files[0] ||
        nbt_files_policy_parse(installed.bridge.files, &out.policy)) {
        return 404;
    }
    const core::Status opened = out.root.open(storage::FilesRoot::location());
    if (!opened.ok()) {
        nbt_logf("files %s: %s (errno %d)", app, opened.message(), opened.code());
        return 503;
    }
    return 0;
}

const char *filesReason(const core::Status &status) {
    struct Reason {
        const char *message, *word;
    };
    static const Reason kReasons[] = {
        {"file type not accepted", "type"},
        {"invalid file name", "name"},
        {"file too large", "size"},
        {"quota exceeded", "quota"},
        {"too many files", "count"},
        {"disk full", "space"},
        {"a file of that name cannot be replaced", "name"},
        {"no such file", "missing"},
    };
    for (size_t i = 0; i < sizeof(kReasons) / sizeof(kReasons[0]); ++i) {
        if (status.message() && !strcmp(status.message(), kReasons[i].message)) {
            return kReasons[i].word;
        }
    }
    return "unavailable";
}

int filesHttpStatus(const core::Status &status) {
    const char *word = filesReason(status);
    if (!strcmp(word, "type") || !strcmp(word, "name")) {
        return 400;
    }
    if (!strcmp(word, "missing")) {
        return 404;
    }
    return strcmp(word, "unavailable") ? 413 : 503;
}

} // namespace worker
} // namespace nbt
