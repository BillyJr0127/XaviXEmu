/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 Billy Jr. and contributors */

#include "win_audio.h"

#include <string.h>

#define WIN_AUDIO_MAGIC UINT32_C(0x57415645)

static void set_error(win_audio *audio, uint32_t error)
{
	AcquireSRWLockExclusive(&audio->ring_lock);
	audio->last_error = error;
	audio->failed = 1;
	audio->active = 0;
	ReleaseSRWLockExclusive(&audio->ring_lock);
}

static int device_is_active(win_audio *audio)
{
	int active;

	AcquireSRWLockShared(&audio->ring_lock);
	active = audio->active && !audio->failed;
	ReleaseSRWLockShared(&audio->ring_lock);
	return active;
}

static void reclaim_finished_buffers(win_audio *audio)
{
	unsigned index;

	MemoryBarrier();
	for (index = 0; index < WIN_AUDIO_DEVICE_BUFFER_COUNT; ++index)
	{
		if (audio->header_queued[index] &&
			(audio->header[index].dwFlags & WHDR_DONE))
		{
			audio->header_queued[index] = 0;
			AcquireSRWLockExclusive(&audio->ring_lock);
			if (audio->device_queued_frames >= WIN_AUDIO_DEVICE_BUFFER_FRAMES)
				audio->device_queued_frames -= WIN_AUDIO_DEVICE_BUFFER_FRAMES;
			else
				audio->device_queued_frames = 0;
			audio->played_frames += WIN_AUDIO_DEVICE_BUFFER_FRAMES;
			ReleaseSRWLockExclusive(&audio->ring_lock);
		}
	}
}

static int pop_ring_buffer(win_audio *audio, int16_t *destination)
{
	uint32_t first_frames;
	uint32_t second_frames;

	AcquireSRWLockExclusive(&audio->ring_lock);
	if (!audio->active || audio->failed ||
		audio->ring_count < WIN_AUDIO_DEVICE_BUFFER_FRAMES)
	{
		ReleaseSRWLockExclusive(&audio->ring_lock);
		return 0;
	}

	first_frames = WIN_AUDIO_RING_FRAMES - audio->ring_read;
	if (first_frames > WIN_AUDIO_DEVICE_BUFFER_FRAMES)
		first_frames = WIN_AUDIO_DEVICE_BUFFER_FRAMES;
	second_frames = WIN_AUDIO_DEVICE_BUFFER_FRAMES - first_frames;
	memcpy(destination,
		&audio->ring_pcm[audio->ring_read * WIN_AUDIO_CHANNELS],
		(size_t)first_frames * WIN_AUDIO_CHANNELS * sizeof(int16_t));
	if (second_frames)
		memcpy(destination + first_frames * WIN_AUDIO_CHANNELS,
			audio->ring_pcm,
			(size_t)second_frames * WIN_AUDIO_CHANNELS * sizeof(int16_t));
	audio->ring_read =
		(audio->ring_read + WIN_AUDIO_DEVICE_BUFFER_FRAMES) % WIN_AUDIO_RING_FRAMES;
	audio->ring_count -= WIN_AUDIO_DEVICE_BUFFER_FRAMES;
	ReleaseSRWLockExclusive(&audio->ring_lock);
	return 1;
}

static uint32_t ring_count(win_audio *audio)
{
	uint32_t count;

	AcquireSRWLockShared(&audio->ring_lock);
	count = audio->ring_count;
	ReleaseSRWLockShared(&audio->ring_lock);
	return count;
}

static unsigned queued_buffer_count(const win_audio *audio)
{
	unsigned index;
	unsigned count = 0;

	for (index = 0; index < WIN_AUDIO_DEVICE_BUFFER_COUNT; ++index)
		if (audio->header_queued[index])
			count++;
	return count;
}

static int find_free_buffer(win_audio *audio, unsigned *result)
{
	unsigned attempt;

	for (attempt = 0; attempt < WIN_AUDIO_DEVICE_BUFFER_COUNT; ++attempt)
	{
		const unsigned index =
			(audio->next_device_buffer + attempt) % WIN_AUDIO_DEVICE_BUFFER_COUNT;
		if (!audio->header_queued[index])
		{
			*result = index;
			audio->next_device_buffer =
				(index + 1U) % WIN_AUDIO_DEVICE_BUFFER_COUNT;
			return 1;
		}
	}
	return 0;
}

static int is_started(win_audio *audio)
{
	int result;

	AcquireSRWLockShared(&audio->ring_lock);
	result = audio->active && !audio->failed && audio->started;
	ReleaseSRWLockShared(&audio->ring_lock);
	return result;
}

