/* Non-blocking diagnostic ring drain called on the plugin's slow poll timer. */
#ifndef NBT_PLUGIN_CAPTURE_DIAG_HPP
#define NBT_PLUGIN_CAPTURE_DIAG_HPP

#include "nbt/capture/diag.h"
#include "nbt/log.h"

#include <stdio.h>

namespace nbt {
namespace plugin {

struct CaptureDiagLogState {
    uint32_t cursor;
    bool initialized;
    unsigned logFile; /* nbt_log_file() when the context was last logged */
};

inline const char *captureDiagKind(unsigned kind) {
    switch (kind) {
    case NBT_CAPTURE_DIAG_INIT:
        return "shim loaded";
    case NBT_CAPTURE_DIAG_OBSERVE:
        return "forwarded";
    case NBT_CAPTURE_DIAG_SUPPRESS:
        return "held";
    case NBT_CAPTURE_DIAG_ESCAPE:
        return "escape";
    case NBT_CAPTURE_DIAG_EXPIRED:
        return "heartbeat expired";
    case NBT_CAPTURE_DIAG_HOLD_DROPPED:
        return "BACK hold dropped";
    default:
        return "?";
    }
}

/* `user` non-NULL: replayed into a new log file, already logged before. */
inline void captureDiagSink(const nbt_capture_diag_slot *slot, void *user) {
    if (!slot) {
        return;
    }
    if (slot->kind == NBT_CAPTURE_DIAG_UNRESOLVED) {
        nbt_logf("capture shim: real DSIClientDataResponse not found; iDrive's DSI data is lost "
                 "(remove the capture shim)");
        return;
    }
    char held[24] = "";
    if (slot->kind == NBT_CAPTURE_DIAG_ESCAPE || slot->kind == NBT_CAPTURE_DIAG_HOLD_DROPPED) {
        snprintf(held, sizeof(held), " BACK held %s%u.%u s", slot->detail == 255 ? ">=" : "",
                 slot->detail / 10u, slot->detail % 10u);
    }
    nbt_logf("capture diag%s kind=%u (%s) fblock=0x%02x fkt=0x%04x inst=0x%02x op=0x%02x "
             "gen=%llu ts=%llu%s",
             user ? " (earlier)" : "", static_cast<unsigned>(slot->kind),
             captureDiagKind(slot->kind), static_cast<unsigned>(slot->fblock),
             static_cast<unsigned>(slot->fkt), static_cast<unsigned>(slot->inst),
             static_cast<unsigned>(slot->op), static_cast<unsigned long long>(slot->generation),
             static_cast<unsigned long long>(slot->timestamp_ms), held);
}

inline void pollCaptureDiag(CaptureDiagLogState &state) {
    if (!nbt_log_enabled()) {
        return;
    }
    nbt_capture_diag_shm snap;
    if (nbt_capture_diag_read(NBT_CAPTURE_DIAG_PATH, &snap) != 0) {
        return;
    }
    const unsigned file = nbt_log_file();
    if (!state.initialized || state.logFile != file) {
        nbt_logf("capture shim active pid=%d observer=%u", static_cast<int>(snap.pid),
                 static_cast<unsigned>(snap.observer_mode));
        if (state.initialized && state.cursor <= snap.write_seq) {
            // A new log file (switched off and on, or rotated): what the ring
            // still holds of what was logged before, so this file has it too.
            nbt_capture_diag_shm before = snap;
            before.write_seq = state.cursor;
            uint32_t from = 0;
            nbt_capture_diag_drain(&before, &from, NULL, &captureDiagSink, &state);
        }
        state.initialized = true;
        state.logFile = file;
    }
    uint32_t missed = 0;
    nbt_capture_diag_drain(&snap, &state.cursor, &missed, &captureDiagSink, NULL);
    if (missed > 0) {
        nbt_logf("capture diag dropped %u ring records", static_cast<unsigned>(missed));
    }
}

} // namespace plugin
} // namespace nbt
#endif
