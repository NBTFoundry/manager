#ifndef NBT_GUI_REPLY_PARSER_HPP
#define NBT_GUI_REPLY_PARSER_HPP

#include "nbt/gui/ui_controller.hpp"

namespace nbt {
namespace gui {

class ReplyParser {
public:
    static bool isError(const char *reply);
    static bool samePackage(const WorkerStatus &a, const WorkerStatus &b);
    static int parseStatus(const char *reply, WorkerStatus &status);
    static int parseApps(UiController &ui, const char *reply);
    static int parseRunning(UiController &ui, const char *reply);
    static int parseSettings(UiController &ui, const char *reply);
    static int parseSession(UiController &ui, const char *reply);
};

} // namespace gui
} // namespace nbt

#endif
