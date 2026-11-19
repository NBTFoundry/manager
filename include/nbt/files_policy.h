// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_FILES_POLICY_H
#define NBT_FILES_POLICY_H

/* The files an application accepts (capability `files`, bridge 1.5),
 * declared in its signed package as one `bridge-files=` line. Canonical text:
 *
 *   <ext>[,<ext>...]|<largest file, MiB>|<quota, MiB>
 *
 * 1-16 unique extensions of 1-8 `[a-z0-9]`, a largest file of 1-4096 MiB and
 * a quota of 1-65536 MiB, at least the largest file; numbers in canonical
 * decimal. See docs/files-bridge.md. Allocation free. */

#include <stddef.h>
#include <stdint.h>

#define NBT_FILES_POLICY_MAX 160 /* text bytes, without the terminator */
#define NBT_FILES_EXTENSIONS 16
#define NBT_FILES_EXTENSION_MAX 8
#define NBT_FILES_FILE_MIB_MAX 4096u
#define NBT_FILES_QUOTA_MIB_MAX 65536u
/* A stored file's name: 1-96 bytes (see nbt_files_name_valid). */
#define NBT_FILES_NAME_MAX 96
/* Files one application may keep. */
#define NBT_FILES_COUNT_MAX 512

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned count;
    char extensions[NBT_FILES_EXTENSIONS][NBT_FILES_EXTENSION_MAX + 1];
    uint32_t file_mib;
    uint32_t quota_mib;
} nbt_files_policy;

/* 0 for a valid, canonical policy (`out` may be NULL to validate only). */
int nbt_files_policy_parse(const char *text, nbt_files_policy *out);
/* Whether `name` is a valid stored name: printable ASCII without
 * / \ : * ? " < > |, not starting with '.' or ' ', not ending with '.' or
 * ' ', with an extension after its last '.'. */
int nbt_files_name_valid(const char *name);
/* Whether `name` is valid and its extension (any case) is in the policy. */
int nbt_files_policy_accepts(const nbt_files_policy *policy, const char *name);
/* The policy's limits in bytes. */
uint64_t nbt_files_policy_file_bytes(const nbt_files_policy *policy);
uint64_t nbt_files_policy_quota_bytes(const nbt_files_policy *policy);

#ifdef __cplusplus
}
#endif

#endif
