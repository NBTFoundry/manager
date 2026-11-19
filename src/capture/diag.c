/* Lock-free shared-memory diagnostic ring reader (zero heap, plugin safe). */
#include "nbt/capture/diag.h"

#include "nbt/capture/control.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

int nbt_capture_diag_read(const char *path, nbt_capture_diag_shm *out) {
    const int fd = nbt_capture_open_shmem_checked(path, 0);
    unsigned char *dst = (unsigned char *)out;
    size_t got = 0;
    if (!out || fd < 0) {
        if (fd >= 0) {
            close(fd);
        }
        return -1;
    }
    while (got < sizeof(*out)) {
        const ssize_t n = read(fd, dst + got, sizeof(*out) - got);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            close(fd);
            return -1;
        }
        got += (size_t)n;
    }
    if (close(fd)) {
        return -1;
    }
    if (out->magic != NBT_CAPTURE_DIAG_MAGIC || out->version != NBT_CAPTURE_DIAG_VERSION ||
        out->observer_mode > 1u || out->pid <= 0) {
        return -1;
    }
    return 0;
}

void nbt_capture_diag_drain(const nbt_capture_diag_shm *snap, uint32_t *cursor, uint32_t *missed,
                            nbt_capture_diag_sink sink, void *user) {
    uint32_t head, start, seq;
    if (missed) {
        *missed = 0;
    }
    if (!snap || !cursor || !sink || snap->magic != NBT_CAPTURE_DIAG_MAGIC ||
        snap->version != NBT_CAPTURE_DIAG_VERSION) {
        return;
    }
    head = snap->write_seq;
    if (head == *cursor) {
        return;
    }
    if (head < *cursor) {
        /* Writer restarted (e.g., iDrive restarted with new HmiMain3D PID). */
        *cursor = 0;
    }
    if (head - *cursor > NBT_CAPTURE_DIAG_SLOTS) {
        start = head - NBT_CAPTURE_DIAG_SLOTS;
        if (missed) {
            *missed = start - *cursor;
        }
    } else {
        start = *cursor;
    }
    for (seq = start + 1u; seq <= head; ++seq) {
        const nbt_capture_diag_slot *slot = &snap->slots[seq % NBT_CAPTURE_DIAG_SLOTS];
        if (slot->seq == seq && slot->seq_end == seq) {
            sink(slot, user);
        }
    }
    *cursor = head;
}
