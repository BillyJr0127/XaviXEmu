// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "xavix2_audio.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void store16(uint8_t *destination, uint16_t value)
{
	destination[0] = (uint8_t)value;
	destination[1] = (uint8_t)(value >> 8);
}

static void store_address(uint8_t *descriptor, unsigned high_offset,
	unsigned low_offset, uint32_t address)
{
	store16(descriptor + high_offset, (uint16_t)(address >> 16));
	store16(descriptor + low_offset, (uint16_t)address);
}

int main(void)
{
	uint8_t rom[256] = { 0 };
	uint8_t descriptors[XAVIX2_AUDIO_DESCRIPTOR_BYTES] = { 0 };
	uint8_t *voice0 = descriptors;
	xavix2_audio audio;
	const int16_t *frame;

	rom[0x10] = 0;
	rom[0x11] = 64;
	rom[0x12] = 127;
	rom[0x13] = 0x80;
	store_address(voice0, 0x02, 0x06, 0x10);
	store16(voice0 + 0x16, 0x8000);
	voice0[0x32] = 0xff;
	voice0[0x33] = 0x80;

	xavix2_audio_init(&audio, rom, sizeof(rom));
	xavix2_audio_command(&audio, 0x40, descriptors, 0, 0, 0);
	assert(xavix2_audio_status(&audio, 0) == 1);
	xavix2_audio_render(&audio, 96000);
	frame = xavix2_audio_frame(&audio);
	assert(frame[0] == 0 && frame[1] == 0);
	assert(frame[2] == 64 * 255 / 2 && frame[3] == 64 * 128 / 2);
	assert(frame[4] == 127 * 255 / 2 && frame[5] == 127 * 128 / 2);
	assert(frame[6] == 0 && frame[7] == 0);
	assert(xavix2_audio_status(&audio, 0) == 0);

	rom[0x20] = 10;
	rom[0x21] = 20;
	rom[0x22] = 0x80;
	store_address(voice0, 0x02, 0x06, 0x20);
	store16(voice0 + 0x16, 0x8000);
	/* Firmware descriptors put the looping sample end (one byte beyond the
	 * terminator) here; playback loops to the primary start address. */
	store_address(voice0, 0x0e, 0x12, 0x23);
	xavix2_audio_command(&audio, 0x240, descriptors, 0, 0, 0);
	assert(audio.voice[0].start_address == 0x20);
	assert(audio.voice[0].end_address == 0x23);
	xavix2_audio_render(&audio, 96000);
	frame = xavix2_audio_frame(&audio);
	assert(frame[0] == 10 * 255 / 2);
	assert(frame[2] == 20 * 255 / 2);
	assert(frame[4] == 10 * 255 / 2);
	assert(xavix2_audio_status(&audio, 0) == 1);

	xavix2_audio_command(&audio, 0xc0, descriptors, 0x4000, 7, 9);
	assert(xavix2_audio_status(&audio, 0) == 1);
	assert(audio.voice[0].pitch == 0x4000);
	assert(audio.voice[0].volume_left == 7);
	assert(audio.voice[0].volume_right == 9);
	xavix2_audio_render(&audio, 96000);
	frame = xavix2_audio_frame(&audio);
	assert(frame[0] == 10 * 7 / 2 && frame[1] == 10 * 9 / 2);
	assert(frame[2] == 15 * 7 / 2 && frame[3] == 15 * 9 / 2);
	xavix2_audio_command(&audio, 0x80, descriptors, 0, 0, 0);
	assert(xavix2_audio_status(&audio, 0) == 0);

	rom[0x30] = 10;
	rom[0x31] = 0x80;
	rom[0x32] = 100;
	store_address(voice0, 0x02, 0x06, 0x30);
	store16(voice0 + 0x16, 0x8000);
	xavix2_audio_command(&audio, 0x40, descriptors, 0, 0, 0);
	xavix2_audio_render(&audio, 192000);
	frame = xavix2_audio_frame(&audio);
	assert(frame[0] == 10 * 255 / 2);
	assert(frame[2] == 0);
	assert(xavix2_audio_status(&audio, 0) == 0);

	store_address(voice0, 0x02, 0x06, sizeof(rom));
	xavix2_audio_command(&audio, 0x40, descriptors, 0, 0, 0);
	assert(xavix2_audio_status(&audio, 0) == 0);
	assert(xavix2_audio_status(&audio, 8) == 0);

	puts("xavix2 audio tests passed");
	return 0;
}
