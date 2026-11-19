/* The languages of the manager's own screen, its Language setting and what
 * iDrive reports as the car's language.
 *
 * The setting is manager/language: one line, "auto" or a language code
 * ("fr\n"). Absent or invalid, it is "auto". Automatic follows iDrive's
 * language (car data vehicle.language, read by the iDrive plugin); a
 * language the manager does not have, or none reported, shows English. */
#ifndef NBT_MODEL_LANGUAGE_HPP
#define NBT_MODEL_LANGUAGE_HPP

#include "nbt/fs.h"

#include <stddef.h>

namespace nbt {
namespace model {

enum Language { LangEn, LangFr, LangDe, LangIt, LangEs, LangZh, LangJa };
static const unsigned kLanguageCount = 7;
/* The setting's value for "follow iDrive"; the others are a Language. */
static const int kLanguageAuto = -1;
/* At most this many bytes in manager/language, newline included. */
static const size_t kLanguageFileMax = 16;

/* "en", "fr", ...; NULL for a value out of range. */
const char *languageCode(int language);
/* The language's own name ("Deutsch", "日本語"), shown in every language. */
const char *languageName(int language);
/* "auto" or a code to kLanguageAuto or a Language; false for anything else. */
bool languageSettingParse(const char *text, int &choice);
/* "auto" or the code. */
const char *languageSettingCode(int choice);
/* iDrive's vehicle.language number (1 German, 2 and 3 English, 4 and 16
 * Spanish, 5 Italian, 6 and 15 French, 10 and 11 Chinese, 13 Japanese);
 * false for a language the manager does not have. */
bool languageFromCar(unsigned value, Language &language);
/* What the screen shows: the chosen language, or iDrive's (`car`: a
 * Language, or -1 when unknown), else English. */
Language languageEffective(int choice, int car);

/* manager/language: kLanguageAuto when absent or invalid; -1 (errno set)
 * only when the file exists and cannot be read. */
int languageSettingLoad(nbt_fs *fs, int &choice);

} // namespace model
} // namespace nbt

#endif
