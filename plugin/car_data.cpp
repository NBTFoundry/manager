// SPDX-License-Identifier: MPL-2.0
#include "car_data.hpp"
#include "nbt/gui/car_data_relay.hpp"
#include "nbt/gui/hmi_hold.hpp"
#include "nbt/log.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
namespace nbt {
namespace plugin {
namespace {
CarData *running = NULL;
/* Subscribed elsewhere in the plugin (phone_runtime.cpp, car_language.cpp). */
const char *const kShared[] = {"driving.speedActual", "hmi.graphicalContext", "vehicle.language"};
bool shared(const char *name) {
    for (size_t i = 0; i < sizeof(kShared) / sizeof(kShared[0]); ++i) {
        if (!strcmp(name, kShared[i])) {
            return true;
        }
    }
    return false;
}
} // namespace

void CarData::start(bmw::PluginApp *instance) {
    memset(slots_, 0, sizeof(slots_));
    app_ = instance;
    nextScan_ = 0;
    full_ = false;
    running = this;
}

void CarData::stop() {
    for (unsigned i = 0; app_ && i < kSlots; ++i) {
        if (slots_[i].subscribed) {
            bmw::BasicString<char> name(slots_[i].name);
            app_->cdsRemovePropertyChangedEventHandler(name);
        }
    }
    memset(slots_, 0, sizeof(slots_));
    app_ = NULL;
    running = NULL;
}

void CarData::want(const char *name) {
    unsigned freeSlot = kSlots;
    for (unsigned i = 0; i < kSlots; ++i) {
        if (slots_[i].used && !strcmp(slots_[i].name, name)) {
            slots_[i].wanted = true;
            return;
        }
        if (!slots_[i].used && freeSlot == kSlots) {
            freeSlot = i;
        }
    }
    if (freeSlot == kSlots) {
        if (!full_) {
            nbt_logf("car data: more than %u names wanted; %s left out",
                     static_cast<unsigned>(kSlots), name);
            full_ = true;
        }
        return;
    }
    Slot &slot = slots_[freeSlot];
    memset(&slot, 0, sizeof(slot));
    slot.owner = this;
    memcpy(slot.name, name, strlen(name) + 1);
    slot.used = slot.wanted = true;
    slot.shared = shared(name);
}

void CarData::poll(uint64_t now) {
    if (!app_ || now < nextScan_) {
        return;
    }
    nextScan_ = now + gui::kCarDataRefreshMs;
    for (unsigned i = 0; i < kSlots; ++i) {
        slots_[i].wanted = false;
    }
    DIR *dir = opendir(gui::kCarDataDir);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            char file[160], names[NBT_CDS_POLICY_MAX + 1];
            nbt_cds_policy policy;
            if (!gui::carDataWantEntry(entry->d_name) ||
                snprintf(file, sizeof(file), "%s/%s", gui::kCarDataDir, entry->d_name) >=
                    static_cast<int>(sizeof(file)) ||
                !gui::carDataWantRead(file, now, names) || nbt_cds_policy_parse(names, &policy)) {
                continue;
            }
            for (unsigned i = 0; i < policy.count; ++i) {
                want(policy.names[i]);
            }
        }
        closedir(dir);
    }
    for (unsigned i = 0; i < kSlots; ++i) {
        Slot &slot = slots_[i];
        if (!slot.used) {
            continue;
        }
        bmw::BasicString<char> name(slot.name);
        if (slot.wanted && !slot.subscribed && !slot.shared) {
            // No value from an earlier run until the car gives one now.
            char value[160];
            if (!gui::carDataValuePath(gui::kCarDataDir, slot.name, value, sizeof(value))) {
                (void)unlink(value);
            }
            bmw::CdsDelegate handler = {&slot, &CarData::onValue};
            app_->cdsAddPropertyChangedEventHandler(name, 0, handler);
            app_->cdsGetPropertyAsync(name, handler);
            slot.subscribed = true;
            nbt_logf("car data %s: subscribed", slot.name);
        } else if (!slot.wanted) {
            if (slot.subscribed) {
                app_->cdsRemovePropertyChangedEventHandler(name);
                nbt_logf("car data %s: unsubscribed", slot.name);
            }
            memset(&slot, 0, sizeof(slot));
            full_ = false;
        }
    }
}

void CarData::publish(Slot &slot, const bmw::BasicString<unsigned short> &value) {
    char text[NBT_CDS_VALUE_MAX + 1], line[NBT_CDS_LINE_MAX + 1], file[160];
    if (!gui::carDataAscii(value.data(), text)) {
        if (!slot.logged) {
            nbt_logf("car data %s: a value that is not short printable ASCII, left out", slot.name);
            slot.logged = true;
        }
        return;
    }
    ++slot.sequence;
    const int n = nbt_cds_value_format(slot.sequence, text, line, sizeof(line));
    if (n < 0 || gui::carDataValuePath(gui::kCarDataDir, slot.name, file, sizeof(file)) ||
        gui::shmemWriteLine(file, line, static_cast<size_t>(n))) {
        return;
    }
    if (!slot.logged) {
        nbt_logf("car data %s = %s", slot.name, text);
        slot.logged = true;
    }
}

bool CarData::onValue(void *object, const bmw::BasicString<unsigned short> &value) {
    Slot &slot = *static_cast<Slot *>(object);
    if (slot.used && slot.wanted && slot.owner) {
        slot.owner->publish(slot, value);
    }
    return true;
}

void CarData::offer(const char *name, const bmw::BasicString<unsigned short> &value) {
    if (!running) {
        return;
    }
    for (unsigned i = 0; i < kSlots; ++i) {
        Slot &slot = running->slots_[i];
        if (slot.used && slot.wanted && slot.shared && !strcmp(slot.name, name)) {
            running->publish(slot, value);
            return;
        }
    }
}
} // namespace plugin
} // namespace nbt
