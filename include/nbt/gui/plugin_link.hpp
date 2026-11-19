#ifndef NBT_GUI_PLUGIN_LINK_HPP
#define NBT_GUI_PLUGIN_LINK_HPP

#include "nbt/gui/hmi_hold.hpp"

#include <stddef.h>
#include <stdint.h>

namespace nbt {
namespace gui {

/* The GUI's side of the plugin's two /dev/shmem files: the liveness line that
 * keeps iDrive held, and the plugin's signals, each taken once and in order.
 * A signal already there at start() is stale; so is a line whose sequence is
 * not newer. */
class PluginLink {
public:
    PluginLink() : signalPath_(NULL), livenessPath_(NULL), sequence_(0), beatMs_(0), have_(false) {
    }
    /* Takes the signal baseline, then says the GUI runs: the plugin's first
     * "show" can follow at once. False when the liveness line was not written. */
    bool start(const char *signalPath, const char *livenessPath, uint64_t nowMs);
    /* Rewrites the liveness line every 500 ms; false when it could not. */
    bool beat(uint64_t nowMs);
    bool next(HoldSignal &signal);

private:
    const char *signalPath_;
    const char *livenessPath_;
    uint64_t sequence_;
    uint64_t beatMs_;
    bool have_;
};

} // namespace gui
} // namespace nbt

#endif
