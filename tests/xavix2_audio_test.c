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
	xavix2_audio_command(&audio, 0x40, descriptors);
	assert(xavix2_audio_status(&audio, 0) == 1);
	xavix2_audio_render(&audio, descriptors, 96000);
	frame = xavix2_audio_frame(&audio);
	assert(frame[0] == 0 && frame[1] == 0);
	assert(frame[2] == 64 * 255 && frame[3] == 64 * 128);
	assert(frame[4] == 127 * 255 && frame[5] == 127 * 128);
	assert(frame[6] == 0 && frame[7] == 0);
	assert(xavix2_audio_status(&audio, 0) == 0);

	rom[0x20] = 10;
	rom[0x21] = 20;
	rom[0x22] = 0x80;
	store_address(voice0, 0x02, 0x06, 0x20);
	store_address(voice0, 0x0e, 0x12, 0x20);
	xavix2_audio_command(&audio, 0x240, descriptors);
	xavix2_audio_render(&audio, descriptors, 96000);
	frame = xavix2_audio_frame(&audio);
	assert(frame[0] == 10 * 255);
	assert(frame[2] == 20 * 255);
	assert(frame[4] == 10 * 255);
	assert(xavix2_audio_status(&audio, 0) == 1);

	xavix2_audio_command(&audio, 0xc0, descriptors);
	assert(xavix2_audio_status(&audio, 0) == 1);
	xavix2_audio_command(&audio, 0x80, descriptors);
	assert(xavix2_audio_status(&audio, 0) == 0);

	store_address(voice0, 0x02, 0x06, sizeof(rom));
	xavix2_audio_command(&audio, 0x40, descriptors);
	assert(xavix2_audio_status(&audio, 0) == 0);
	assert(xavix2_audio_status(&audio, 8) == 0);

	puts("xavix2 audio tests passed");
	return 0;
}
