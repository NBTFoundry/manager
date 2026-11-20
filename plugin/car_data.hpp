// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PLUGIN_CAR_DATA_HPP
#define NBT_PLUGIN_CAR_DATA_HPP
#include "framework.hpp"
#include "nbt/bridge_hold.h"
#include "nbt/cds_policy.h"
#include <stdint.h>
namespace nbt {
namespace plugin {
/* Car data for applications (capability `car-data`, bridge 1.6;
 * include/nbt/gui/car_data_relay.hpp): every second, the union of the names
 * running hosts want; each subscribed while wanted, its values published as
 * the car gives them. Names another part of the plugin already subscribes to
 * (the phone's speed and screen, iDrive's language) are not subscribed twice:
 * those parts offer their values here. Static storage, no STL. */
class CarData {
public:
    void start(bmw::PluginApp *instance);
    void stop();
    void poll(uint64_t now);
    /* A value of a name the plugin subscribes to for itself. */
    static void offer(const char *name, const bmw::BasicString<unsigned short> &value);

private:
    enum { kSlots = 32 };
    struct Slot {
        CarData *owner;
        char name[NBT_CDS_NAME_MAX + 1];
        bool used, wanted, subscribed, shared, logged;
        uint64_t sequence;
    };
    void want(const char *name);
    void publish(Slot &slot, const bmw::BasicString<unsigned short> &value);
    static bool onValue(void *slot, const bmw::BasicString<unsigned short> &value);

    Slot slots_[kSlots];
    bmw::PluginApp *app_;
    uint64_t nextScan_;
    bool full_;
};
} // namespace plugin
} // namespace nbt
#endif
