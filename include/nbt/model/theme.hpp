// SPDX-License-Identifier: MPL-2.0
/* The colours of the manager's own screen: one theme per make of car, its
 * Theme setting and the make the head unit reports.
 *
 * The setting is manager/theme: one line, "auto" or a theme code ("mini\n").
 * Absent or invalid, it is "auto". Automatic follows the car's make, read
 * from the head unit's CarPlay configuration (kCarMakePath: the logo label,
 * then the logo file, then the manufacturer, in its [General] section; a
 * MINI keeps BMW as its manufacturer, so the logo comes first). A car that
 * has no such file, or names another make, shows the BMW theme. */
#ifndef NBT_MODEL_THEME_HPP
#define NBT_MODEL_THEME_HPP

#include "nbt/fs.h"

#include <stddef.h>

namespace nbt {
namespace model {

enum Theme { ThemeBmw, ThemeMini, ThemeSupra };
static const unsigned kThemeCount = 3;
/* The setting's value for "follow the car's make"; the others are a Theme. */
static const int kThemeAuto = -1;
/* At most this many bytes in manager/theme, newline included. */
static const size_t kThemeFileMax = 8;
/* At most this much of the car's file is read; [General] is its first section. */
static const size_t kCarMakeFileMax = 2048;
/* Where the head unit keeps it (written by its connectivity process). */
static const char kCarMakePath[] = "/var/opt/conn/carplay/carplay.ini";

/* "bmw", "mini", "supra"; NULL for a value out of range. */
const char *themeCode(int theme);
/* "BMW", "MINI", "Supra": names, the same in every language. */
const char *themeName(int theme);
/* "auto" or a code to kThemeAuto or a Theme; false for anything else. */
bool themeSettingParse(const char *text, int &choice);
/* "auto" or the code. */
const char *themeSettingCode(int choice);
/* The make in the car's CarPlay configuration (`size` bytes, not
 * terminated); false when it names none of ours. */
bool themeFromCarConfig(const char *text, size_t size, Theme &theme);
/* What the screen shows: the chosen theme, or the car's (`car`: a Theme, or
 * -1 when unknown), else BMW. */
Theme themeEffective(int choice, int car);
/* manager/theme: kThemeAuto when absent or invalid; -1 (errno set) only when
 * the file exists and cannot be read. */
int themeSettingLoad(nbt_fs *fs, int &choice);
/* The car's make from the file at `path`: a Theme, or -1 when the file is
 * absent, unreadable or names none of ours. */
int themeCarRead(const char *path);

} // namespace model
} // namespace nbt

#endif
