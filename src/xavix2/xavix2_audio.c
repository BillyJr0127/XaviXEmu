// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors
//
// Independent implementation based on firmware traces and runtime tests.
// MAME's XaviX 2 driver at the reference revision does not emulate sound.

#include "xavix2_audio.h"

#include <limits.h>
#include <string.h>

static uint16_t load16(const uint8_t *source)
{
	return (uint16_t)(source[0] | ((uint16_t)source[1] << 8));
}

static uint32_t descriptor_address(const uint8_t *descriptor,
	unsigned high_offset, unsigned low_offset)
{
	return ((uint32_t)load16(descriptor + high_offset) << 16) |
		load16(descriptor + low_offset);
}

static int16_t clamp16(int64_t sample)
{
	if (sample > INT16_MAX)
		return INT16_MAX;
	if (sample < INT16_MIN)
		return INT16_MIN;
	return (int16_t)sample;
}

static void stop_voice(xavix2_audio_voice *voice)
{
	voice->position = 0;
	voice->loop_address = 0;
	voice->active = 0;
	voice->loop = 0;
}

void xavix2_audio_init(xavix2_audio *audio, const uint8_t *rom,
	size_t rom_size)
{
	if (!audio)
		return;
	memset(audio, 0, sizeof(*audio));
	audio->rom = rom;
	audio->rom_size = rom_size;
}

void xavix2_audio_command(xavix2_audio *audio, uint16_t command,
	const uint8_t descriptors[XAVIX2_AUDIO_DESCRIPTOR_BYTES])
{
	unsigned channel;
	unsigned operation;
	const uint8_t *descriptor;
	xavix2_audio_voice *voice;
	uint32_t start;

	if (!audio || !descriptors)
		return;
	channel = command & 0x3f;
	operation = command & 0x3c0;
	voice = &audio->voice[channel];
	descriptor = descriptors + channel * XAVIX2_AUDIO_DESCRIPTOR_SIZE;

	if (operation == 0x080)
	{
		stop_voice(voice);
		return;
	}
	if (operation == 0x0c0)
		return;
	if (operation != 0x040 && operation != 0x240)
		return;

	start = descriptor_address(descriptor, 0x02, 0x06);
	if (start >= audio->rom_size)
	{
		stop_voice(voice);
		return;
	}
	voice->position = (uint64_t)start << 32;
	voice->loop_address = descriptor_address(descriptor, 0x0e, 0x12);
	voice->loop = operation == 0x240;
	if (voice->loop && voice->loop_address >= audio->rom_size)
		voice->loop_address = start;
	voice->active = 1;
}

static int current_sample(xavix2_audio *audio, xavix2_audio_voice *voice,
	int32_t *sample)
{
	uint32_t address;
	uint8_t value;

	address = (uint32_t)(voice->position >> 32);
	if (address >= audio->rom_size)
	{
		stop_voice(voice);
		return 0;
	}
	value = audio->rom[address];
	if (value == 0x80)
	{
		if (!voice->loop || voice->loop_address >= audio->rom_size)
		{
			stop_voice(voice);
			return 0;
		}
		voice->position = (uint64_t)voice->loop_address << 32;
		value = audio->rom[voice->loop_address];
		if (value == 0x80)
		{
			stop_voice(voice);
			return 0;
		}
	}
	*sample = (int8_t)value;
	return 1;
}

static int32_t interpolated_sample(xavix2_audio *audio,
	xavix2_audio_voice *voice)
{
	uint32_t address;
	uint32_t fraction;
	int32_t first;
	int32_t second;
	uint8_t next;

	if (!current_sample(audio, voice, &first))
		return 0;
	address = (uint32_t)(voice->position >> 32);
	fraction = (uint32_t)voice->position;
	if (address + 1 >= audio->rom_size)
		return first;
	next = audio->rom[address + 1];
	if (next == 0x80)
	{
		if (voice->loop && voice->loop_address < audio->rom_size &&
			audio->rom[voice->loop_address] != 0x80)
			second = (int8_t)audio->rom[voice->loop_address];
		else
			second = 0;
	}
	else
		second = (int8_t)next;
	return first + (int32_t)(((int64_t)(second - first) * fraction) >> 32);
}

void xavix2_audio_render(xavix2_audio *audio,
	const uint8_t descriptors[XAVIX2_AUDIO_DESCRIPTOR_BYTES],
	uint32_t engine_rate)
{
	unsigned frame;
	if (!audio || !descriptors)
		return;
	memset(audio->frame, 0, sizeof(audio->frame));
	if (!engine_rate)
		return;

	for (frame = 0; frame < XAVIX2_AUDIO_FRAMES_PER_VIDEO_FRAME; ++frame)
	{
		int64_t left = 0;
		int64_t right = 0;
		unsigned channel;
		for (channel = 0; channel < XAVIX2_AUDIO_VOICES; ++channel)
		{
			xavix2_audio_voice *voice = &audio->voice[channel];
			const uint8_t *descriptor;
			uint16_t pitch;
			uint64_t step;
			int32_t sample;
			if (!voice->active)
				continue;
			descriptor = descriptors +
				channel * XAVIX2_AUDIO_DESCRIPTOR_SIZE;
			pitch = load16(descriptor + 0x16);
			if (!pitch)
				continue;
			sample = interpolated_sample(audio, voice);
			if (!voice->active)
				continue;
			left += sample * descriptor[0x32];
			right += sample * descriptor[0x33];
			step = ((uint64_t)pitch * engine_rate << 16) /
				XAVIX2_AUDIO_OUTPUT_RATE;
			voice->position += step;
		}
		audio->frame[frame * 2] = clamp16(left);
		audio->frame[frame * 2 + 1] = clamp16(right);
	}
}

uint8_t xavix2_audio_status(const xavix2_audio *audio, unsigned byte_index)
{
	uint8_t result = 0;
	unsigned bit;
	if (!audio || byte_index >= XAVIX2_AUDIO_VOICES / 8)
		return 0;
	for (bit = 0; bit < 8; ++bit)
		if (audio->voice[byte_index * 8 + bit].active)
			result |= (uint8_t)(1U << bit);
	return result;
}

const int16_t *xavix2_audio_frame(const xavix2_audio *audio)
{
	return audio ? audio->frame : NULL;
}
