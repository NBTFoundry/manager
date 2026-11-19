/* Non-destructive DSI/MOST stream inspection and 5-second escape tracking. */
#include "nbt/capture/classifier.h"

#include <string.h>

#define DSI_HEADER_BYTES 16u
#define MOST_HEADER_BYTES 32u
#define DSI_INFO_PROCESS_MSG 0x80000001u
#define MOST_FBLOCK_CONTROLLER 0xE5u
#define MOST_OP_STATUS 0x0Cu

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t le16(const unsigned char *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int has_instance(const nbt_capture_control *ctrl, uint8_t inst) {
    size_t i;
    for (i = 0; i < ctrl->instance_count; ++i) {
        if (ctrl->instances[i] == inst) {
            return 1;
        }
    }
    return 0;
}

void nbt_capture_escape_init(nbt_capture_escape_state *state) {
    if (state) {
        memset(state, 0, sizeof(*state));
    }
}

int nbt_capture_classify_bytes(const unsigned char *bytes, size_t length,
                               const nbt_capture_control *ctrl, nbt_capture_packet *out) {
    const unsigned char *p;
    const unsigned char *data;
    uint32_t hdr2, tx_type, variant_tag, variant_value, data_len;
    size_t plen;
    uint8_t knob_inst, ccs_inst, knob_back, ccs_back;
    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (!bytes || length < DSI_HEADER_BYTES + MOST_HEADER_BYTES) {
        return 0;
    }
    hdr2 = le32(bytes + 8u);
    if (hdr2 != DSI_INFO_PROCESS_MSG) {
        return 0;
    }
    p = bytes + DSI_HEADER_BYTES;
    plen = length - DSI_HEADER_BYTES;
    tx_type = le32(p);
    variant_tag = le32(p + 4u);
    variant_value = le32(p + 8u);
    data_len = le32(p + 28u);
    if (variant_tag != 2u || data_len > plen - MOST_HEADER_BYTES) {
        return 0;
    }
    /* Forward segmented or TX status messages unchanged. */
    if ((tx_type == 1u && variant_value >= 19u && variant_value <= 21u) ||
        (variant_value >= 4u && variant_value <= 9u)) {
        return 0;
    }
    out->has_most = 1u;
    out->fblock = p[16];
    out->inst = p[17];
    out->fkt = le16(p + 18u);
    out->op = p[20];
    data = p + MOST_HEADER_BYTES;
    if (out->op != MOST_OP_STATUS) {
        return 0;
    }
    /* The centre stack's favourite keys, any instance: one state byte per key.
     * Presses and releases alike are held back, or a release would run the
     * stored favourite in iDrive. */
    if (out->fblock == NBT_CAPTURE_FBLOCK_CENTER_STACK) {
        if (out->fkt == NBT_CAPTURE_FKT_FAVOURITES && data_len >= 1u) {
            out->is_candidate = 1u;
        }
        return 0;
    }
    if (out->fblock != MOST_FBLOCK_CONTROLLER) {
        return 0;
    }
    knob_inst = ctrl ? ctrl->knob_instance : 0x03u;
    ccs_inst = ctrl ? ctrl->ccs_instance : 0x01u;
    knob_back = ctrl && ctrl->knob_back_slot < NBT_CCS_SLOTS ? ctrl->knob_back_slot : 1u;
    ccs_back = ctrl && ctrl->ccs_back_slot < NBT_CCS_SLOTS ? ctrl->ccs_back_slot : 1u;
    if (ctrl && !has_instance(ctrl, out->inst)) {
        return 0;
    }
    if (out->inst == knob_inst) {
        if (out->fkt == 0x0C46u) {
            if (data_len >= 5u) {
                out->is_candidate = 1u;
            }
        } else if (out->fkt == 0x0C47u || out->fkt == 0x0C4Fu) {
            if (data_len >= 1u) {
                out->is_candidate = 1u;
                if (data_len > knob_back) {
                    out->has_hard_back = 1u;
                    out->hard_back_held = (uint8_t)(data[knob_back] != 0u);
                }
            }
        }
    } else if (out->inst == ccs_inst) {
        if (out->fkt == 0x0C10u) {
            const uint32_t need = out->inst == 1u ? 7u : 8u;
            if (data_len >= need && data[2] != 0xFFu && data[need - 2u] == 0x1Eu) {
                out->is_candidate = 1u;
            }
        } else if (out->fkt == 0x0C20u) {
            if (data_len >= 7u && data[2] != 0xFFu && data[3] <= 2u) {
                const uint8_t cluster = data[5];
                const uint8_t sensor = data[6];
                out->is_candidate = 1u;
                if (cluster == 0u && sensor == (uint8_t)(1u << ccs_back)) {
                    out->has_hard_back = 1u;
                    out->hard_back_held = (uint8_t)(data[3] == 1u || data[3] == 2u);
                }
            }
        }
    }
    return 0;
}

int nbt_capture_inspect_stream(const nbt_dsi_c_stream *stream, const nbt_capture_control *ctrl,
                               nbt_capture_packet *out) {
    uint32_t avail;
    if (!out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (!stream || stream->error_flag != 0u || (stream->flags & 1u) != 0u || !stream->buffer ||
        stream->read_pos > stream->populated || stream->populated > stream->capacity) {
        return 0;
    }
    avail = stream->populated - stream->read_pos;
    return nbt_capture_classify_bytes(stream->buffer + stream->read_pos, avail, ctrl, out);
}

static int escape_latched(const nbt_capture_escape_state *e, const nbt_capture_control *c) {
    if (!e || !c || e->escaped_generation == 0u) {
        return 0;
    }
    if (c->generation < e->escaped_generation) {
        return 1;
    }
    return c->generation == e->escaped_generation;
}

nbt_capture_decision nbt_capture_evaluate(const nbt_capture_packet *pkt,
                                          const nbt_capture_control *ctrl, int authorized,
                                          int observer_mode, uint64_t now_ms,
                                          nbt_capture_escape_state *escape) {
    int latched = escape_latched(escape, ctrl);
    if (!pkt || !pkt->is_candidate || !ctrl || !authorized || latched) {
        if (escape && (!ctrl || !authorized || latched)) {
            escape->hard_back_held = 0;
            escape->back_press_ms = 0;
        }
        return NBT_CAP_DECISION_FORWARD;
    }
    if (!ctrl->escape_enabled) {
        if (escape) {
            escape->hard_back_held = 0;
            escape->back_press_ms = 0;
        }
    } else if (escape) {
        /* A hold that began in another capture generation is not this one's: its
         * release may have gone by uncaptured (car, 2026-11-18: the first message
         * of a new capture escaped at once). */
        if (escape->hard_back_held && escape->back_generation != ctrl->generation) {
            escape->dropped = 1;
            escape->dropped_generation = escape->back_generation;
            escape->held_ms = now_ms >= escape->back_press_ms ? now_ms - escape->back_press_ms : 0;
            escape->hard_back_held = 0;
            escape->back_press_ms = 0;
        }
        /* Evaluate elapsed hold time BEFORE updating or clearing held state so a
         * release arriving after >= 5 seconds still triggers the escape. */
        if (escape->hard_back_held && now_ms >= escape->back_press_ms &&
            now_ms - escape->back_press_ms >= NBT_CAPTURE_ESCAPE_HOLD_MS) {
            escape->held_ms = now_ms - escape->back_press_ms;
            escape->hard_back_held = 0;
            escape->back_press_ms = 0;
            escape->escaped_session_id = ctrl->session_id;
            escape->escaped_generation = ctrl->generation;
            return observer_mode ? NBT_CAP_DECISION_FORWARD : NBT_CAP_DECISION_ESCAPE;
        }
        if (pkt->has_hard_back) {
            if (pkt->hard_back_held && !escape->hard_back_held) {
                escape->hard_back_held = 1;
                escape->back_press_ms = now_ms;
                escape->back_generation = ctrl->generation;
            } else if (!pkt->hard_back_held) {
                escape->hard_back_held = 0;
                escape->back_press_ms = 0;
            }
        }
    }
    return observer_mode ? NBT_CAP_DECISION_FORWARD : NBT_CAP_DECISION_SUPPRESS;
}
