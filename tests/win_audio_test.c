// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "win_audio.h"

#include <assert.h>
#include <stdio.h>

static void fill_pcm(int16_t *pcm, size_t frames)
{
	size_t frame;

	for (frame = 0; frame < frames; ++frame)
	{
		const int16_t value = (int16_t)(((frame * 97U) & 0x3fffU) - 0x2000);
		pcm[frame * 2U] = value;
		pcm[frame * 2U + 1U] = (int16_t)-value;
	}
}

int main(void)
{
	static int16_t pcm[(WIN_AUDIO_RING_FRAMES + 2048) * WIN_AUDIO_CHANNELS];
	win_audio audio;
	win_audio_stats stats;
	const size_t frames = WIN_AUDIO_RING_FRAMES + 2048U;
	int opened;

	win_audio_init(&audio);
	fill_pcm(pcm, frames);
	assert(!win_audio_is_active(&audio));
	assert(win_audio_submit(&audio, pcm, 32) == 32);
	win_audio_get_stats(&audio, &stats);
	assert(stats.received_frames == 32);
	assert(stats.dropped_frames == 32);

	opened = win_audio_open(&audio);
	if (opened)
	{
		unsigned wait_count;

		assert(win_audio_is_active(&audio));
		assert(win_audio_submit(&audio, pcm, frames) == frames);
		for (wait_count = 0; wait_count < 100U; ++wait_count)
		{
			win_audio_get_stats(&audio, &stats);
			if (stats.started || stats.failed)
				break;
			Sleep(1);
		}
		win_audio_get_stats(&audio, &stats);
		assert(stats.received_frames == frames);
		/* submit is non-blocking and the software ring has finite capacity. */
		assert(stats.dropped_frames >= 2048U);
		assert(stats.started && !stats.failed);
		puts("win_audio_test: waveOut opened");
	}
	else
	{
		assert(!win_audio_is_active(&audio));
		assert(win_audio_submit(&audio, pcm, frames) == frames);
		win_audio_get_stats(&audio, &stats);
		assert(stats.dropped_frames == frames);
		puts("win_audio_test: no waveOut device (graceful fallback passed)");
	}

	win_audio_shutdown(&audio);
	win_audio_shutdown(&audio);
	assert(!win_audio_is_active(&audio));
	assert(win_audio_submit(&audio, pcm, 1) == 1);
	puts("win_audio_test: ok");
	return 0;
}
