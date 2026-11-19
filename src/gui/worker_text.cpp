#include "nbt/gui/worker_text.hpp"

#include "nbt/gui/text.hpp"
#include "nbt/gui/view_builder.hpp"

#include <string.h>

namespace nbt {
namespace gui {
namespace {

const TextId kWorkerTexts[] = {T_W_INSTALLING_UPDATE,
                               T_W_CHECKING_HEALTH,
                               T_W_RESTORING,
                               T_W_RESTARTING_APP,
                               T_W_APP_STOPPED,
                               T_W_STOPPING_APP,
                               T_W_UPDATE_INSTALLED,
                               T_W_SETTINGS_NOT_UPDATED,
                               T_W_MANAGER_STARTS,
                               T_W_SCREEN_TAKEN,
                               T_W_ALREADY_RUNNING,
                               T_W_BLOCKED,
                               T_W_APP_INSTALLED,
                               T_W_APP_STARTED,
                               T_W_INSTALLING_APP,
                               T_W_CHECKING_PACKAGE,
                               T_W_PACKAGE_REJECTED,
                               T_W_PACKAGE_VERIFIED,
                               T_W_READY_UPLOAD,
                               T_W_SESSION_CLOSED,
                               T_W_SESSION_EXPIRED,
                               T_W_RECOVERED,
                               T_W_UPDATE_FAILED_RESTORED,
                               T_W_STOPPED_UNEXPECTEDLY,
                               T_W_MANAGER_BUSY};

/* `message` as `english`, with {1} matching any non-empty text, written to `filler`. */
bool matches(const char *message, const char *english, char *filler, size_t size) {
    const char *slot = strstr(english, "{1}");
    if (!slot) {
        filler[0] = 0;
        return !strcmp(message, english);
    }
    const size_t prefix = static_cast<size_t>(slot - english), suffix = strlen(slot + 3);
    const size_t length = strlen(message);
    if (length <= prefix + suffix || strncmp(message, english, prefix) ||
        strcmp(message + length - suffix, slot + 3)) {
        return false;
    }
    const size_t n = length - prefix - suffix;
    if (n >= size) {
        return false;
    }
    memcpy(filler, message + prefix, n);
    filler[n] = 0;
    return true;
}

} // namespace

void workerText(char *out, size_t size, const char *message) {
    if (!out || !size) {
        return;
    }
    if (!message) {
        out[0] = 0;
        return;
    }
    char filler[128];
    for (size_t i = 0; i < sizeof(kWorkerTexts) / sizeof(kWorkerTexts[0]); ++i) {
        if (matches(message, textIn(model::LangEn, kWorkerTexts[i]), filler, sizeof(filler))) {
            textf(out, size, kWorkerTexts[i], filler);
            return;
        }
    }
    ViewBuilder::formatText(out, size, "%s", message);
}

} // namespace gui
} // namespace nbt
