// SPDX-License-Identifier: MPL-2.0
/* The signed manifest of a manager release, NBTRLS1 to NBTRLS3
 * (docs/manager-release.md). */
#include "nbt/package/release_manifest.hpp"

#include "nbt/package.h"
#include "nbt/util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace nbt {
namespace package {
namespace {

core::Status failure(const char *message) {
    return core::Status::error(EINVAL, message);
}

const unsigned kFixed = NBT_RELEASE_V1_COMPONENTS;
const char *const kFixedNames[kFixed] = {
    "launcher",        "gui",        "worker",      "plugin_lib",
    "plugin_manifest", "plugin_xml", "plugin_text", "plugin_images"};
const char kCaptureName[] = "capture_lib";
const char kRunnerName[] = "runner";

/* A named component: [a-z][a-z0-9_]{0,23}, none of the fixed roles, unique. */
bool validExtraName(const nbt_release &release, unsigned count, const char *name, size_t size) {
    if (!size || size > NBT_RELEASE_NAME_MAX || name[0] < 'a' || name[0] > 'z') {
        return false;
    }
    for (size_t i = 0; i < size; ++i) {
        const char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    for (unsigned i = 0; i < count; ++i) {
        if (strlen(release.components[i].name) == size &&
            !memcmp(release.components[i].name, name, size)) {
            return false;
        }
    }
    return true;
}

/* `<name>_size=<n>` then `<name>_sha256=<hex>` for component `index`. */
core::Status parseComponent(char **p, nbt_release *out, unsigned index, uint64_t *total) {
    char field[NBT_RELEASE_NAME_MAX + 8];
    const char *name = out->components[index].name;
    uint64_t size = 0;
    snprintf(field, sizeof(field), "%s_size", name);
    char *s = nbt_field(p, field);
    if (!s || nbt_parse_u64(s, &size, 0) || !size || size > NBT_PAYLOAD_MAX ||
        (isElfComponent(*out, index) && size < 52) || *total > NBT_RELEASE_PAYLOAD_MAX - size) {
        return failure("invalid manifest");
    }
    out->components[index].size = size;
    *total += size;
    snprintf(field, sizeof(field), "%s_sha256", name);
    s = nbt_field(p, field);
    if (!s || nbt_hex_decode(s, out->components[index].sha256, 32)) {
        return failure("invalid manifest");
    }
    return core::Status();
}

/* NBTRLS3: the next line names a component as `<name>_size=`. */
bool nextExtraName(const char *p, const nbt_release &release, unsigned count, char *name) {
    const char *eq = strchr(p, '=');
    const char *nl = strchr(p, '\n');
    if (!eq || !nl || eq > nl || eq - p < 6 || memcmp(eq - 5, "_size", 5)) {
        return false;
    }
    const size_t size = static_cast<size_t>(eq - 5 - p);
    if (!validExtraName(release, count, p, size)) {
        return false;
    }
    memcpy(name, p, size);
    name[size] = 0;
    return true;
}

} // namespace

/* The launcher, GUI, worker and plugin library, and the named components this
 * manager knows as programs, are ARM ELF; other named components are only
 * hashed (a later release's own code reads them). */
bool isElfComponent(const nbt_release &release, unsigned index) {
    const char *name = release.components[index].name;
    return index < 4 || !strcmp(name, kCaptureName) || !strcmp(name, kRunnerName);
}

bool mustBeSharedObject(const nbt_release &release, unsigned index) {
    return index == 3 || !strcmp(release.components[index].name, kCaptureName);
}

core::Status parseManifest(char *text, uint8_t format, nbt_release *out, uint64_t *total) {
    char *p = text;
    char *s = nbt_field(&p, "version");
    if (!nbt_token_valid(s, 48, NBT_TOKEN_UPPER | NBT_TOKEN_DOT)) {
        return failure("invalid manifest");
    }
    memcpy(out->version, s, strlen(s) + 1);
    s = nbt_field(&p, "sequence");
    if (!s || nbt_parse_u64(s, &out->sequence, 0) || !out->sequence) {
        return failure("invalid manifest");
    }
    s = nbt_field(&p, "platform");
    if (!s || strcmp(s, "qnx6.5-armv7le")) {
        return failure("invalid manifest");
    }
    s = nbt_field(&p, "compat");
    if (!nbt_token_valid(s, 48, NBT_TOKEN_DOT)) {
        return failure("invalid manifest");
    }
    memcpy(out->compatibility, s, strlen(s) + 1);
    while (!strncmp(p, "bridge=", 7)) {
        s = nbt_field(&p, "bridge");
        if (!s || nbt_bridge_provider_add(&out->bridge, s)) {
            return failure("invalid manifest");
        }
    }
    *total = 0;
    unsigned count = 0;
    for (; count < kFixed; ++count) {
        memcpy(out->components[count].name, kFixedNames[count], strlen(kFixedNames[count]) + 1);
        if (!parseComponent(&p, out, count, total).ok()) {
            return failure("invalid manifest");
        }
    }
    if (format == 2) {
        memcpy(out->components[count].name, kCaptureName, sizeof(kCaptureName));
        if (!parseComponent(&p, out, count++, total).ok()) {
            return failure("invalid manifest");
        }
    }
    while (format == 3 && *p) {
        if (count >= NBT_RELEASE_MAX_COMPONENTS ||
            !nextExtraName(p, *out, count, out->components[count].name) ||
            !parseComponent(&p, out, count++, total).ok()) {
            return failure("invalid manifest");
        }
    }
    out->component_count = static_cast<uint8_t>(count);
    return *p ? failure("invalid manifest") : core::Status();
}

} // namespace package
} // namespace nbt
