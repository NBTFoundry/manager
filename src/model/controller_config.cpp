#include "nbt/model/controller_config.hpp"

#include "nbt/core/unique_fd.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace nbt {
namespace model {
namespace {

const char kTag[] = "NBTCTL1";

enum FieldKey {
    kKeyKnob,
    kKeyCcs,
    kKeyInstances,
    kKeyInvert,
    kKeyKnobMenu,
    kKeyKnobBack,
    kKeyCcsMenu,
    kKeyCcsBack,
    kKeyRelease,
    kKeyCount
};

const char *const kKeyNames[kKeyCount] = {"knob_instance", "ccs_instance",   "instances",
                                          "invert",        "knob_menu_slot", "knob_back_slot",
                                          "ccs_menu_slot", "ccs_back_slot",  "release"};

int hexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool parseInstance(const char *s, size_t n, uint8_t *out) {
    int high = 0;
    int low = 0;
    if (n != 2 || (high = hexDigit(s[0])) < 0 || (low = hexDigit(s[1])) < 0) {
        return false;
    }
    *out = static_cast<uint8_t>((high << 4) | low);
    return true;
}

bool parseDigit(const char *s, size_t n, unsigned max, uint8_t *out) {
    if (n != 1 || s[0] < '0' || static_cast<unsigned>(s[0] - '0') > max) {
        return false;
    }
    *out = static_cast<uint8_t>(s[0] - '0');
    return true;
}

bool parseInstances(const char *s, size_t n, nbt_ccs_config *c) {
    size_t count = 0;
    size_t start = 0;
    for (size_t i = 0; i <= n; ++i) {
        if (i < n && s[i] != ',') {
            continue;
        }
        if (count == NBT_CCS_INSTANCES_MAX ||
            !parseInstance(s + start, i - start, &c->instances[count])) {
            return false;
        }
        ++count;
        start = i + 1;
    }
    c->instance_count = count;
    return true;
}

bool applyField(nbt_ccs_config *c, unsigned key, const char *v, size_t n) {
    uint8_t value = 0;
    switch (key) {
    case kKeyKnob:
        return parseInstance(v, n, &c->knob_instance);
    case kKeyCcs:
        return parseInstance(v, n, &c->ccs_instance);
    case kKeyInstances:
        return parseInstances(v, n, c);
    case kKeyInvert:
        if (!parseDigit(v, n, 1, &value)) {
            return false;
        }
        c->invert = value;
        return true;
    case kKeyKnobMenu:
        return parseDigit(v, n, NBT_CCS_SLOTS - 1, &c->knob_menu_slot);
    case kKeyKnobBack:
        return parseDigit(v, n, NBT_CCS_SLOTS - 1, &c->knob_back_slot);
    case kKeyCcsMenu:
        return parseDigit(v, n, NBT_CCS_SLOTS - 1, &c->ccs_menu_slot);
    case kKeyCcsBack:
        return parseDigit(v, n, NBT_CCS_SLOTS - 1, &c->ccs_back_slot);
    case kKeyRelease:
        return parseDigit(v, n, 1, &c->release);
    default:
        return false;
    }
}

bool validCommentLine(const char *line, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(line[i]);
        if (c < 0x20 || c == 0x7f) {
            return false;
        }
    }
    return true;
}

bool parseConfigText(const char *text, size_t size, nbt_ccs_config *c) {
    const size_t tagLen = sizeof(kTag) - 1;
    unsigned seen = 0;
    if (!text || size > NBT_CONTROLLER_CONFIG_MAX || size <= tagLen ||
        memcmp(text, kTag, tagLen) != 0 || text[tagLen] != '\n' || text[size - 1] != '\n') {
        return false;
    }
    for (size_t pos = tagLen + 1; pos < size;) {
        const char *line = text + pos;
        const char *end = static_cast<const char *>(memchr(line, '\n', size - pos));
        const size_t length = static_cast<size_t>(end - line);
        pos += length + 1;
        if (!length || line[0] == '#') {
            if (!validCommentLine(line, length)) {
                return false;
            }
            continue;
        }
        const char *equals = static_cast<const char *>(memchr(line, '=', length));
        if (!equals) {
            return false;
        }
        const size_t nameLen = static_cast<size_t>(equals - line);
        unsigned key = 0;
        while (key < kKeyCount &&
               (strlen(kKeyNames[key]) != nameLen || memcmp(line, kKeyNames[key], nameLen) != 0)) {
            ++key;
        }
        if (key == kKeyCount || (seen & (1u << key)) != 0 ||
            !applyField(c, key, equals + 1, length - nameLen - 1)) {
            return false;
        }
        seen |= 1u << key;
    }
    return nbt_ccs_config_valid(c) != 0;
}

} // namespace

ControllerConfig::ControllerConfig() {
    nbt_ccs_defaults(&config_);
}

