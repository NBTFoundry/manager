// SPDX-License-Identifier: MPL-2.0
/* Whether the capture shim keeps the controller's keys from iDrive for this
 * GUI's capture, from the shim's diagnostic ring: a note for the manager's
 * list when it does not, logged when it changes. */
#ifndef NBT_GUI_CAPTURE_WATCH_HPP
#define NBT_GUI_CAPTURE_WATCH_HPP

#include "nbt/capture/protocol.h"

#include <stdint.h>

namespace nbt {
namespace gui {

class CaptureWatch {
public:
    enum State { Unknown, Blocking, Escaped, Observer, NotLoaded };

    CaptureWatch();
    /* Reads the ring at most once a second while capture is wanted; true
     * when the note changed. `path` NULL: never checked (host tests). */
    bool poll(const char *path, uint64_t generation, bool wanted, uint64_t nowMs);
    /* "" while the keys are kept from iDrive (or nothing is known). */
    const char *note() const;
    /* The keys' state in a few words, as logged ("kept from iDrive"). */
    const char *describe() const;
    /* The state a ring snapshot shows for `generation`; the GUI's own pid
     * check is `alive`. */
    static State classify(const nbt_capture_diag_shm *snap, uint64_t generation, bool alive);

private:
    State state_;
    uint64_t nextMs_;
};

} // namespace gui
} // namespace nbt
#endif
