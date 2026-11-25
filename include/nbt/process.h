// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_H
#define NBT_PROCESS_H

#include "nbt/fs.h"
#include <stdint.h>
#include <sys/types.h>

typedef struct {
    pid_t pid;
    int ready_fd;
    int lease_fd;
    uint64_t started_ms;
    uint64_t ready_ms;
    uint64_t last_heartbeat_ms;
    uint64_t stop_ms;
    int ready;
    int stopping;
    int killed;
    int exit_status;
    int exit_signal;
    /* Why nbt_process_poll returned -1 (NBT_PROCESS_FAULT_*), else 0. */
    int fault;
} nbt_process;

#define NBT_PROCESS_FAULT_NOT_READY 1 /* no ready byte within 5 s */
#define NBT_PROCESS_FAULT_HEARTBEAT 2 /* no heartbeat for 3 s */
#define NBT_PROCESS_FAULT_PROTOCOL 3  /* a byte other than 'R' on the ready pipe */
#define NBT_PROCESS_FAULT_RELEASED 4  /* ready pipe closed or lease dropped, still running */

#define NBT_PROCESS_STOP_GRACE_MS 5000u

/* Only applications implementing the readiness and lease contract are eligible. */
int nbt_process_start(nbt_fs *fs, const char *app, nbt_process *child, uint64_t now_ms);
/* As nbt_process_start, with this environment (NULL-terminated; the default
 * is empty). */
int nbt_process_start_env(nbt_fs *fs, const char *app, char *const *environment, nbt_process *child,
                          uint64_t now_ms);
/* Host-only user launch: pass the fresh status descriptor as child fd 5 and
 * the application's end of the MOST socket as fd 6. Both remain owned by the
 * caller; -1 grants no descriptor. */
int nbt_process_start_host(nbt_fs *fs, const char *app, char *const *environment, int status_fd,
                           int most_fd, nbt_process *child, uint64_t now_ms);
/* The bridge descriptors a host passes to its application, each -1 for none
 * and kept by the caller: status fd 5, most fd 6, gamepad fd 7, controller
 * fd 8, hold fd 9, diag fd 10 and companion fd 11 (docs/app-bridge.md). */
typedef struct {
    int status, most, gamepad, controller, hold, diag, companion;
    int service; /* 1: the application's service, holding service.lease instead of lease */
} nbt_app_channels;
int nbt_process_start_host_channels(nbt_fs *fs, const char *app, char *const *environment,
                                    const nbt_app_channels *channels, nbt_process *child,
                                    uint64_t now_ms);
/* The manager GUI of `slot`, given the launcher's environment and its end of
 * the worker channel as fd 5 (`--worker-fd 5`); the caller keeps worker_fd. */
int nbt_process_start_manager(nbt_fs *fs, unsigned slot, int worker_fd, nbt_process *child,
                              uint64_t now_ms);
/* Returns 1 while running, 0 when exited, -1 on a contract or I/O failure. */
int nbt_process_poll(nbt_process *child, uint64_t now_ms);
/* Healthy: ready, heartbeating and holding its lease for NBT_HEALTHY_MS. An
 * update (application or manager) is confirmed once its process is healthy. */
#define NBT_HEALTHY_MS 10000u
int nbt_process_healthy(const nbt_process *child, uint64_t now_ms);
/* Sends `signal_number` to the child's process group when it leads one, else
 * to the child (as stop does); 0 or -1 with errno. */
int nbt_process_signal(const nbt_process *child, int signal_number);
/* Blocking stop: SIGTERM, then SIGKILL after the grace period. */
int nbt_process_stop(nbt_process *child);
/* Non-blocking stop for event loops. terminate sends SIGTERM once and records
 * the time; reap returns 1 while the child is still exiting, 0 once it has been
 * reaped and its descriptors closed, -1 on failure. reap sends SIGKILL once the
 * grace period has elapsed. */
int nbt_process_terminate(nbt_process *child, uint64_t now_ms);
int nbt_process_reap(nbt_process *child, uint64_t now_ms);

#endif
