#include "nbt/target.h"
#include <assert.h>
#include <stddef.h>

int main(void) {
    assert(nbt_target_compatible("QNX", "6.5.0", 1));
    assert(nbt_target_compatible("QNX", "6.5", 1));
    assert(!nbt_target_compatible("QNX", "6.5.0", 0));
    assert(!nbt_target_compatible("QNX", "6.5.0", 2));
    assert(!nbt_target_compatible("QNX", "6.6.0", 1));
    assert(!nbt_target_compatible("QNX", "6.50", 1));
    assert(!nbt_target_compatible("Linux", "6.5.0", 1));
    assert(!nbt_target_compatible(NULL, "6.5.0", 1));
    assert(!nbt_target_compatible("QNX", NULL, 1));
    return 0;
}
