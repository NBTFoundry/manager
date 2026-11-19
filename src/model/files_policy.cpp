// SPDX-License-Identifier: MPL-2.0
#include "nbt/files_policy.h"

#include <string.h>

namespace nbt {
namespace model {
namespace {

/* strlen bounded by `max` (QNX 6.5's libc is not known to export strnlen). */
size_t boundedLength(const char *text, size_t max) {
    size_t n = 0;
    while (n < max && text[n]) {
        ++n;
    }
    return n;
}

/* Canonical decimal in [1, max]: no sign, no leading zero. */
bool parseNumber(const char *p, size_t n, uint32_t max, uint32_t *out) {
    if (!n || n > 5 || p[0] == '0') {
        return false;
    }
    uint32_t value = 0;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] < '0' || p[i] > '9') {
            return false;
        }
        value = value * 10u + static_cast<uint32_t>(p[i] - '0');
    }
    if (value > max) {
        return false;
    }
    *out = value;
    return true;
}

bool extensionChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

bool parseExtensions(const char *p, size_t n, nbt_files_policy *policy) {
    const char *end = p + n;
    while (p < end) {
        const char *comma = static_cast<const char *>(memchr(p, ',', static_cast<size_t>(end - p)));
        const size_t length = static_cast<size_t>((comma ? comma : end) - p);
        if (!length || length > NBT_FILES_EXTENSION_MAX || policy->count >= NBT_FILES_EXTENSIONS) {
            return false;
        }
        for (size_t i = 0; i < length; ++i) {
            if (!extensionChar(p[i])) {
                return false;
            }
        }
        for (unsigned i = 0; i < policy->count; ++i) {
            if (strlen(policy->extensions[i]) == length &&
                !memcmp(policy->extensions[i], p, length)) {
                return false;
            }
        }
        memcpy(policy->extensions[policy->count], p, length);
        policy->extensions[policy->count][length] = 0;
        ++policy->count;
        if (!comma) {
            break;
        }
        p = comma + 1;
        if (p == end) {
            return false; // a trailing comma
        }
    }
    return policy->count != 0;
}

bool forbidden(unsigned char c) {
    return c < 32 || c > 126 || strchr("/\\:*?\"<>|", c) != NULL;
}

} // namespace
} // namespace model
} // namespace nbt

int nbt_files_policy_parse(const char *text, nbt_files_policy *out) {
    nbt_files_policy policy;
    memset(&policy, 0, sizeof(policy));
    if (!text) {
        return -1;
    }
    const size_t length = nbt::model::boundedLength(text, NBT_FILES_POLICY_MAX + 1);
    if (!length || length > NBT_FILES_POLICY_MAX) {
        return -1;
    }
    // Exactly two separators: extensions | largest file | quota.
    size_t bars[2] = {0, 0}, found = 0;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] == '|') {
            if (found == 2) {
                return -1;
            }
            bars[found++] = i;
        }
    }
    if (found != 2 || !nbt::model::parseExtensions(text, bars[0], &policy) ||
        !nbt::model::parseNumber(text + bars[0] + 1, bars[1] - bars[0] - 1, NBT_FILES_FILE_MIB_MAX,
                                 &policy.file_mib) ||
        !nbt::model::parseNumber(text + bars[1] + 1, length - bars[1] - 1, NBT_FILES_QUOTA_MIB_MAX,
                                 &policy.quota_mib) ||
        policy.quota_mib < policy.file_mib) {
        return -1;
    }
    if (out) {
        *out = policy;
    }
    return 0;
}

int nbt_files_name_valid(const char *name) {
    if (!name) {
        return 0;
    }
    const size_t length = nbt::model::boundedLength(name, NBT_FILES_NAME_MAX + 1);
    if (!length || length > NBT_FILES_NAME_MAX || name[0] == '.' || name[0] == ' ' ||
        name[length - 1] == '.' || name[length - 1] == ' ') {
        return 0;
    }
    for (size_t i = 0; i < length; ++i) {
        if (nbt::model::forbidden(static_cast<unsigned char>(name[i]))) {
            return 0;
        }
    }
    const char *dot = strrchr(name, '.');
    return dot != NULL && dot != name && dot[1] != 0;
}

int nbt_files_policy_accepts(const nbt_files_policy *policy, const char *name) {
    if (!policy || !nbt_files_name_valid(name)) {
        return 0;
    }
    const char *extension = strrchr(name, '.') + 1;
    const size_t length = strlen(extension);
    if (length > NBT_FILES_EXTENSION_MAX) {
        return 0;
    }
    char lower[NBT_FILES_EXTENSION_MAX + 1];
    for (size_t i = 0; i <= length; ++i) {
        const char c = extension[i];
        lower[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    for (unsigned i = 0; i < policy->count && i < NBT_FILES_EXTENSIONS; ++i) {
        if (!strcmp(policy->extensions[i], lower)) {
            return 1;
        }
    }
    return 0;
}

uint64_t nbt_files_policy_file_bytes(const nbt_files_policy *policy) {
    return policy ? static_cast<uint64_t>(policy->file_mib) << 20 : 0;
}

uint64_t nbt_files_policy_quota_bytes(const nbt_files_policy *policy) {
    return policy ? static_cast<uint64_t>(policy->quota_mib) << 20 : 0;
}
