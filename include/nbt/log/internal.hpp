#ifndef NBT_LOG_INTERNAL_HPP
#define NBT_LOG_INTERNAL_HPP

/* Between log.cpp (formatting, size limit, plain path) and log_rotation.cpp
 * (opening and rotation through the manager path policy). */

/* Called when the file in use is full or no longer at the log path. Returns a
 * descriptor for the current log file, or -1. `fd` is the descriptor in use,
 * which the caller closes afterwards. */
typedef int (*nbt_log_rotate_fn)(int fd);

/* Installs an open, checked append descriptor for the file at `path`. Takes
 * ownership of `fd`. */
int nbt_log_attach(const char *component, const char *path, int fd, nbt_log_rotate_fn rotate);
/* 0 for a usable log file: regular, one link, not writable by group or others. */
int nbt_log_checked(int fd);

#endif
