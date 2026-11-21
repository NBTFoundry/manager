// SPDX-License-Identifier: MPL-2.0
/* manager/native-audio, read: the audio connection applications' sound uses
 * (nbt/manager_settings.h; written by SET manager native_audio). */
#include "nbt/manager_settings.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

extern "C" {

int nbt_manager_native_audio(nbt_fs *fs) {
    const int fd =
        fs ? nbt_fs_open(fs, NBT_FS_MANAGER_NATIVE_AUDIO, "manager", NULL, NBT_FS_READ) : -1;
    if (fd < 0) {
        return 0;
    }
    char text[32];
    ssize_t n;
    do {
        n = read(fd, text, sizeof(text) - 1);
    } while (n < 0 && errno == EINTR);
    close(fd);
    // Anything but the exact word keeps the default connection.
    static const char kMedia[] = "media\n";
    return n == static_cast<ssize_t>(sizeof(kMedia) - 1) &&
                   !memcmp(text, kMedia, sizeof(kMedia) - 1)
               ? 1
               : 0;
}

} // extern "C"
