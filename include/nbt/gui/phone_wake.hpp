#ifndef NBT_GUI_PHONE_WAKE_HPP
#define NBT_GUI_PHONE_WAKE_HPP
#include <stddef.h>
#include <stdint.h>
namespace nbt {
namespace gui {
static const char kPhoneWakePath[] = "/dev/shmem/nbt-manager.wake";
static const char kDoorLivePath[] = "/dev/shmem/nbt-manager.door";
enum PhoneWakeState { WakeOpening, WakeOpen, WakeBusy, WakeMoving, WakeOff };
struct PhoneWake {
    char id[33];
    uint64_t when;
    PhoneWakeState state;
};
bool phoneDoorClaim(const char *path);
bool phoneDoorReset(const char *path);
bool phoneWakeParse(const char *text, size_t size, PhoneWake &wake);
bool phoneWakeRead(const char *path, PhoneWake &wake);
bool phoneWakeWrite(const char *path, const PhoneWake &wake);
const char *phoneWakeName(PhoneWakeState state);
PhoneWakeState phoneWakeRule(bool enabled, bool speedKnown, unsigned speed, uint64_t speedMs,
                             bool idriveAlone, bool contextSafe, uint64_t contextMs,
                             bool alreadyOpen, uint64_t now);
bool phoneSpeed(const char *json, unsigned &speed);
bool phoneContextSafe(const char *json);
} // namespace gui
} // namespace nbt
#endif
