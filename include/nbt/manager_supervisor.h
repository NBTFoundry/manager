// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_MANAGER_SUPERVISOR_H
#define NBT_MANAGER_SUPERVISOR_H

#include "nbt/fs.h"

/* The GUI's exit status once the driver approved a manager release from a
 * phone: the launcher stages it and starts it at once, keeping iDrive held
 * on the manager meanwhile (PROTOCOL.md, "Manager updates from a phone"). */
#define NBT_MANAGER_GUI_EXIT_UPDATE 3
/* Set in the environment of the GUI started right after that: it may show
 * itself as soon as it runs, without waiting for a new show signal. */
#define NBT_MANAGER_HANDOVER_ENV "NBT_MANAGER_HANDOVER"

/* Holds the manager operation lock for the lifetime of the supervised GUI. */
int nbt_manager_supervise(nbt_fs *fs);

#endif
