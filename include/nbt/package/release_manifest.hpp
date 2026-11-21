// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PACKAGE_RELEASE_MANIFEST_HPP
#define NBT_PACKAGE_RELEASE_MANIFEST_HPP

#include "nbt/core/status.hpp"
#include "nbt/release.h"

#include <stdint.h>

namespace nbt {
namespace package {

/* Parses a release manifest of `format` (1-3) into `out`: identity, bridge
 * lines, components with their names; `total` gets the payload size. */
core::Status parseManifest(char *text, uint8_t format, nbt_release *out, uint64_t *total);
/* Whether component `index` is ARM ELF, and whether it must be ET_DYN. */
bool isElfComponent(const nbt_release &release, unsigned index);
bool mustBeSharedObject(const nbt_release &release, unsigned index);

} // namespace package
} // namespace nbt

#endif
