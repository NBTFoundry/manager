#ifndef NBT_CCS_H
#define NBT_CCS_H

/* Decodes iDrive controller status messages (MOST FBlock 0xE5, op Status) into
 * manager input. Two controller protocols are handled: CCS35up (rotary 0xC46,
 * hard keys 0xC47/0xC4F) and the older CCS (rotary 0xC10, buttons 0xC20).
 * Layouts are reconstructed target assumptions (docs/controller-input.md). */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NBT_INPUT_RELEASE: the controller push (SELECT) was let go, so a host can
 * tell a short push from a held one. */
typedef enum {
    NBT_INPUT_NONE,
    NBT_INPUT_UP,
    NBT_INPUT_DOWN,
    NBT_INPUT_SELECT,
    NBT_INPUT_BACK,
    NBT_INPUT_HOME,
    NBT_INPUT_RELEASE
} nbt_input;

/* Hard key slots: the byte index in CCS35up 0xC47/0xC4F, the sensor bit in
 * older CCS 0xC20. Which slot is MENU or BACK varies by car. */
#define NBT_CCS_SLOTS 7
#define NBT_CCS_INSTANCES_MAX 8
/* A rotary step larger than this is treated as a counter re-base, not input. */
#define NBT_CCS_MAX_STEP 16

/* Defaults (nbt_ccs_defaults) follow docs/controller-input.md; the manager GUI
 * can override them from manager/controller.conf (src/controller_config.c). */
typedef struct {
    uint8_t knob_instance;                    /* CCS35up controller instance, default 0x03 */
    uint8_t ccs_instance;                     /* older CCS controller instance, default 0x01 */
    uint8_t instances[NBT_CCS_INSTANCES_MAX]; /* FBlock 0xE5 instances registered for */
    size_t instance_count;
    int invert;                             /* reverse the rotary direction */
    uint8_t knob_menu_slot, knob_back_slot; /* defaults 0 and 1 */
    uint8_t ccs_menu_slot, ccs_back_slot;   /* defaults 0 and 1 */
    uint8_t release;                        /* unregister the controllers on exit, default 0 */
} nbt_ccs_config;

void nbt_ccs_defaults(nbt_ccs_config *config);
/* Instances unique and including both controller instances; slots below
 * NBT_CCS_SLOTS and MENU distinct from BACK; flags 0 or 1. */
int nbt_ccs_config_valid(const nbt_ccs_config *config);

typedef struct {
    nbt_ccs_config config;
    int position, ccs_position; /* -1 until the first value */
    int push, ccs_push;
    nbt_input tilt, ccs_tilt;
    unsigned char slots[NBT_CCS_SLOTS], ccs_slots[NBT_CCS_SLOTS];
} nbt_ccs;

/* NULL or an invalid config: defaults. */
void nbt_ccs_init(nbt_ccs *ccs, const nbt_ccs_config *config);
/* Feeds one message and writes at most `max` inputs; returns how many. Each
 * input is one action, taken on press: a clockwise detent moves down, a push
 * selects (and its release is reported), up/down tilts move, BACK or a left
 * tilt goes back and MENU goes home. A right tilt and other hard keys are
 * ignored. */
size_t nbt_ccs_feed(nbt_ccs *ccs, unsigned fblock, unsigned inst, unsigned fkt, unsigned op,
                    const unsigned char *data, size_t length, nbt_input *out, size_t max);
/* Forgets positions and held keys after a lost link. */
void nbt_ccs_reset(nbt_ccs *ccs);
/* Non-zero while BACK (hard key or left tilt) is held. */
int nbt_ccs_back_held(const nbt_ccs *ccs);
/* Non-zero while a hard BACK button slot is held (excludes left tilt). */
int nbt_ccs_back_key_held(const nbt_ccs *ccs);

#ifdef __cplusplus
}
#endif
#endif
