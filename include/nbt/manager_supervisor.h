#ifndef NBT_MANAGER_SUPERVISOR_H
#define NBT_MANAGER_SUPERVISOR_H

#include "nbt/fs.h"

/* Holds the manager operation lock for the lifetime of the supervised GUI. */
int nbt_manager_supervise(nbt_fs *fs);

#endif
