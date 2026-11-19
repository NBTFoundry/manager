/* At an opening, before anything runs: a manager release a paired phone sent
 * and the driver approved (src/worker/manager_upload.cpp) is staged as an
 * installer Update would stage it, then supervised on trial as usual. The
 * request is removed before staging, so a release that cannot be staged is
 * tried once, never at every opening. */
#include "nbt/core/text.hpp"
#include "nbt/log.h"
#include "nbt/manager_stage.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

int nbt_manager_stage_requested(nbt_fs *fs) {
    char text[128];
    char error[160];
    unsigned char digest[32];
    const char prefix[] = "NBTSTAGE1\nsha256=";
    const int present = fs ? nbt_fs_exists(fs, NBT_FS_MANAGER_STAGE_REQUEST, "manager", NULL) : -1;
    if (present <= 0) {
        return present;
    }
    const int fd = nbt_fs_open(fs, NBT_FS_MANAGER_STAGE_REQUEST, "manager", NULL, NBT_FS_READ);
    ssize_t length = fd >= 0 ? read(fd, text, sizeof(text) - 1) : -1;
    if (fd >= 0) {
        close(fd);
    }
    const int removed = nbt_fs_unlink(fs, NBT_FS_MANAGER_STAGE_REQUEST, "manager", NULL);
    text[length > 0 ? length : 0] = 0;
    const size_t prefixLength = sizeof(prefix) - 1;
    if (removed || length != static_cast<ssize_t>(prefixLength + 65) ||
        strncmp(text, prefix, prefixLength) || text[prefixLength + 64] != '\n' ||
        !nbt::core::hexDecode(nbt::core::StringRef(text + prefixLength, 64),
                              nbt::core::Span<uint8_t>(digest, sizeof(digest)))
             .ok()) {
        nbt_logf("phone update request unreadable; ignored");
        return -1;
    }
    if (nbt_manager_stage(fs, 0, 0, digest, error, sizeof(error))) {
        nbt_logf("phone update not staged: %s", error);
        return -1;
    }
    nbt_logf("phone update staged");
    return 1;
}
