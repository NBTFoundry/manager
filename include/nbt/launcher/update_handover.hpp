// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_LAUNCHER_UPDATE_HANDOVER_HPP
#define NBT_LAUNCHER_UPDATE_HANDOVER_HPP

#include "nbt/storage/managed_root.hpp"

#include <pthread.h>
#include <stdint.h>

namespace nbt {
namespace launcher {

/* Keeps iDrive held on the manager while the launcher replaces the GUI with
 * a manager release the driver just approved (NBT_MANAGER_GUI_EXIT_UPDATE).
 * The plugin (unchanged, plugin/launcher_spawn.cpp) counts the manager as
 * running while gui.lock is locked and the liveness line is fresh; both stay
 * so from the old GUI to the new one:
 * - the GUI locks byte 0 of gui.lock; once a GUI not on trial is ready, the
 *   launcher locks byte 1 (a GUI before 0.5.2 locks the whole file: no
 *   handover then). Any other end of the GUI releases it.
 * - the old GUI hands the liveness line to the launcher's pid as it ends; a
 *   thread rewrites it until the new GUI is about to start, which writes its
 *   own line first thing.
 * Closing any descriptor of a file drops all of the process's locks on it
 * (POSIX): after staging, which opens and closes gui.lock, rearm(). */
class UpdateHandover {
public:
    explicit UpdateHandover(const storage::ManagedRoot &root);
    ~UpdateHandover();

    /* Locks byte 1 once per GUI; true when held. */
    bool arm();
    /* After something in this process closed gui.lock: byte 1 again. */
    bool rearm();
    bool armed() const {
        return armed_;
    }
    void release();
    /* The liveness line with the launcher's pid, now and every 250 ms. */
    void startBeats();
    /* A last line, then no more: the next GUI writes its own. */
    void stopBeats();
    /* Held throughout and no gap in the line the plugin could have seen. */
    bool continuous() const;
    /* Why not continuous(), for the log. */
    const char *broken() const;
    /* No handover: the plugin is to see the manager gone (liveness removed),
     * so that it releases iDrive and the driver opens the new release. */
    void abandon();

private:
    UpdateHandover(const UpdateHandover &);
    UpdateHandover &operator=(const UpdateHandover &);
    static void *beatMain(void *self);
    bool beat();

    const storage::ManagedRoot &root_;
    const char *livenessPath_;
    int fd_;
    bool armed_;
    bool tried_;
    bool brokenLock_;
    pthread_t thread_;
    pthread_mutex_t mutex_;
    bool running_;
    bool stop_;
    bool gap_;
    uint64_t lastBeatMs_;
};

} // namespace launcher
} // namespace nbt

#endif
