/* The worker's STATUS messages, shown in the current language. The worker
 * speaks English (PROTOCOL.md); a message it sends as one of the w_ texts of
 * src/gui/text/en.txt, where {1} stands for any text, is shown translated,
 * and any other message as sent. Decisions keep comparing the English. */
#ifndef NBT_GUI_WORKER_TEXT_HPP
#define NBT_GUI_WORKER_TEXT_HPP

#include <stddef.h>

namespace nbt {
namespace gui {

void workerText(char *out, size_t size, const char *message);

} // namespace gui
} // namespace nbt

#endif
