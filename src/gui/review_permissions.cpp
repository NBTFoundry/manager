// SPDX-License-Identifier: MPL-2.0
// The install review's permission lines (docs/permissions.md).
#include "nbt/gui/text.hpp"
#include "nbt/gui/view_builder.hpp"
#include "nbt/permissions.h"

#include <stdio.h>
#include <string.h>

namespace nbt {
namespace gui {
namespace {

struct Label {
    unsigned bit;
    TextId text;
};

const Label kUses[] = {
    {NBT_PERM_BACKGROUND, T_PERM_BACKGROUND}, {NBT_PERM_CAR_DATA, T_PERM_CAR_DATA},
    {NBT_PERM_ENGINE, T_PERM_ENGINE},         {NBT_PERM_FILES, T_PERM_FILES},
    {NBT_PERM_PHONE_NET, T_PERM_PHONE_NET},   {NBT_PERM_MOST_READ, T_PERM_MOST_READ}};
const Label kCan[] = {{NBT_PERM_CLEAR, T_PERM_CLEAR},
                      {NBT_PERM_CONTROL, T_PERM_CONTROL},
                      {NBT_PERM_MOST_ANY, T_PERM_MOST_ANY},
                      {NBT_PERM_MOST_SEND, T_PERM_MOST_SEND}};

// The labels of `bits`, joined; false when none.
bool join(const Label *labels, size_t count, unsigned bits, char *out, size_t size) {
    out[0] = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!(bits & labels[i].bit)) {
            continue;
        }
        // ", " (the catalogue trims the space), or Chinese and Japanese "、".
        const char *separator = text(T_PERM_SEPARATOR);
        const bool spaced = static_cast<unsigned char>(separator[0]) < 0x80;
        const size_t used = strlen(out);
        snprintf(out + used, size - used, "%s%s%s", used ? separator : "",
                 used && spaced ? " " : "", text(labels[i].text));
    }
    return out[0] != 0;
}

} // namespace

void reviewPermissions(const WorkerStatus &review, bool update, UiView *v) {
    unsigned shown = update ? review.newPermissions : review.permissions;
    // Allowed while driving is told with what it allows.
    if (shown & NBT_PERM_DRIVE) {
        shown |= review.permissions & (NBT_PERM_CLEAR | NBT_PERM_CONTROL);
    }
    // Sending any command says it all.
    if (shown & NBT_PERM_MOST_ANY) {
        shown &= ~NBT_PERM_MOST_SEND;
    }
    char list[kTextMax + 1], line[kTextMax + 1];
    if (join(kUses, sizeof(kUses) / sizeof(kUses[0]), shown, list, sizeof(list))) {
        textf(line, sizeof(line), update ? T_PERM_NEW_USES : T_PERM_USES, list);
        ViewBuilder::addWrapped(v, line);
    }
    if (join(kCan, sizeof(kCan) / sizeof(kCan[0]), shown, list, sizeof(list))) {
        if (shown & (NBT_PERM_CLEAR | NBT_PERM_CONTROL)) {
            const size_t used = strlen(list);
            snprintf(list + used, sizeof(list) - used, " %s",
                     text(review.permissions & NBT_PERM_DRIVE ? T_PERM_DRIVING : T_PERM_PARKED));
        }
        textf(line, sizeof(line), update ? T_PERM_NEW_CAN : T_PERM_CAN, list);
        ViewBuilder::addWrapped(v, line);
    }
}

} // namespace gui
} // namespace nbt
