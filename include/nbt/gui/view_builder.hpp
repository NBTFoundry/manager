#ifndef NBT_GUI_VIEW_BUILDER_HPP
#define NBT_GUI_VIEW_BUILDER_HPP

#include "nbt/gui/ui_controller.hpp"

namespace nbt {
namespace gui {

class ViewBuilder {
public:
    static void formatText(char *out, size_t size, const char *format, ...);
    static core::Status build(const UiController &ui, UiView &view);
};

} // namespace gui
} // namespace nbt

#endif
