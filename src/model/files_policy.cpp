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

/* Whether text[0, length) is made of characters a stored name may hold. */
bool validChars(const char *text, size_t length) {
    for (size_t i = 0; i < length;) {
        const size_t n = nbt_files_char_length(text + i, length - i);
        if (!n) {
            return false;
        }
        i += n;
    }
    return true;
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
    // Two separators: extensions | largest file | quota, then "|folders" or nothing.
    size_t bars[3] = {0, 0, 0}, found = 0;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] == '|') {
            if (found == 3) {
                return -1;
            }
            bars[found++] = i;
        }
    }
    static const char kFolders[] = "folders=";
    const size_t quotaEnd = found == 3 ? bars[2] : length;
    if (found < 2 || !nbt::model::parseExtensions(text, bars[0], &policy) ||
        !nbt::model::parseNumber(text + bars[0] + 1, bars[1] - bars[0] - 1, NBT_FILES_FILE_MIB_MAX,
                                 &policy.file_mib) ||
        !nbt::model::parseNumber(text + bars[1] + 1, quotaEnd - bars[1] - 1,
                                 NBT_FILES_QUOTA_MIB_MAX, &policy.quota_mib) ||
        policy.quota_mib < policy.file_mib) {
        return -1;
    }
    if (found == 3) {
        // "folders=<ext>,...": the declared extensions that make a folder one game.
        const char *marks = text + bars[2] + 1;
        const size_t n = length - bars[2] - 1;
        nbt_files_policy listed;
        memset(&listed, 0, sizeof(listed));
        if (n <= sizeof(kFolders) - 1 || memcmp(marks, kFolders, sizeof(kFolders) - 1) ||
            !nbt::model::parseExtensions(marks + sizeof(kFolders) - 1, n - sizeof(kFolders) + 1,
                                         &listed)) {
            return -1;
        }
        for (unsigned i = 0; i < listed.count; ++i) {
            unsigned j = 0;
            while (j < policy.count && strcmp(policy.extensions[j], listed.extensions[i])) {
                ++j;
            }
            if (j == policy.count) {
                return -1; // only a declared extension marks a game folder
            }
            policy.folder_marks |= static_cast<uint16_t>(1u << j);
        }
    }
    if (out) {
        *out = policy;
    }
    return 0;
}

size_t nbt_files_char_length(const char *s, size_t left) {
    if (!s || !left) {
        return 0;
    }
    const unsigned char c = static_cast<unsigned char>(s[0]);
    if (c < 0x80) {
        return c >= 32 && c <= 126 && !strchr("/\\:*?\"<>|", c) ? 1 : 0;
    }
    size_t n;
    uint32_t point;
    if (c >= 0xc2 && c <= 0xdf) {
        n = 2;
        point = c & 0x1fu;
    } else if (c >= 0xe0 && c <= 0xef) {
        n = 3;
        point = c & 0x0fu;
    } else if (c >= 0xf0 && c <= 0xf4) {
        n = 4;
        point = c & 0x07u;
    } else {
        return 0;
    }
    if (left < n) {
        return 0;
    }
    for (size_t i = 1; i < n; ++i) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        if ((b & 0xc0u) != 0x80u) {
            return 0;
        }
        point = (point << 6) | (b & 0x3fu);
    }
    static const uint32_t kLeast[5] = {0, 0, 0x80, 0x800, 0x10000};
    const bool ok = point >= kLeast[n] && point >= 0xa0 && (point < 0xd800 || point > 0xdfff) &&
                    point <= 0x10ffff;
    return ok ? n : 0;
}

int nbt_files_name_valid(const char *name) {
    if (!name) {
        return 0;
    }
    const size_t length = nbt::model::boundedLength(name, NBT_FILES_NAME_MAX + 1);
    if (!length || length > NBT_FILES_NAME_MAX || name[0] == '.' || name[0] == ' ' ||
        name[length - 1] == '.' || name[length - 1] == ' ' ||
        !nbt::model::validChars(name, length)) {
        return 0;
    }
    const char *dot = strrchr(name, '.');
    return dot != NULL && dot != name && dot[1] != 0;
}

int nbt_files_folder_valid(const char *name) {
    if (!name) {
        return 0;
    }
    const size_t length = nbt::model::boundedLength(name, NBT_FILES_FOLDER_MAX + 1);
    return length && length <= NBT_FILES_FOLDER_MAX && name[0] != '.' && name[0] != ' ' &&
           name[length - 1] != '.' && name[length - 1] != ' ' &&
           nbt::model::validChars(name, length);
}

int nbt_files_policy_marks_folder(const nbt_files_policy *policy, const char *name) {
    if (!policy || !policy->folder_marks || !nbt_files_policy_accepts(policy, name)) {
        return 0;
    }
    const char *extension = strrchr(name, '.') + 1;
    for (unsigned i = 0; i < policy->count; ++i) {
        const char *declared = policy->extensions[i];
        size_t k = 0;
        while (declared[k] && extension[k] &&
               (extension[k] == declared[k] || (extension[k] >= 'A' && extension[k] <= 'Z' &&
                                                extension[k] - 'A' + 'a' == declared[k]))) {
            ++k;
        }
        if (!declared[k] && !extension[k]) {
            return (policy->folder_marks >> i) & 1u;
        }
    }
    return 0;
}

int nbt_files_listed_name_valid(const char *name) {
    if (nbt_files_name_valid(name)) {
        return 1;
    }
    const size_t length = name ? nbt::model::boundedLength(name, NBT_FILES_FOLDER_MAX + 2) : 0;
    if (length < 2 || length > NBT_FILES_FOLDER_MAX + 1 || name[length - 1] != '/') {
        return 0;
    }
    char folder[NBT_FILES_FOLDER_MAX + 1];
    memcpy(folder, name, length - 1);
    folder[length - 1] = 0;
    return nbt_files_folder_valid(folder);
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
