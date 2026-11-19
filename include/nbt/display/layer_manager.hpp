// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_DISPLAY_LAYER_MANAGER_HPP
#define NBT_DISPLAY_LAYER_MANAGER_HPP

#include "nbt/core/status.hpp"

#include <stddef.h>

namespace nbt {
namespace display {

/* GENIVI LayerManagerService composes this head unit's panel: a Screen window
 * is drawn only once it is bound to a numbered LayerManager surface, which
 * LayerManager creates from the window's id string on its first post. The
 * iDrive HMI is surface 256, alone on layer 0x80000000. These calls run
 * /opt/sys/bin/LayerManagerControl as NBT-AAD does (docs/head-unit-interfaces.md). */

/* The manager GUI's surface, and the one surface for the application it shows
 * (NBT_SURFACE, held by one application host at a time): unused by the
 * firmware and NBT-AAD (1280 CarPlay, 1281 AAD's volume pill, 1282 AAD's
 * Android Auto picture). */
static const int kManagerSurface = 1290;
static const int kAppSurface = 1291;
static const int kVolumeSurface = 1292;

/* Runs LayerManagerControl with `args` (NULL-terminated, at most 9), keeping up
 * to size - 1 bytes of its output. Returns its exit status, or -1 with errno. */
int control(const char *const args[], char *output, size_t size);

/* Called at least every 250 ms while control() waits (up to 3 s a call), so a
 * caller keeps its heartbeats while LayerManager is slow. dev79, 33 s after a
 * boot: LayerManager answered no call for 28 s, and the GUI, blocked past the
 * launcher's 3 s heartbeat limit, was stopped and restarted three times. */
typedef void (*WaitHook)(void *context);
void setWaitHook(WaitHook hook, void *context);

/* True once LayerManager has made `surface` and bound a window to it. */
bool surfaceBound(int surface);

/* The size of screen 0 as LayerManager reports it. */
bool panelSize(int &width, int &height);

/* 1 when `surface` is in layer 0x80000000's render order, 0 when it is not,
 * -1 when LayerManager gave no order. */
int holds(int surface);

/* Puts `surface` alone on layer 0x80000000 in place of the HMI's surface 256,
 * or of the manager's other surface (the manager and an application swap
 * without iDrive in between), its source and destination both the whole panel,
 * as NBT-AAD shows its surfaces: the window must be panel-sized. Refuses with
 * EBUSY unless 256 or one of the manager's surfaces is alone on the layer:
 * Android Auto, CarPlay's pill or anything else there keeps the panel.
 * `replaced`, if given, receives the surface it took the panel from.
 * When `overlay > 0`, places `overlay` above `surface` in the render order. */
core::Status show(int surface, int *replaced = NULL, int overlay = 0);

/* Configures `overlay` for the full panel and places it above `mainSurface`
 * in layer 0x80000000's render order. False restores `mainSurface` alone. */
bool attachOverlay(int overlay, int mainSurface);

/* Puts the HMI back as NBT-AAD does (render order 256, surface 256 visible)
 * and hides the manager's surfaces, when the layer holds nothing but them and
 * 256, or nothing at all (a surface goes when its window closes). Anything
 * else on the layer (Android Auto) keeps it. True if it restored. */
bool restore();

} // namespace display
} // namespace nbt

#endif
