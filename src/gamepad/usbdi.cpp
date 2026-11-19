// SPDX-License-Identifier: MPL-2.0
/* Chooses the libusbdi the gamepad talks to io-usb through. */
#include "nbt/gamepad/usbdi.hpp"
#ifdef __QNX__
#include "nbt/log.h"
#include <dlfcn.h>
#include <pthread.h>
#include <string.h>
namespace nbt {
namespace gamepad {
namespace {
const char kCarLibrary[] = "/lib/libusbdi.so.2";
#define NBT_USBDI_BUILTIN(name) &::usbd_##name,
const Usbdi kBuiltin = {NBT_USBDI_FUNCTIONS(NBT_USBDI_BUILTIN)};
#undef NBT_USBDI_BUILTIN
Usbdi carTable;
const Usbdi *chosen = &kBuiltin;
pthread_once_t chooseOnce = PTHREAD_ONCE_INIT;

template <typename Function> bool resolve(void *library, const char *symbol, Function &out) {
    void *address = dlsym(library, symbol);
    if (!address) {
        return false;
    }
    static_assert(sizeof(address) == sizeof(out), "function and data pointers differ");
    memcpy(&out, &address, sizeof(out));
    return true;
}

void choose() {
    void *library = dlopen(kCarLibrary, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        const char *why = dlerror();
        nbt_logf("gamepad: %s did not load (%s); using the built-in libusbdi", kCarLibrary,
                 why ? why : "no reason");
        return;
    }
    const char *missing = NULL;
#define NBT_USBDI_RESOLVE(name)                                                                    \
    if (!missing && !resolve(library, "usbd_" #name, carTable.name)) {                             \
        missing = "usbd_" #name;                                                                   \
    }
    NBT_USBDI_FUNCTIONS(NBT_USBDI_RESOLVE)
#undef NBT_USBDI_RESOLVE
    if (missing) {
        nbt_logf("gamepad: %s has no %s; using the built-in libusbdi", kCarLibrary, missing);
        (void)dlclose(library);
        return;
    }
    chosen = &carTable; // kept loaded for the process's life
    nbt_logf("gamepad: using %s", kCarLibrary);
}
} // namespace

const Usbdi &usbdi() {
    if (pthread_once(&chooseOnce, &choose)) {
        return kBuiltin;
    }
    return *chosen;
}
} // namespace gamepad
} // namespace nbt
#endif