core::Result<ControllerConfig> ControllerConfig::parse(core::Span<const char> text) {
    ControllerConfig result;
    if (!parseConfigText(text.data(), text.size(), &result.config_)) {
        return core::Result<ControllerConfig>::failure(
            core::Status::error(core::kInvalidArgument, "invalid controller configuration"));
    }
    return core::Result<ControllerConfig>::success(result);
}

core::Result<size_t> ControllerConfig::format(core::Span<char> output) const {
    char list[NBT_CCS_INSTANCES_MAX * 3];
    size_t used = 0;
    if (!output.data() || !output.size() || !nbt_ccs_config_valid(&config_)) {
        return core::Result<size_t>::failure(core::Status::error(
            core::kInvalidArgument, "invalid controller configuration or output size"));
    }
    for (size_t i = 0; i < config_.instance_count; ++i) {
        const int n =
            snprintf(list + used, sizeof(list) - used, i ? ",%02x" : "%02x", config_.instances[i]);
        if (n < 0 || static_cast<size_t>(n) >= sizeof(list) - used) {
            return core::Result<size_t>::failure(
                core::Status::error(core::kInvalidArgument, "invalid controller instance list"));
        }
        used += static_cast<size_t>(n);
    }
    const int n = snprintf(
        output.data(), output.size(),
        "NBTCTL1\n# Written by the manager's Settings.\n"
        "knob_instance=%02x\nccs_instance=%02x\ninstances=%s\ninvert=%d\n"
        "knob_menu_slot=%u\nknob_back_slot=%u\nccs_menu_slot=%u\nccs_back_slot=%u\nrelease=%u\n",
        config_.knob_instance, config_.ccs_instance, list, config_.invert, config_.knob_menu_slot,
        config_.knob_back_slot, config_.ccs_menu_slot, config_.ccs_back_slot, config_.release);
    if (n < 0 || static_cast<size_t>(n) >= output.size() ||
        static_cast<size_t>(n) > NBT_CONTROLLER_CONFIG_MAX) {
        return core::Result<size_t>::failure(core::Status::error(
            core::kInvalidArgument, "controller configuration buffer too small"));
    }
    return core::Result<size_t>::success(static_cast<size_t>(n));
}

ControllerConfig::LoadStatus ControllerConfig::loadLegacy(nbt_fs *fs, nbt_ccs_config *config) {
    char text[NBT_CONTROLLER_CONFIG_MAX + 1];
    size_t used = 0;
    if (!config) {
        return LoadInvalid;
    }
    nbt_ccs_defaults(config);
    if (!fs) {
        return LoadInvalid;
    }
    core::UniqueFd fd(
        nbt_fs_open(fs, NBT_FS_MANAGER_CONTROLLER_CONFIG, "manager", NULL, NBT_FS_READ));
    if (!fd.valid()) {
        return errno == ENOENT ? LoadAbsent : LoadInvalid;
    }
    while (used < sizeof(text)) {
        const ssize_t n = ::read(fd.get(), text + used, sizeof(text) - used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            return LoadInvalid;
        }
        if (n == 0) {
            break;
        }
        used += static_cast<size_t>(n);
    }
    return nbt_controller_config_parse(text, used, config) == 0 ? LoadApplied : LoadInvalid;
}

core::Result<ControllerConfig> ControllerConfig::load(const storage::ManagedRoot &root,
                                                      bool *absent) {
    ControllerConfig result;
    const LoadStatus status = loadLegacy(const_cast<nbt_fs *>(root.legacy()), &result.config_);
    if (status == LoadInvalid) {
        return core::Result<ControllerConfig>::failure(
            core::Status::fromErrno("controller configuration load failed"));
    }
    if (absent) {
        *absent = status == LoadAbsent;
    }
    return core::Result<ControllerConfig>::success(result);
}

} // namespace model
} // namespace nbt

int nbt_controller_config_parse(const char *text, size_t size, nbt_ccs_config *config) {
    if (!config) {
        return -1;
    }
    nbt_ccs_defaults(config);
    if (!text) {
        return -1;
    }
    nbt::core::Result<nbt::model::ControllerConfig> parsed =
        nbt::model::ControllerConfig::parse(nbt::core::Span<const char>(text, size));
    if (!parsed.get()) {
        nbt_ccs_defaults(config);
        return -1;
    }
    *config = parsed.value().legacy();
    return 0;
}

int nbt_controller_config_load(nbt_fs *fs, nbt_ccs_config *config) {
    return static_cast<int>(nbt::model::ControllerConfig::loadLegacy(fs, config));
}

int nbt_controller_config_format(const nbt_ccs_config *c, char *out, size_t size) {
    if (!c || !out || !size) {
        return -1;
    }
    nbt::model::ControllerConfig wrapper;
    wrapper.legacy() = *c;
    nbt::core::Result<size_t> formatted = wrapper.format(nbt::core::Span<char>(out, size));
    return formatted.get() ? static_cast<int>(formatted.value()) : -1;
}
