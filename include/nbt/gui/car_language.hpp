/* iDrive's language, from the plugin to the manager GUI. The plugin reads car
 * data vehicle.language ({"language":N}, docs/car-data-properties.tsv) and
 * writes N to a shared-memory record, "NBTLANG1\nvalue=N\n"; the GUI reads
 * it and maps N to a language (nbt/model/language.hpp). Plugin-linked: no
 * STL, no heap. */
#ifndef NBT_GUI_CAR_LANGUAGE_HPP
#define NBT_GUI_CAR_LANGUAGE_HPP

namespace nbt {
namespace gui {

static const char kCarLanguagePath[] = "/dev/shmem/nbt-manager.language";
/* The largest value accepted (iDrive's "invalid" is 255). */
static const unsigned kCarLanguageMax = 255;

/* {"language":N} with N in 0..kCarLanguageMax. */
bool carLanguageParse(const char *json, unsigned &value);
bool carLanguageWrite(const char *path, unsigned value);
/* False when there is no valid record. */
bool carLanguageRead(const char *path, unsigned &value);

} // namespace gui
} // namespace nbt

#endif
