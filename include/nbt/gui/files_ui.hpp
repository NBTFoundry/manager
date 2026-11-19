// SPDX-License-Identifier: MPL-2.0
/* An application's files on the car (capability `files`, docs/files-bridge.md):
 * Settings > Files lists them by page with their usage, deletes one after a
 * No/Yes, and imports them from a USB stick (scan, then all or one). The
 * worker does the work (FILES, src/worker/ipc_files.cpp); the GUI polls its
 * progress once a second. Nothing here asks the phone. */
#ifndef NBT_GUI_FILES_UI_HPP
#define NBT_GUI_FILES_UI_HPP

#include "nbt/gui/ui_controller.hpp"

namespace nbt {
namespace gui {

/* Keys on Settings' Files entry and the files screens; false: not theirs. */
bool filesPress(UiController &ui, Key key);
bool filesRequest(const UiController &ui, int kind, Request &request);
int filesReply(UiController &ui, int kind, const char *reply, uint64_t now);
void filesView(const UiController &ui, UiView &view);
size_t filesItemCount(const UiController &ui);
/* A USB scan or import under way on its screen: FILES USB, each poll. */
bool filesPolling(const UiController &ui);
/* "12,3 MB" in the current language. */
void formatSize(uint64_t bytes, char *out, size_t size);
/* The text for a refusal or stop reason the worker names. */
const char *filesReasonText(const char *reason);

} // namespace gui
} // namespace nbt

#endif
