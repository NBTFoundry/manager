// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_PHONE_WAKE_HPP
#define NBT_GUI_PHONE_WAKE_HPP
#include <stddef.h>
#include <stdint.h>
namespace nbt {
namespace gui {
static const char kPhoneWakePath[] = "/dev/shmem/nbt-manager.wake";
static const char kDoorLivePath[] = "/dev/shmem/nbt-manager.door";
/* Marks that this boot's automatic application starts were asked for. */
static const char kAutostartBootPath[] = "/dev/shmem/nbt-manager.autostart";
/* Set by `--autostart` when an open manager session holds the manager
 * operation lock: that session's worker starts the applications instead
 * (dev122: the manager opened before the boot's automatic starts, which then
 * gave up). */
static const char kAutostartHandoverPath[] = "/dev/shmem/nbt-manager.autostart-pending";
enum PhoneWakeState { WakeOpening, WakeOpen, WakeBusy, WakeMoving, WakeOff };
struct PhoneWake {
    char id[33];
    uint64_t when;
    PhoneWakeState state;
};
/* A record shared by the plugin and the manager: a private regular file of
 * this user with one name, opened without following links (created when
 * `writing`); -1 for anything else. */
int privateRecordOpen(const char *path, bool writing);
bool phoneDoorClaim(const char *path);
/* True once per boot: /dev/shmem starts empty after a reboot, but keeps the
 * record across iDrive restarts. */
bool bootOnceClaim(const char *path);
bool phoneDoorReset(const char *path);
/* A one-byte flag record: set writes `value` ('1' to '9'); take gives it once
 * after a set (and writes "0"), else 0. */
bool flagRecordSet(const char *path, char value = '1');
char flagRecordTakeValue(const char *path);
inline bool flagRecordTake(const char *path) {
    return flagRecordTakeValue(path) == '1';
}
/* kAutostartHandoverPath (tests: NBT_TEST_AUTOSTART_HANDOVER). */
const char *autostartHandoverPath();
/* An App shortcut pressed while a manager session holds the manager
 * operation lock: its number, for that session's worker (shortcut_dispatch). */
static const char kShortcutHandoverPath[] = "/dev/shmem/nbt-manager.shortcut-pending";
/* kShortcutHandoverPath (tests: NBT_TEST_SHORTCUT_HANDOVER). */
const char *shortcutHandoverPath();
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
