// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_CONTROLLER_CONFIG_H
#define NBT_CONTROLLER_CONFIG_H

/* Optional controller configuration, manager/controller.conf under the manager
 * root, written by the desktop installer or, for invert and release, by the
 * manager's Settings (src/manager_settings.c). Text, at most NBT_CONTROLLER_CONFIG_MAX bytes:
 *
 *   NBTCTL1
 *   # comment
 *   knob_instance=03
 *   ccs_instance=01
 *   instances=01,02,03,05,10,12,14
 *   invert=0
 *   knob_menu_slot=0
 *   knob_back_slot=1
 *   ccs_menu_slot=0
 *   ccs_back_slot=1
 *   release=0
 *
 * The first line is the format tag. Every other line is empty, a comment or
 * key=value with no spaces; keys may come in any order, each at most once, and
 * omitted keys keep their default. Instances are two hexadecimal digits; slots
 * are 0 to 6; invert and release are 0 or 1. Unknown keys and anything else
 * make the whole file invalid. */

#include "nbt/ccs.h"
#include "nbt/fs.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NBT_CONTROLLER_CONFIG_MAX 1024u

/* Parses `size` bytes. On success fills `config` (defaults plus the file's
 * values) and returns 0; returns -1 for invalid text and leaves `config` as the
 * defaults. */
int nbt_controller_config_parse(const char *text, size_t size, nbt_ccs_config *config);
/* Loads the file. Returns 0 when it was applied, 1 when it is absent and -1
 * when it is unreadable or invalid; `config` is always usable (the defaults
 * unless 0 is returned). */
int nbt_controller_config_load(nbt_fs *fs, nbt_ccs_config *config);
/* Formats a valid configuration with every key, as the manager's Settings
 * write it (src/manager_settings.c); returns the length or -1. */
int nbt_controller_config_format(const nbt_ccs_config *config, char *out, size_t size);

#ifdef __cplusplus
}
#endif
#endif
