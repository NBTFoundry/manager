// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_GUI_VIEW_BUILDER_HPP
#define NBT_GUI_VIEW_BUILDER_HPP

#include "nbt/gui/ui_controller.hpp"

namespace nbt {
namespace gui {

class ViewBuilder {
public:
    static void formatText(char *out, size_t size, const char *format, ...);
    static core::Status build(const UiController &ui, UiView &view);
    /* A line, on two when it is too long for one. */
    static void addWrapped(UiView *view, const char *text);
};

/* The install review's permission lines (review_permissions.cpp,
 * docs/permissions.md): what a new application uses and can do, or what an
 * update adds. */
void reviewPermissions(const WorkerStatus &review, bool update, UiView *view);

} // namespace gui
} // namespace nbt

#endif
