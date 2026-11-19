#ifndef NBT_APP_FAULTS_H
#define NBT_APP_FAULTS_H

#include "nbt/fs.h"

#define NBT_APP_FAULT_LIMIT 3u

#ifdef __cplusplus
extern "C" {
#endif

/* Missing record means zero faults; malformed or unsafe records fail closed
 * (returns -1). */
int nbt_app_fault_load(nbt_fs *fs, const char *app, unsigned *faults);
/* Non-zero when the application has reached NBT_APP_FAULT_LIMIT or its fault
 * record is unreadable/malformed. */
int nbt_app_fault_quarantined(nbt_fs *fs, const char *app);
/* Increments the fault counter (capped at NBT_APP_FAULT_LIMIT) under app tx lock. */
int nbt_app_fault_record(nbt_fs *fs, const char *app);
/* Resets the fault counter to zero (no-op when absent or already zero). */
int nbt_app_fault_clear(nbt_fs *fs, const char *app);

#ifdef __cplusplus
}
#endif
#endif
