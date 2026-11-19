/* Settings > Language: Automatic (iDrive's language) or one of the manager's
 * languages, each listed by its own name, saved with SET manager language
 * (nbt/model/language.hpp) and shown at once. Also the manager's own
 * settings as they read in the current language; an application's settings
 * are shown as its package declares them. */
#ifndef NBT_GUI_LANGUAGE_UI_HPP
#define NBT_GUI_LANGUAGE_UI_HPP

#include "nbt/gui/ui_controller.hpp"

namespace nbt {
namespace gui {

/* Keys for Settings' Language entry and the Language screen; false: not theirs. */
bool languagePress(UiController &ui, Key key);
bool languageRequest(const UiController &ui, int kind, Request &request);
int languageReply(UiController &ui, const char *reply);
void languageView(const UiController &ui, UiView &view);
/* The Settings row: "Language: Automatic (Deutsch)". */
void languageEntry(const UiController &ui, char *out, size_t size);
const char *settingLabel(const UiController &ui, const nbt_setting &setting);
const char *settingChoice(const UiController &ui, const char *choice);

} // namespace gui
} // namespace nbt

#endif
