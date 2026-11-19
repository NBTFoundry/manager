// SPDX-License-Identifier: MPL-2.0
/* Pure C99 DSI/MOST candidate classifier and 5-second escape evaluator. */
#ifndef NBT_CAPTURE_CLASSIFIER_H
#define NBT_CAPTURE_CLASSIFIER_H

#include "nbt/capture/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int hard_back_held;
    uint64_t back_press_ms;
    uint64_t back_generation; /* the capture generation the hold began in */
    /* Set by nbt_capture_evaluate for the diagnostic record, cleared by the
     * caller: how long BACK was held at an escape or a dropped hold, and a
     * hold dropped because a new generation began (with its generation). */
    uint64_t held_ms;
    int dropped;
    uint64_t dropped_generation;
    uint64_t escaped_session_id;
    uint64_t escaped_generation;
} nbt_capture_escape_state;

void nbt_capture_escape_init(nbt_capture_escape_state *state);

int nbt_capture_classify_bytes(const unsigned char *bytes, size_t length,
                               const nbt_capture_control *ctrl, nbt_capture_packet *out);

int nbt_capture_inspect_stream(const nbt_dsi_c_stream *stream, const nbt_capture_control *ctrl,
                               nbt_capture_packet *out);

nbt_capture_decision nbt_capture_evaluate(const nbt_capture_packet *pkt,
                                          const nbt_capture_control *ctrl, int authorized,
                                          int observer_mode, uint64_t now_ms,
                                          nbt_capture_escape_state *escape);

#ifdef __cplusplus
}
#endif
#endif