static int ready_to_start(win_audio *audio)
{
	int result;

	AcquireSRWLockShared(&audio->ring_lock);
	result = audio->active && !audio->failed && !audio->started &&
		audio->ring_count >= WIN_AUDIO_PREFILL_FRAMES;
	ReleaseSRWLockShared(&audio->ring_lock);
	return result;
}

static void note_queue_failure(win_audio *audio)
{
	AcquireSRWLockExclusive(&audio->ring_lock);
	audio->dropped_frames += WIN_AUDIO_DEVICE_BUFFER_FRAMES;
	ReleaseSRWLockExclusive(&audio->ring_lock);
}

static int queue_one_buffer(win_audio *audio, unsigned index)
{
	MMRESULT result;

	if (!pop_ring_buffer(audio, audio->device_pcm[index]))
		return 1;
	result = waveOutWrite(audio->device, &audio->header[index],
		sizeof(audio->header[index]));
	if (result != MMSYSERR_NOERROR)
	{
		note_queue_failure(audio);
		set_error(audio, result);
		return 0;
	}
	audio->header_queued[index] = 1;
	AcquireSRWLockExclusive(&audio->ring_lock);
	audio->device_queued_frames += WIN_AUDIO_DEVICE_BUFFER_FRAMES;
	ReleaseSRWLockExclusive(&audio->ring_lock);
	return 1;
}

static int queue_free_buffers(win_audio *audio)
{
	for (;;)
	{
		unsigned index;

		if (ring_count(audio) < WIN_AUDIO_DEVICE_BUFFER_FRAMES ||
			!find_free_buffer(audio, &index))
			break;
		if (!queue_one_buffer(audio, index))
			return 0;
	}
	return 1;
}

static int prefill_and_start(win_audio *audio)
{
	MMRESULT result;

	if (!ready_to_start(audio))
		return 1;
	if (!queue_free_buffers(audio))
		return 0;
	if (queued_buffer_count(audio) != WIN_AUDIO_DEVICE_BUFFER_COUNT)
		return 1;
	result = waveOutRestart(audio->device);
	if (result != MMSYSERR_NOERROR)
	{
		set_error(audio, result);
		return 0;
	}
	AcquireSRWLockExclusive(&audio->ring_lock);
	audio->started = 1;
	audio->ever_started = 1;
	ReleaseSRWLockExclusive(&audio->ring_lock);
	return 1;
}

static int queue_available_buffers(win_audio *audio)
{
	const unsigned had_queued = queued_buffer_count(audio);
	const int was_started = is_started(audio);
	MMRESULT result;

	reclaim_finished_buffers(audio);
	if (was_started && had_queued && !queued_buffer_count(audio))
	{
		result = waveOutPause(audio->device);
		if (result != MMSYSERR_NOERROR)
		{
			set_error(audio, result);
			return 0;
		}
		AcquireSRWLockExclusive(&audio->ring_lock);
		audio->started = 0;
		if (audio->ever_started)
			audio->underruns++;
		ReleaseSRWLockExclusive(&audio->ring_lock);
	}
	if (!is_started(audio))
		return prefill_and_start(audio);

	return queue_free_buffers(audio);
}

static DWORD WINAPI audio_worker(void *opaque)
{
	win_audio *audio = (win_audio *)opaque;
	HANDLE events[3];

	events[0] = audio->shutdown_event;
	events[1] = audio->buffer_event;
	events[2] = audio->data_event;
	for (;;)
	{
		const DWORD wait_result = WaitForMultipleObjects(3, events, FALSE, INFINITE);
		if (wait_result == WAIT_OBJECT_0)
			break;
		if (wait_result == WAIT_FAILED)
		{
			set_error(audio, MMSYSERR_ERROR);
			break;
		}
		if (!queue_available_buffers(audio))
			break;
	}
	return 0;
}

void win_audio_init(win_audio *audio)
{
	if (!audio)
		return;
	memset(audio, 0, sizeof(*audio));
	InitializeSRWLock(&audio->ring_lock);
	audio->magic = WIN_AUDIO_MAGIC;
}

static void close_events(win_audio *audio)
{
	if (audio->shutdown_event)
		CloseHandle(audio->shutdown_event);
	if (audio->buffer_event)
		CloseHandle(audio->buffer_event);
	if (audio->data_event)
		CloseHandle(audio->data_event);
	audio->shutdown_event = NULL;
	audio->buffer_event = NULL;
	audio->data_event = NULL;
}

