#ifndef NBT_UTIL_H
#define NBT_UTIL_H

#include <stddef.h>
#include <stdint.h>

enum { NBT_TOKEN_UPPER = 1, NBT_TOKEN_DOT = 2 };

int nbt_token_valid(const char *s, size_t max, unsigned flags);
int nbt_parse_u64(const char *s, uint64_t *value, int allow_leading_zero);
int nbt_hex_decode(const char *s, unsigned char *out, size_t n);
int nbt_hex(const unsigned char *bytes, size_t n, char *out, size_t out_size);
/* Consumes one exact name=value\n field in a mutable, NUL-terminated buffer. */
char *nbt_field(char **cursor, const char *name);
int nbt_random(void *buffer, size_t size);
int nbt_constant_equal(const void *a, const void *b, size_t size);
void nbt_clear(void *buffer, size_t size);
int nbt_monotonic_ms(uint64_t *value);
/* Executables that own/reap children call this before spawning. An ignored
 * SIGCHLD survives exec (e.g. from HMI), which otherwise auto-reaps children. */
int nbt_child_reaping_init(void);

#endif
