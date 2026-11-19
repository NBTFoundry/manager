#ifndef NBT_MANAGER_PLUGIN_H
#define NBT_MANAGER_PLUGIN_H
#include "nbt/fs.h"
int nbt_manager_plugin_preflight(nbt_hmi_fs *hmi, int release_fd, int key_fd, const char *compat,
                                 const unsigned char digest[32], char *error, unsigned error_size);
/* Explicit first registration of the five signed plugin roles. */
int nbt_manager_plugin_install(nbt_hmi_fs *hmi, int release_fd, int key_fd, const char *compat,
                               const unsigned char digest[32], char *error, unsigned error_size);
#endif