static void unprepare_headers(win_audio *audio)
{
	unsigned index;

	for (index = 0; index < WIN_AUDIO_DEVICE_BUFFER_COUNT; ++index)
	{
		unsigned retries = 0;
		MMRESULT result;

		if (!audio->header_prepared[index])
			continue;
		do
		{
			result = waveOutUnprepareHeader(audio->device, &audio->header[index],
				sizeof(audio->header[index]));
			if (result == WAVERR_STILLPLAYING)
				Sleep(1);
		} while (result == WAVERR_STILLPLAYING && ++retries < 100U);
		if (result == MMSYSERR_NOERROR)
			audio->header_prepared[index] = 0;
		else if (!audio->last_error)
			audio->last_error = result;
		audio->header_queued[index] = 0;
	}
}

void win_audio_shutdown(win_audio *audio)
{
	if (!audio || audio->magic != WIN_AUDIO_MAGIC)
		return;

	AcquireSRWLockExclusive(&audio->ring_lock);
	audio->active = 0;
	ReleaseSRWLockExclusive(&audio->ring_lock);
	if (audio->shutdown_event)
		SetEvent(audio->shutdown_event);
	if (audio->worker_thread)
	{
		WaitForSingleObject(audio->worker_thread, INFINITE);
		CloseHandle(audio->worker_thread);
		audio->worker_thread = NULL;
	}
	if (audio->device)
	{
		MMRESULT result = waveOutReset(audio->device);
		if (result != MMSYSERR_NOERROR && !audio->last_error)
			audio->last_error = result;
		unprepare_headers(audio);
		result = waveOutClose(audio->device);
		if (result != MMSYSERR_NOERROR && !audio->last_error)
			audio->last_error = result;
		audio->device = NULL;
	}
	close_events(audio);
	AcquireSRWLockExclusive(&audio->ring_lock);
	audio->ring_read = 0;
	audio->ring_write = 0;
	audio->ring_count = 0;
	audio->device_queued_frames = 0;
	audio->started = 0;
	ReleaseSRWLockExclusive(&audio->ring_lock);
}

static void reset_open_state(win_audio *audio)
{
	unsigned index;

	AcquireSRWLockExclusive(&audio->ring_lock);
	audio->received_frames = 0;
	audio->ring_written_frames = 0;
	audio->played_frames = 0;
	audio->dropped_frames = 0;
	audio->underruns = 0;
	audio->ring_read = 0;
	audio->ring_write = 0;
	audio->ring_count = 0;
	audio->device_queued_frames = 0;
	audio->next_device_buffer = 0;
	audio->last_error = 0;
	audio->active = 0;
	audio->failed = 0;
	audio->started = 0;
	audio->ever_started = 0;
	ReleaseSRWLockExclusive(&audio->ring_lock);
	for (index = 0; index < WIN_AUDIO_DEVICE_BUFFER_COUNT; ++index)
	{
		memset(&audio->header[index], 0, sizeof(audio->header[index]));
		audio->header_prepared[index] = 0;
		audio->header_queued[index] = 0;
	}
}

