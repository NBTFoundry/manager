// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_PROCESS_AUDIO_MIXER_HPP
#define NBT_PROCESS_AUDIO_MIXER_HPP

/* NBT Manager's audio mixer (docs/audio-mixer.md): the release's nbt-runner
 * in its `--mixer` mode owns the head unit's audio output and plays every
 * application's and service's sound at once. A host starts it, unless it
 * runs, and names it to its application in NBT_AUDIO_MIXER. */

#include "nbt/app_settings.h"

namespace nbt {
namespace process {

/* "127.0.0.1:<port>"; NBT_TEST_MIXER_ADDRESS in host tests. */
const char *mixerAddress();
/* Starts the mixer with `environment` (the application's: the HMI's
 * libraries, the native audio setting) unless it answers, then adds
 * NBT_AUDIO_MIXER to the environment. False without a runner, or when the
 * environment is full: the application then opens the output itself. */
bool grantMixer(nbt_app_environment *environment);

} // namespace process
} // namespace nbt

#endif
