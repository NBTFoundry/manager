#ifndef NBT_APP_REMOVE_H
#define NBT_APP_REMOVE_H

#include "nbt/fs.h"

/* nbt_app_remove() results; -1 is a failure with errno set. */
enum {
    NBT_APP_REMOVED = 0,
    NBT_APP_REMOVE_RUNNING = 1, /* its host or the application holds its locks */
    NBT_APP_REMOVE_BUSY = 2,    /* an update, recovery or manager operation is under way */
    NBT_APP_REMOVE_MISSING = 3  /* none of its paths exists */
};

/* Uninstalls one application (never "manager"), as approved in the manager's
 * GUI. Holds the manager operation lock and the application's host lock,
 * transaction lock and lease, so it never removes a running application or
 * one being updated, then removes all its files (nbt_fs_app_remove). A
 * removal cut short by power loss leaves the application off the list and
 * completes when run again. */
int nbt_app_remove(nbt_fs *fs, const char *app);

#endif
