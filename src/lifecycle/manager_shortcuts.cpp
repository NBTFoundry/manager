// SPDX-License-Identifier: MPL-2.0
/* manager/shortcuts, read: the applications iDrive's App shortcut entries
 * open (nbt/manager_settings.h; written by SET manager shortcut<n>). */
#include "nbt/manager_settings.h"
#include "nbt/util.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

extern "C" {

int nbt_manager_shortcuts_load(nbt_fs *fs, char apps[NBT_SHORTCUTS][NBT_ID_MAX + 1]) {
    for (unsigned i = 0; i < NBT_SHORTCUTS; ++i) {
        apps[i][0] = 0;
    }
    const int fd = nbt_fs_open(fs, NBT_FS_MANAGER_SHORTCUTS, "manager", NULL, NBT_FS_READ);
    if (fd < 0) {
        return errno == ENOENT ? 0 : -1;
    }
    char text[NBT_SHORTCUTS * (NBT_ID_MAX + 1) + 1];
    ssize_t n;
    do {
        n = read(fd, text, sizeof(text));
    } while (n < 0 && errno == EINTR);
    close(fd);
    if (n < 0) {
        return -1;
    }
    // One line per entry; anything malformed counts as no application.
    size_t at = 0;
    for (unsigned i = 0; i < NBT_SHORTCUTS && at < static_cast<size_t>(n); ++i) {
        const char *end =
            static_cast<const char *>(memchr(text + at, '\n', static_cast<size_t>(n) - at));
        if (!end) {
            break;
        }
        const size_t length = static_cast<size_t>(end - (text + at));
        if (length && length <= NBT_ID_MAX) {
            memcpy(apps[i], text + at, length);
            apps[i][length] = 0;
            if (!nbt_token_valid(apps[i], NBT_ID_MAX, 0)) {
                apps[i][0] = 0;
            }
        }
        at += length + 1;
    }
    return 0;
}

} // extern "C"
