#include "nbt/target.h"
#include <string.h>

int nbt_target_compatible(const char *system, const char *release, int armv7) {
    return system && release && armv7 == 1 && !strcmp(system, "QNX") &&
           !strncmp(release, "6.5", 3) && (!release[3] || release[3] == '.');
}
