// SPDX-License-Identifier: MPL-2.0
#include "nbt/most/ccs.hpp"

#include <string.h>

namespace {

const unsigned kFblockController = 0xe5;
const unsigned kOpStatus = 0xc;

void emit(nbt_input *out, size_t max, size_t *count, nbt_input input, unsigned repeat) {
    while (repeat-- > 0 && *count < max) {
        out[(*count)++] = input;
    }
}

void rotate(nbt_ccs *c, int *last, int position, nbt_input *out, size_t max, size_t *count) {
    if (*last >= 0) {
        int delta = (int)(int16_t)(uint16_t)(position - *last);
        if (c->config.invert) {
            delta = -delta;
        }
        if (delta != 0 && delta <= NBT_CCS_MAX_STEP && delta >= -NBT_CCS_MAX_STEP) {
            emit(out, max, count, delta > 0 ? NBT_INPUT_DOWN : NBT_INPUT_UP,
                 (unsigned)(delta > 0 ? delta : -delta));
        }
    }
    *last = position;
}

void key(unsigned char *state, int pressed, nbt_input input, nbt_input *out, size_t max,
         size_t *count) {
    if (pressed && !*state && input) {
        emit(out, max, count, input, 1);
    }
    *state = (unsigned char)(pressed != 0);
}

void slotKey(unsigned char *slots, size_t slot, unsigned menu, unsigned back, int pressed,
             nbt_input *out, size_t max, size_t *count) {
    unsigned option = 0; // the first slot that is neither MENU nor BACK (nbt/ccs.h)
    while (option == menu || option == back) {
        ++option;
    }
    const nbt_input input = slot == back     ? NBT_INPUT_BACK
                            : slot == menu   ? NBT_INPUT_HOME
                            : slot == option ? NBT_INPUT_OPTION
                                             : NBT_INPUT_NONE;
    key(&slots[slot], pressed, input, out, max, count);
}

void tilt(nbt_input *state, nbt_input direction, nbt_input *out, size_t max, size_t *count) {
    if (direction != *state &&
        (direction == NBT_INPUT_UP || direction == NBT_INPUT_DOWN || direction == NBT_INPUT_BACK)) {
        emit(out, max, count, direction, 1);
    }
    *state = direction;
}

nbt_input clockDirection(unsigned value) {
    if (value == 0 || value > 12) {
        return NBT_INPUT_NONE;
    }
    if (value == 12 || value <= 2) {
        return NBT_INPUT_UP;
    }
    if (value <= 5) {
        return NBT_INPUT_SELECT;
    }
    if (value <= 8) {
        return NBT_INPUT_DOWN;
    }
    return NBT_INPUT_BACK;
}

void resetState(nbt_ccs *c) {
    if (!c) {
        return;
    }
    c->position = c->ccs_position = -1;
    c->push = c->ccs_push = 0;
    c->tilt = c->ccs_tilt = NBT_INPUT_NONE;
    memset(c->slots, 0, sizeof(c->slots));
    memset(c->ccs_slots, 0, sizeof(c->ccs_slots));
}

void initState(nbt_ccs *c, const nbt_ccs_config *config) {
    if (!c) {
        return;
    }
    memset(c, 0, sizeof(*c));
    if (nbt::most::Ccs::valid(config)) {
        c->config = *config;
    } else {
        nbt::most::Ccs::defaults(&c->config);
    }
    resetState(c);
}

size_t feedState(nbt_ccs *c, unsigned fblock, unsigned inst, unsigned fkt, unsigned op,
                 const unsigned char *d, size_t n, nbt_input *out, size_t max) {
    size_t count = 0;
    if (!c || !d || !out || fblock != kFblockController || op != kOpStatus) {
        return 0;
    }
    if (inst == c->config.knob_instance && (fkt == 0xc46 || fkt == 0xc47 || fkt == 0xc4f)) {
        if (fkt == 0xc46) {
            if (n < 5) {
                return 0;
            }
            rotate(c, &c->position, d[1] << 8 | d[2], out, max, &count);
            tilt(&c->tilt, clockDirection(d[4]), out, max, &count);
            if (d[3] && !c->push) {
                emit(out, max, &count, NBT_INPUT_SELECT, 1);
            } else if (!d[3] && c->push) {
                emit(out, max, &count, NBT_INPUT_RELEASE, 1);
            }
            c->push = d[3] != 0;
        } else {
            for (size_t i = 0; i < n && i < NBT_CCS_SLOTS; ++i) {
                slotKey(c->slots, i, c->config.knob_menu_slot, c->config.knob_back_slot, d[i] != 0,
                        out, max, &count);
            }
        }
        return count;
    }
    if (inst != c->config.ccs_instance) {
        return 0;
    }
    if (fkt == 0xc10) {
        const size_t need = inst == 1 ? 7 : 8;
        if (n < need) {
            return 0;
        }
        if (d[2] == 0xff || d[need - 2] != 0x1e) {
            c->ccs_position = -1;
            return 0;
        }
        if (d[need - 1]) {
            c->ccs_position = d[3] << 8 | d[4];
            return 0;
        }
        rotate(c, &c->ccs_position, d[3] << 8 | d[4], out, max, &count);
    } else if (fkt == 0xc20 && n >= 7) {
        const unsigned status = d[3], cluster = d[5], sensor = d[6];
        const int down = status == 1 || status == 2;
        if (d[2] == 0xff || status > 2) {
            return 0;
        }
        if (cluster == 0x1e && sensor == 1) {
            if (down && !c->ccs_push) {
                emit(out, max, &count, NBT_INPUT_SELECT, 1);
            } else if (!down && c->ccs_push) {
                emit(out, max, &count, NBT_INPUT_RELEASE, 1);
            }
            c->ccs_push = down;
        } else if (cluster == 0x1d) {
            const nbt_input direction = !down       ? NBT_INPUT_NONE
                                        : d[4] == 1 ? NBT_INPUT_UP
                                        : d[4] == 4 ? NBT_INPUT_DOWN
                                        : d[4] == 8 ? NBT_INPUT_BACK
                                        : d[4] == 2 ? NBT_INPUT_SELECT
                                                    : NBT_INPUT_NONE;
            tilt(&c->ccs_tilt, direction, out, max, &count);
        } else if (cluster == 0) {
            for (size_t i = 0; i < NBT_CCS_SLOTS; ++i) {
                if (sensor == 1u << i) {
                    slotKey(c->ccs_slots, i, c->config.ccs_menu_slot, c->config.ccs_back_slot, down,
                            out, max, &count);
                }
            }
        }
    }
    return count;
}

} // namespace

