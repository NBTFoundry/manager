// SPDX-License-Identifier: MPL-2.0
/* The capture shim's diagnostic ring writer: zero heap, lock held by the caller. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "shim_diag.h"
#include "shim_env.h"

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static nbt_capture_diag_shm *g_diag = NULL;

uint8_t nbt_shim_tenths(uint64_t ms) {
    return ms / 100u > 255u ? 255u : (uint8_t)(ms / 100u);
}

int nbt_shim_diag_ready(void) {
    return g_diag != NULL;
}

void nbt_shim_diag_record(uint8_t kind, uint8_t fblock, uint16_t fkt, uint8_t inst, uint8_t op,
                          uint64_t ts, uint64_t gen, uint8_t flags, uint8_t detail) {
    uint32_t seq;
    nbt_capture_diag_slot *slot;
    if (!g_diag) {
        return;
    }
    seq = g_diag->write_seq + 1u;
    slot = &g_diag->slots[seq % NBT_CAPTURE_DIAG_SLOTS];
    slot->seq = seq;
    slot->kind = kind;
    slot->inst = inst;
    slot->op = op;
    slot->flags = flags;
    slot->timestamp_ms = ts;
    slot->generation = gen;
    slot->fkt = fkt;
    slot->fblock = fblock;
    slot->detail = detail;
    slot->seq_end = seq;
    __sync_synchronize();
    g_diag->write_seq = seq;
}

int nbt_shim_diag_open(int observer) {
    struct stat st, named;
    int flags = O_RDWR | O_CREAT | O_NONBLOCK;
    int fd, valid, one_name;
    void *mapped;
    const char *p = nbt_shim_diag_path();
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    fd = open(p, flags, 0600);
    if (fd < 0) {
        return -1;
    }
    valid = !fcntl(fd, F_SETFD, FD_CLOEXEC) && !fstat(fd, &st) && !lstat(p, &named);
    one_name = valid && st.st_nlink == 1;
#ifdef __QNXNTO__
    if (valid && strncmp(p, "/dev/shmem/", 11) == 0) {
        one_name = st.st_nlink >= 1 && st.st_nlink <= 3;
    }
#endif
    valid = valid && S_ISREG(st.st_mode) && S_ISREG(named.st_mode) && st.st_uid == geteuid() &&
            one_name && !(st.st_mode & 0022) && st.st_dev == named.st_dev &&
            st.st_ino == named.st_ino;
    if (!valid || ftruncate(fd, (off_t)sizeof(nbt_capture_diag_shm)) != 0) {
        close(fd);
        return -1;
    }
    mapped = mmap(NULL, sizeof(nbt_capture_diag_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (mapped == MAP_FAILED) {
        return -1;
    }
    g_diag = (nbt_capture_diag_shm *)mapped;
    memset(g_diag, 0, sizeof(*g_diag));
    g_diag->observer_mode = observer ? 1u : 0u;
    g_diag->pid = (int32_t)getpid();
    g_diag->version = NBT_CAPTURE_DIAG_VERSION;
    __sync_synchronize();
    g_diag->magic = NBT_CAPTURE_DIAG_MAGIC;
    nbt_shim_diag_record(NBT_CAPTURE_DIAG_INIT, 0, 0, 0, 0, nbt_shim_monotonic_ms(), 0,
                         observer ? 1u : 0u, 0);
    return 0;
}
