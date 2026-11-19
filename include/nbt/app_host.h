#ifndef NBT_APP_HOST_H
#define NBT_APP_HOST_H

/* Applications outlive the manager GUI and its worker. Each running
 * application has a host: the worker binary started with --host <app>,
 * detached from the worker (its own session, not a child). The host holds an
 * exclusive lock on state/<app>/host.lock for its lifetime, recovers an
 * interrupted transaction, then starts the application with the usual
 * readiness and lease contract and watches its heartbeats. It exits when the
 * application exits or breaks the contract, and on SIGTERM stops the
 * application (SIGTERM, then SIGKILL after the grace period) first.
 *
 * "Running" means the host lock is held; the kernel reports the holder's pid,
 * which is the only process the worker ever signals. */

#include "nbt/app_settings.h"
#include "nbt/bridge.h"
#include "nbt/fs.h"
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The worker binary's absolute path, used to start hosts, and (host tests
 * only) the managed root to pass on. Returns -1 for a relative path. */
int nbt_app_host_set_self(const char *path, const char *test_root);
/* Starts a detached host for `app` and returns once it is on its own; whether
 * the application came up is seen through nbt_app_host_pid. */
int nbt_app_host_start(nbt_fs *fs, const char *app);
/* Same detached host, with a durable attempt and health-confirmed boot guard. */
int nbt_app_host_autostart(nbt_fs *fs, const char *app);
/* The running host's pid, 0 when none runs, -1 on error. */
pid_t nbt_app_host_pid(nbt_fs *fs, const char *app);
/* Signals the running host (SIGTERM to stop, SIGKILL as a last resort).
 * Returns 0 when it was signalled or none runs. */
int nbt_app_host_signal(nbt_fs *fs, const char *app, int signal_number);
/* Request show only from a ready host whose child was granted the capability.
 * 0 accepted, 1 running without show (or not yet ready), -1 no host/I/O error.
 * Byte 0 of host.lock is the lifetime write lock; byte 1 is the ready show's
 * read-lock marker. Both belong to the same kernel-reported host PID. */
int nbt_app_host_show(nbt_fs *fs, const char *app);
/* Ready/granted status, checked against the same live host before/after read.
 * 0 valid text, nonzero fallback; text is cleared on every failure. */
int nbt_app_host_status(nbt_fs *fs, const char *app, char text[NBT_BRIDGE_STATUS_MAX + 1]);
/* The one LayerManager surface for applications (display::kAppSurface) goes
 * to one host at a time, held by a write lock on byte 0 of
 * state/manager/surface.lock for its life. Claim returns that lock's
 * descriptor, or -1 (EAGAIN/EACCES: another host has it). Owner returns the
 * holding host's pid, 0 when none, -1 on error. */
int nbt_app_surface_claim(nbt_fs *fs);
pid_t nbt_app_surface_owner(nbt_fs *fs);
/* Claims the surface for `app`'s host into *fd and adds NBT_SURFACE to its
 * environment; when another host has it, *fd is -1 and the application runs
 * without one. -1 only when the environment has no room. */
int nbt_app_surface_grant(nbt_fs *fs, const char *app, nbt_app_environment *environment, int *fd);
/* Drops `entry` from a built environment. */
void nbt_app_environment_remove(nbt_app_environment *environment, const char *entry);
/* The fallback when the host cannot pipe its application's output into the
 * application's log (src/process/app_output.cpp): while the debug log is on,
 * makes it the host's (so its application's) stdout and stderr; otherwise
 * they stay /dev/null. */
void nbt_app_output_to_log(nbt_fs *fs);
/* The host itself: returns its exit status (0 after the application ended,
 * 3 when another host already runs, 1 on errors). */
int nbt_app_host_run(nbt_fs *fs, const char *app);
int nbt_app_host_run_mode(nbt_fs *fs, const char *app, int automatic);

#ifdef __cplusplus
}
#endif
#endif