namespace nbt {
namespace most {

void Ccs::defaults(nbt_ccs_config *config) {
    static const uint8_t instances[] = {0x01, 0x02, 0x03, 0x05, 0x10, 0x12, 0x14};
    if (!config) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->knob_instance = 0x03;
    config->ccs_instance = 0x01;
    memcpy(config->instances, instances, sizeof(instances));
    config->instance_count = sizeof(instances);
    config->knob_menu_slot = config->ccs_menu_slot = 0;
    config->knob_back_slot = config->ccs_back_slot = 1;
}

bool Ccs::valid(const nbt_ccs_config *c) {
    int knob = 0, ccs = 0;
    if (!c || !c->instance_count || c->instance_count > NBT_CCS_INSTANCES_MAX ||
        (c->invert != 0 && c->invert != 1) || c->release > 1 ||
        c->knob_menu_slot >= NBT_CCS_SLOTS || c->knob_back_slot >= NBT_CCS_SLOTS ||
        c->ccs_menu_slot >= NBT_CCS_SLOTS || c->ccs_back_slot >= NBT_CCS_SLOTS ||
        c->knob_menu_slot == c->knob_back_slot || c->ccs_menu_slot == c->ccs_back_slot) {
        return false;
    }
    for (size_t i = 0; i < c->instance_count; ++i) {
        for (size_t j = 0; j < i; ++j) {
            if (c->instances[i] == c->instances[j]) {
                return false;
            }
        }
        if (c->instances[i] == c->knob_instance) {
            knob = 1;
        }
        if (c->instances[i] == c->ccs_instance) {
            ccs = 1;
        }
    }
    return knob && ccs;
}

Ccs::Ccs(const nbt_ccs_config *config) {
    initState(&state_, config);
}
void Ccs::reset() {
    resetState(&state_);
}
size_t Ccs::feed(unsigned fblock, unsigned inst, unsigned fkt, unsigned op,
                 const unsigned char *data, size_t length, nbt_input *out, size_t max) {
    return feedState(&state_, fblock, inst, fkt, op, data, length, out, max);
}

bool Ccs::backHeld() const {
    return nbt_ccs_back_held(&state_) != 0;
}
bool Ccs::backKeyHeld() const {
    return nbt_ccs_back_key_held(&state_) != 0;
}

} // namespace most
} // namespace nbt

extern "C" {

void nbt_ccs_defaults(nbt_ccs_config *config) {
    nbt::most::Ccs::defaults(config);
}
int nbt_ccs_config_valid(const nbt_ccs_config *config) {
    return nbt::most::Ccs::valid(config) ? 1 : 0;
}
void nbt_ccs_init(nbt_ccs *ccs, const nbt_ccs_config *config) {
    initState(ccs, config);
}
void nbt_ccs_reset(nbt_ccs *ccs) {
    resetState(ccs);
}

int nbt_ccs_back_held(const nbt_ccs *ccs) {
    if (!ccs) {
        return 0;
    }
    return ccs->slots[ccs->config.knob_back_slot] || ccs->ccs_slots[ccs->config.ccs_back_slot] ||
           ccs->tilt == NBT_INPUT_BACK || ccs->ccs_tilt == NBT_INPUT_BACK;
}
int nbt_ccs_back_key_held(const nbt_ccs *ccs) {
    if (!ccs) {
        return 0;
    }
    return (ccs->slots[ccs->config.knob_back_slot] || ccs->ccs_slots[ccs->config.ccs_back_slot])
               ? 1
               : 0;
}
size_t nbt_ccs_feed(nbt_ccs *ccs, unsigned fblock, unsigned inst, unsigned fkt, unsigned op,
                    const unsigned char *data, size_t length, nbt_input *out, size_t max) {
    return feedState(ccs, fblock, inst, fkt, op, data, length, out, max);
}

} // extern "C"
