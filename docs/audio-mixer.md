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
- Each client (version 2): the mixer asks it for frames as the output takes
  them, 256 at a time (5.8 ms), so its render runs just in time. Two
  fragments are queued per client (11.6 ms); a client that left the mixer
  empty three times gets one more, four at most.
- The output queues as little as the car holds. `dev-libasound` queues a
  playback twice (`head-unit-interfaces.md`), A2I (the "media" native audio
  setting) once:

  | Level | Fragments | Through `dev-libasound` | A2I buffers | Through A2I |
  | --- | --- | --- | --- | --- |
  | 0 | 2 | 17 ms | 3 | 17 ms |
  | 1 | 3 | 29 ms | 4 | 23 ms |
  | 2 | 6 | 64 ms | 8 | 46 ms |
  | 3 | 10 | 110 ms | 16 | 93 ms |

  It starts at level 0. When the output has played three gaps within 30 s
  (the SDK's `AudioPace`), the mixer opens it again at the next level, a
  moment's silence, and keeps that level in `/dev/shmem/nbt-mixer-level`
  until the head unit starts again. Its log says so (`mixer: the output
  played gaps, level N from now on`).
- A guest keeps two fragments in its own ring (11.6 ms) and more once the
  ring ran empty; a guest built on an SDK before `6634602` keeps 23 ms.
- From a guest's render to the audio input at level 0: about 41 ms (52 ms
  for an older guest), plus what iDrive's audio adds. The emulators add
  their queue: 32 ms, up to 64 ms when frames come late.
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
