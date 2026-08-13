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

uint32_t xavix2_audio_engine_rate(uint8_t divider_a, uint8_t divider_b)
{
	const uint32_t divisor = ((uint32_t)divider_a + 1U) *
		((uint32_t)divider_b + 1U);
	return XAVIX2_AUDIO_MASTER_CLOCK / divisor;
}

static void stop_voice(xavix2_audio_voice *voice)
{
	voice->position = 0;
	voice->start_address = 0;
	voice->loop_address = 0;
	voice->pitch = 0;
	voice->volume_left = 0;
	voice->volume_right = 0;
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
	const uint8_t descriptors[XAVIX2_AUDIO_DESCRIPTOR_BYTES],
	uint16_t control_pitch, uint8_t control_left, uint8_t control_right)
{
	unsigned channel;
	unsigned operation;
	const uint8_t *descriptor;
	xavix2_audio_voice *voice;
	uint32_t start;
	uint32_t loop_address;

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
	{
		if (voice->active)
		{
			voice->pitch = control_pitch;
			voice->volume_left = control_left;
			voice->volume_right = control_right;
		}
		return;
	}
	if (operation != 0x040 && operation != 0x240)
		return;

	start = descriptor_address(descriptor, 0x02, 0x06);
	if (start >= audio->rom_size)
	{
		stop_voice(voice);
		return;
	}
	voice->position = (uint64_t)start << 32;
	voice->start_address = start;
	/* The second waveform address is the target selected when a 0x80
	 * terminator is encountered, not an exclusive end address.  Real Naruto
	 * descriptors place the end of an instrument's attack immediately before
	 * this address, then keep a short sustain waveform after it. */
	loop_address = descriptor_address(descriptor, 0x0e, 0x12);
	voice->loop = operation == 0x240 && loop_address < audio->rom_size;
	voice->loop_address = voice->loop ?
		loop_address : 0;
	voice->pitch = load16(descriptor + 0x16);
	voice->volume_left = descriptor[0x32];
	voice->volume_right = descriptor[0x33];
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

static void advance_voice(xavix2_audio *audio, xavix2_audio_voice *voice,
	uint64_t step)
{
	const uint32_t old_address = (uint32_t)(voice->position >> 32);
	uint64_t next_position = voice->position + step;
	uint32_t next_address = (uint32_t)(next_position >> 32);
	uint32_t address;

	/* Faster-than-output-rate voices may cross the 0x80 terminator without
	 * landing on it.  Check every newly crossed source byte so a one-shot
	 * cannot continue into unrelated ROM and a sentinel-loop cannot emit the
	 * bytes following its sample. */
	for (address = old_address + 1; address <= next_address; ++address)
	{
		if (address >= audio->rom_size || audio->rom[address] == 0x80)
		{
			if (voice->loop && voice->loop_address < audio->rom_size &&
				audio->rom[voice->loop_address] != 0x80)
				voice->position = (uint64_t)voice->loop_address << 32;
			else
				stop_voice(voice);
			return;
		}
		if (address == UINT32_MAX)
			break;
	}
	voice->position = next_position;
}

void xavix2_audio_render(xavix2_audio *audio, uint32_t engine_rate)
{
	unsigned frame;
	if (!audio)
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
			uint64_t step;
			int32_t sample;
			if (!voice->active)
				continue;
			if (!voice->pitch)
				continue;
			sample = interpolated_sample(audio, voice);
			if (!voice->active)
				continue;
			if (!voice->host_muted)
			{
				left += sample * voice->volume_left;
				right += sample * voice->volume_right;
			}
			/* Firmware computes pitch as source_rate * 65536 / engine_rate.
			 * Convert that Q16 phase increment to the host output cadence. */
			step = ((uint64_t)voice->pitch * engine_rate << 16) /
				XAVIX2_AUDIO_OUTPUT_RATE;
			advance_voice(audio, voice, step);
		}
		/* The channel accumulator has more headroom than the final DAC.  Keep
		 * one guard bit before conversion so ordinary polyphonic passages do
		 * not hard-clip when several firmware voices overlap. */
		left /= 2;
		right /= 2;
		audio->frame[frame * 2] = clamp16(left);
		audio->frame[frame * 2 + 1] = clamp16(right);
	}
}

void xavix2_audio_set_mute_mask(xavix2_audio *audio, uint64_t mute_mask)
{
	unsigned channel;
	if (!audio)
		return;
	for (channel = 0; channel < XAVIX2_AUDIO_VOICES; ++channel)
		audio->voice[channel].host_muted =
			(uint8_t)((mute_mask >> channel) & 1U);
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