int win_audio_open(win_audio *audio)
{
	WAVEFORMATEX format;
	MMRESULT result;
	unsigned index;

	if (!audio)
		return 0;
	if (audio->magic != WIN_AUDIO_MAGIC)
		win_audio_init(audio);
	if (audio->device || audio->worker_thread || audio->shutdown_event ||
		audio->buffer_event || audio->data_event)
		win_audio_shutdown(audio);
	reset_open_state(audio);

	audio->shutdown_event = CreateEventW(NULL, TRUE, FALSE, NULL);
	audio->buffer_event = CreateEventW(NULL, FALSE, FALSE, NULL);
	audio->data_event = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (!audio->shutdown_event || !audio->buffer_event || !audio->data_event)
	{
		set_error(audio, MMSYSERR_NOMEM);
		close_events(audio);
		return 0;
	}

	memset(&format, 0, sizeof(format));
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = WIN_AUDIO_CHANNELS;
	format.nSamplesPerSec = WIN_AUDIO_SAMPLE_RATE;
	format.wBitsPerSample = 16;
	format.nBlockAlign = (WORD)(format.nChannels * format.wBitsPerSample / 8U);
	format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
	result = waveOutOpen(&audio->device, WAVE_MAPPER, &format,
		(DWORD_PTR)audio->buffer_event, 0, CALLBACK_EVENT);
	if (result != MMSYSERR_NOERROR)
	{
		set_error(audio, result);
		audio->device = NULL;
		close_events(audio);
		return 0;
	}
	result = waveOutPause(audio->device);
	if (result != MMSYSERR_NOERROR)
	{
		set_error(audio, result);
		waveOutClose(audio->device);
		audio->device = NULL;
		close_events(audio);
		return 0;
	}

	for (index = 0; index < WIN_AUDIO_DEVICE_BUFFER_COUNT; ++index)
	{
		audio->header[index].lpData = (LPSTR)audio->device_pcm[index];
		audio->header[index].dwBufferLength =
			(DWORD)sizeof(audio->device_pcm[index]);
		result = waveOutPrepareHeader(audio->device, &audio->header[index],
			sizeof(audio->header[index]));
		if (result != MMSYSERR_NOERROR)
		{
			set_error(audio, result);
			waveOutReset(audio->device);
			unprepare_headers(audio);
			waveOutClose(audio->device);
			audio->device = NULL;
			close_events(audio);
			return 0;
		}
		audio->header_prepared[index] = 1;
	}

	AcquireSRWLockExclusive(&audio->ring_lock);
	audio->active = 1;
	ReleaseSRWLockExclusive(&audio->ring_lock);
	audio->worker_thread = CreateThread(NULL, 0, audio_worker, audio, 0, NULL);
	if (!audio->worker_thread)
	{
		set_error(audio, MMSYSERR_NOMEM);
		waveOutReset(audio->device);
		unprepare_headers(audio);
		waveOutClose(audio->device);
		audio->device = NULL;
		close_events(audio);
		return 0;
	}
	return 1;
}

size_t win_audio_submit(win_audio *audio, const int16_t *interleaved_stereo,
	size_t frames)
{
	size_t accepted;
	uint32_t first_frames;
	uint32_t second_frames;

	if (!audio || audio->magic != WIN_AUDIO_MAGIC || (!interleaved_stereo && frames))
		return 0;
	if (!frames)
		return 0;

	AcquireSRWLockExclusive(&audio->ring_lock);
	audio->received_frames += frames;
	if (!audio->active || audio->failed)
	{
		audio->dropped_frames += frames;
		ReleaseSRWLockExclusive(&audio->ring_lock);
		return frames;
	}

	accepted = WIN_AUDIO_RING_FRAMES - audio->ring_count;
	if (accepted > frames)
		accepted = frames;
	first_frames = WIN_AUDIO_RING_FRAMES - audio->ring_write;
	if (first_frames > accepted)
		first_frames = (uint32_t)accepted;
	second_frames = (uint32_t)accepted - first_frames;
	memcpy(&audio->ring_pcm[audio->ring_write * WIN_AUDIO_CHANNELS],
		interleaved_stereo,
		(size_t)first_frames * WIN_AUDIO_CHANNELS * sizeof(int16_t));
	if (second_frames)
		memcpy(audio->ring_pcm,
			interleaved_stereo + first_frames * WIN_AUDIO_CHANNELS,
			(size_t)second_frames * WIN_AUDIO_CHANNELS * sizeof(int16_t));
	audio->ring_write = (audio->ring_write + (uint32_t)accepted) %
		WIN_AUDIO_RING_FRAMES;
	audio->ring_count += (uint32_t)accepted;
	audio->ring_written_frames += accepted;
	audio->dropped_frames += frames - accepted;
	ReleaseSRWLockExclusive(&audio->ring_lock);
	if (accepted && audio->data_event)
		SetEvent(audio->data_event);

	/* The caller's input is always consumed; dropped_frames reports loss. */
	return frames;
}

int win_audio_is_active(win_audio *audio)
{
	if (!audio || audio->magic != WIN_AUDIO_MAGIC)
		return 0;
	return device_is_active(audio);
}

void win_audio_get_stats(win_audio *audio, win_audio_stats *stats)
{
	if (!stats)
		return;
	memset(stats, 0, sizeof(*stats));
	if (!audio || audio->magic != WIN_AUDIO_MAGIC)
		return;

	AcquireSRWLockShared(&audio->ring_lock);
	stats->received_frames = audio->received_frames;
	stats->ring_written_frames = audio->ring_written_frames;
	stats->played_frames = audio->played_frames;
	stats->dropped_frames = audio->dropped_frames;
	stats->underruns = audio->underruns;
	stats->ring_queued_frames = audio->ring_count;
	stats->device_queued_frames = audio->device_queued_frames;
	stats->last_error = audio->last_error;
	stats->active = audio->active;
	stats->failed = audio->failed;
	stats->started = audio->started;
	ReleaseSRWLockShared(&audio->ring_lock);
}
