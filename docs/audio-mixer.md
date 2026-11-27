<!-- SPDX-License-Identifier: MPL-2.0 -->
# Audio mixer

Every application's and service's sound plays at once: one process owns the
head unit's audio output and mixes what each sends it. No capability; every
application on an SDK with the mixer client uses it without a change.

## Who runs it

- The release's `nbt-runner`, as `nbt-runner --mixer 127.0.0.1:42863`
  (the SDK's `nbt/app/audio_mix.hpp`, `src/audio_mix.cpp`).
- Each application host starts it before its application, unless it answers
  on that address, with the application's environment: the HMI's libraries
  and the manager's native audio setting (`NBT_AUDIO_CONN`, `NBT_AUDIO_A2I`),
  which it then plays through, as an application would. It runs on its own
  (its own session) and ends 30 s after its last client left.
- The host names it in `NBT_AUDIO_MIXER=127.0.0.1:42863`. Without a runner (a
  bundle for an older manager) the variable is absent and the application
  opens the output itself, as before.
- `src/process/audio_mixer.cpp`.

## What it does

- 44100 Hz stereo S16, the rate A2I plays. The SDK's `AudioPcm` reports it
  as its rate; the SDK's queues resample to it.
- Each client (SDK `b962e55` and later: version 2): the mixer asks it for
  frames as the output takes them, so its render runs just in time; 23 ms
  queued per client. The output is opened with 3 fragments of 11.6 ms, which
  `dev-libasound` queues twice (`head-unit-interfaces.md`): about 58 ms. A
  guest adds its own 23 ms. From a guest's render to the audio input: about
  105 ms, plus what iDrive's audio adds.
- An older client (version 1) pushes; TCP paces it, 46 ms queued plus 4 KiB
  in its socket.
- Clients are added sample by sample and clipped; one with nothing ready
  adds silence.
- Its threads and each client's playback thread run at priority 15 (as
  NBT-AAD's audio) for up to 4 ms of every 20 ms, then at their own
  (QNX sporadic scheduling): sound keeps up under load, and a render that
  runs away cannot starve iDrive.
- The output opens with the first client and closes 3 s after the last.
  The audio connection (`0xb8`, or the native audio setting's) is wanted
  while any client wants it (`AudioPcm::set_route_wanted`).
- At most 16 clients.

## Limits

- An application on an older SDK opens the output itself: while the mixer
  plays, that open may fail, or the two may not mix.
- Android Auto's daemon plays through its own player, not the mixer.
- A change of the native audio setting reaches the mixer when it starts
  again (30 s after the last client).
