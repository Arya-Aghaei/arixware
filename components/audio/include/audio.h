#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Starts the speaker codec and loads the packed sound-effect assets. */
void audio_init(void);

/** Queues a sound (by its generated MMAP_SOUNDS_* index) for playback on a dedicated task.
 * No-op while muted (see audio_set_muted()). */
void audio_play_index(int index);

/** True while any sound is queued or playing. */
bool audio_is_playing(void);

/** Drops queued sounds and cuts the one playing (long clips such as alarms stop within ~130 ms). */
void audio_stop(void);

/** Speaker volume in percent (0 is silent). Values are clamped to 0..100. */
void audio_set_volume(int volume);
int audio_get_volume(void);

/** Immediately mutes/unmutes playback, discarding queued sounds when muted. Keeps volume. */
void audio_set_muted(bool muted);

#ifdef __cplusplus
}
#endif
