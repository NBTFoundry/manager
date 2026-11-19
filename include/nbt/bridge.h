#ifndef NBT_BRIDGE_H
#define NBT_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

/* Bounded, allocation-free declarations. Unknown optional capabilities are
 * valid, so an older implementation can grant only the intersection. */
#define NBT_BRIDGE_CAPS_MAX 255
#define NBT_BRIDGE_NAME_MAX 24
#define NBT_BRIDGE_VERSIONS_MAX 2
/* Equal to NBT_MOST_POLICY_MAX (include/nbt/most_policy.h). */
#define NBT_BRIDGE_MOST_MAX 640
/* Formatted requirement: three lines of at most 300 bytes and the MOST line. */
#define NBT_BRIDGE_TEXT_MAX 1600

typedef struct {
    uint16_t major, minor;
    char uses[NBT_BRIDGE_CAPS_MAX + 1];
    char optional[NBT_BRIDGE_CAPS_MAX + 1];
    /* The MOST policy (capability `most`), or empty. Only with `most` declared. */
    char most[NBT_BRIDGE_MOST_MAX + 1];
} nbt_bridge_requirement;

typedef struct {
    uint16_t major, minor;
    char caps[NBT_BRIDGE_CAPS_MAX + 1];
} nbt_bridge_version;

typedef struct {
    unsigned count;
    nbt_bridge_version versions[NBT_BRIDGE_VERSIONS_MAX];
} nbt_bridge_provider;

int nbt_bridge_version_parse(const char *text, uint16_t *major, uint16_t *minor);
int nbt_bridge_caps_valid(const char *text);
void nbt_bridge_caps_without(const char *caps, const char *name, char out[NBT_BRIDGE_CAPS_MAX + 1]);
int nbt_bridge_cap_has(const char *list, const char *name);
/* Optional ordered fields: <prefix>bridge, bridge-uses, bridge-optional, then
 * bridge-most when the application declares `most` (required then). nbt_field
 * consumes input in place. Missing bridge means no dependency. */
int nbt_bridge_requirement_parse(char **text, const char *prefix, nbt_bridge_requirement *out);
int nbt_bridge_requirement_format(const nbt_bridge_requirement *value, const char *prefix,
                                  char *out, size_t size);
/* Release line: bridge=<major>.<minor>|<comma separated capabilities>.
 * Majors must increase; no duplicates, at most current and previous major. */
int nbt_bridge_provider_add(nbt_bridge_provider *out, const char *text);
void nbt_bridge_current(nbt_bridge_provider *out);
/* Required version/capabilities must be served. Optional ones are intersected.
 * available == NULL checks implementation (install); otherwise checks runtime
 * availability as well. Grants never include undeclared capabilities. A MOST
 * policy needs a served `most` even when the capability is optional: only
 * such a manager can store and enforce it. */
int nbt_bridge_check(const nbt_bridge_requirement *need, const nbt_bridge_provider *provider,
                     const char *available, nbt_bridge_version *grant, char *error, unsigned size);
/* Status wire line: canonical uint64, space, 1..24 printable ASCII, newline.
 * Parsing is length based; embedded NULs and trailing bytes are rejected. */
#define NBT_BRIDGE_STATUS_MAX 24
#define NBT_BRIDGE_STATUS_LINE_MAX (20 + 1 + NBT_BRIDGE_STATUS_MAX + 1)
int nbt_bridge_status_parse(const char *line, size_t length, uint64_t *sequence,
                            char text[NBT_BRIDGE_STATUS_MAX + 1]);
int nbt_bridge_status_format(uint64_t sequence, const char *text, char *out, size_t size);
#endif
