/* The language the manager's screen shows (nbt/model/language.hpp): the
 * setting, loaded when the GUI starts and changed by its Language screen, and
 * iDrive's language from the plugin's record (car_language.hpp), checked
 * once a second, so Automatic follows a change made in iDrive. */
#ifndef NBT_GUI_LANGUAGE_WATCH_HPP
#define NBT_GUI_LANGUAGE_WATCH_HPP

#include "nbt/gui/ui_controller.hpp"

#include <stdint.h>

namespace nbt {
namespace gui {

class LanguageWatch {
public:
    LanguageWatch() : path_(NULL), nextMs_(0), seen_(-2) {
    }
    /* `root`: the manager root (manager/language); `carPath`: the plugin's
     * record, NULL for none. Applies the language before the first frame. */
    void start(const char *root, const char *carPath, UiController &ui);
    void poll(UiController &ui, uint64_t nowMs);

private:
    void readCar(UiController &ui);

    const char *path_;
    uint64_t nextMs_;
    int seen_; // the record's value last applied; -1 none, -2 not read yet
};

} // namespace gui
} // namespace nbt

#endif
