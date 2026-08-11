/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 Billy Jr. and contributors */

#ifndef DRGQST_WIN_AUDIO_H
#define DRGQST_WIN_AUDIO_H

#include <windows.h>
#include <mmsystem.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	WIN_AUDIO_SAMPLE_RATE = 48000,
	WIN_AUDIO_CHANNELS = 2,
	WIN_AUDIO_DEVICE_BUFFER_COUNT = 8,
	WIN_AUDIO_DEVICE_BUFFER_FRAMES = 256,
	WIN_AUDIO_PREFILL_FRAMES =
		WIN_AUDIO_DEVICE_BUFFER_COUNT * WIN_AUDIO_DEVICE_BUFFER_FRAMES,
	WIN_AUDIO_RING_FRAMES = 4096
};

typedef struct win_audio_stats
{
	uint64_t received_frames;
	uint64_t ring_written_frames;
	uint64_t played_frames;
	uint64_t dropped_frames;
	uint64_t underruns;
	uint32_t ring_queued_frames;
	uint32_t device_queued_frames;
	uint32_t last_error;
	uint8_t active;
	uint8_t failed;
	uint8_t started;
} win_audio_stats;

/* Public to permit static allocation by the small Win32 front end.  Callers
 * must use the functions below rather than changing members directly. */
typedef struct win_audio
{
	SRWLOCK ring_lock;
	HWAVEOUT device;
	HANDLE shutdown_event;
	HANDLE buffer_event;
	HANDLE data_event;
	HANDLE worker_thread;
	WAVEHDR header[WIN_AUDIO_DEVICE_BUFFER_COUNT];
	int16_t device_pcm[WIN_AUDIO_DEVICE_BUFFER_COUNT]
		[WIN_AUDIO_DEVICE_BUFFER_FRAMES * WIN_AUDIO_CHANNELS];
	int16_t ring_pcm[WIN_AUDIO_RING_FRAMES * WIN_AUDIO_CHANNELS];
	uint64_t received_frames;
	uint64_t ring_written_frames;
	uint64_t played_frames;
	uint64_t dropped_frames;
	uint64_t underruns;
	uint32_t ring_read;
	uint32_t ring_write;
	uint32_t ring_count;
	uint32_t device_queued_frames;
	uint32_t next_device_buffer;
	uint32_t last_error;
	uint32_t magic;
	uint8_t header_prepared[WIN_AUDIO_DEVICE_BUFFER_COUNT];
	uint8_t header_queued[WIN_AUDIO_DEVICE_BUFFER_COUNT];
	uint8_t active;
	uint8_t failed;
	uint8_t started;
	uint8_t ever_started;
} win_audio;

void win_audio_init(win_audio *audio);

/* Returns non-zero when a waveOut device and worker were opened.  Failure is
 * non-fatal: submit remains a non-blocking discard sink. */
int win_audio_open(win_audio *audio);

/* Consumes frames from the caller even when they must be dropped, preventing
 * audio back-pressure from stalling emulation.  A NULL samples pointer with a
 * non-zero frame count is invalid and returns zero. */
size_t win_audio_submit(win_audio *audio, const int16_t *interleaved_stereo,
	size_t frames);

int win_audio_is_active(win_audio *audio);
void win_audio_get_stats(win_audio *audio, win_audio_stats *stats);

/* Idempotent.  Stops the worker, resets waveOut, unprepares every header and
 * closes all handles. */
void win_audio_shutdown(win_audio *audio);

#ifdef __cplusplus
}
#endif

#endif
