// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "xavix2_audio.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
			return 1; \
		} \
	} while (0)

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

	CHECK(xavix2_audio_engine_rate(0x20, 0x0d) == 213068);
	CHECK(xavix2_audio_engine_rate(0x20, 0x0f) == 186434);

	rom[0x10] = 0;
	rom[0x11] = 64;
	rom[0x12] = 127;
	rom[0x13] = 0x80;
	store_address(voice0, 0x02, 0x06, 0x10);
	store16(voice0 + 0x16, 0x8000);
	voice0[0x32] = 0xff;
	voice0[0x33] = 0x80;

	xavix2_audio_init(&audio, rom, sizeof(rom));
	xavix2_audio_command(&audio, 0x40, descriptors, 0, 0, 0, 0);
	CHECK(xavix2_audio_status(&audio, 0) == 1);
	xavix2_audio_render(&audio, 96000);
	frame = xavix2_audio_frame(&audio);
	CHECK(frame[0] == 0 && frame[1] == 0);
	CHECK(frame[2] == 64 * 255 / 2 && frame[3] == 64 * 128 / 2);
	CHECK(frame[4] == 127 * 255 / 2 && frame[5] == 127 * 128 / 2);
	CHECK(frame[6] == 0 && frame[7] == 0);
	CHECK(xavix2_audio_status(&audio, 0) == 0);

	rom[0x20] = 10; /* attack */
	rom[0x21] = 20;
	rom[0x22] = 0x80;
	rom[0x23] = 30; /* sustain loop */
	rom[0x24] = 40;
	rom[0x25] = 0x80;
	store_address(voice0, 0x02, 0x06, 0x20);
	store16(voice0 + 0x16, 0x8000);
	/* The secondary array begins one byte beyond the attack terminator. */
	store_address(voice0, 0x0e, 0x12, 0x23);
	xavix2_audio_command(&audio, 0x240, descriptors, 0, 0, 0, 0);
	CHECK(audio.voice[0].start_address == 0x20);
	CHECK(audio.voice[0].loop_address == 0x23);
	audio.voice[0].loop_address = 0x24;
	xavix2_audio_restore_descriptors(&audio, descriptors);
	CHECK(audio.voice[0].loop_address == 0x24);
	/* Older F7 files captured the provisional primary-loop address.  Restore
	 * derives the patent-confirmed sustain address from guest descriptors. */
	audio.voice[0].loop_address = 0x20;
	xavix2_audio_restore_descriptors(&audio, descriptors);
	CHECK(audio.voice[0].loop_address == 0x23);
	xavix2_audio_render(&audio, 96000);
	frame = xavix2_audio_frame(&audio);
	CHECK(frame[0] == 10 * 255 / 2);
	CHECK(frame[2] == 20 * 255 / 2);
	CHECK(frame[4] == 30 * 255 / 2);
	CHECK(frame[6] == 40 * 255 / 2);
	CHECK(frame[8] == 30 * 255 / 2);
	CHECK(xavix2_audio_status(&audio, 0) == 1);

	/* Host diagnostics may mute a channel without changing guest-visible
	 * status or pausing its source position. */
	{
		uint64_t position = audio.voice[0].position;
		xavix2_audio_set_mute_mask(&audio, 1);
		/* Use a cadence that cannot wrap this two-byte loop back to the
		 * same Q32 position after one complete output frame. */
		xavix2_audio_render(&audio, 72000);
		frame = xavix2_audio_frame(&audio);
		CHECK(frame[0] == 0 && frame[1] == 0);
		CHECK(xavix2_audio_status(&audio, 0) == 1);
		CHECK(audio.voice[0].position != position);
		/* Restore this fixture's phase before the following update test. */
		audio.voice[0].position = position;
		xavix2_audio_set_mute_mask(&audio, 0);
	}

	audio.voice[0].position = UINT64_C(0x20) << 32;
	xavix2_audio_command(&audio, 0xc0, descriptors, 0x4000, 0, 7, 9);
	CHECK(xavix2_audio_status(&audio, 0) == 1);
	CHECK(audio.voice[0].pitch == 0x4000);
	CHECK(audio.voice[0].volume_left == 7);
	CHECK(audio.voice[0].volume_right == 9);
	xavix2_audio_render(&audio, 96000);
	frame = xavix2_audio_frame(&audio);
	CHECK(frame[0] == 10 * 7 / 2 && frame[1] == 10 * 9 / 2);
	CHECK(frame[2] == 15 * 7 / 2 && frame[3] == 15 * 9 / 2);
	/* EA1B bit 0 is the firmware's note-release form of the update command.
	 * It keeps the guest channel allocated while its output decays. */
	xavix2_audio_command(&audio, 0xc0, descriptors, 0x4000, 0x0100, 7, 9);
	CHECK(xavix2_audio_status(&audio, 0) == 1);
	CHECK(audio.voice[0].release_phase == 1);
	xavix2_audio_render(&audio, 96000);
	frame = xavix2_audio_frame(&audio);
	CHECK(frame[0] == 30 * 7 / 2 && frame[1] == 30 * 9 / 2);
	CHECK(audio.voice[0].release_phase == 2);
	for (unsigned release_frame = 1; release_frame < 16; ++release_frame)
		xavix2_audio_render(&audio, 96000);
	CHECK(xavix2_audio_status(&audio, 0) == 0);
	CHECK(audio.voice[0].release_phase == 0);
	xavix2_audio_render(&audio, 96000);
	frame = xavix2_audio_frame(&audio);
	CHECK(frame[0] == 0 && frame[1] == 0);
	/* A new note reuses the allocated voice at full level. */
	xavix2_audio_command(&audio, 0x240, descriptors, 0, 0, 0, 0);
	CHECK(audio.voice[0].release_phase == 0);
	xavix2_audio_command(&audio, 0x80, descriptors, 0, 0, 0, 0);
	CHECK(xavix2_audio_status(&audio, 0) == 0);

	rom[0x30] = 10;
	rom[0x31] = 0x80;
	rom[0x32] = 100;
	store_address(voice0, 0x02, 0x06, 0x30);
	store16(voice0 + 0x16, 0x8000);
	xavix2_audio_command(&audio, 0x40, descriptors, 0, 0, 0, 0);
	xavix2_audio_render(&audio, 192000);
	frame = xavix2_audio_frame(&audio);
	CHECK(frame[0] == 10 * 255 / 2);
	CHECK(frame[2] == 0);
	CHECK(xavix2_audio_status(&audio, 0) == 0);

	/* An invalid loop target must not silently wrap to ROM address zero. */
	rom[0x40] = 10;
	rom[0x41] = 0x80;
	store_address(voice0, 0x02, 0x06, 0x40);
	store_address(voice0, 0x0e, 0x12, sizeof(rom));
	xavix2_audio_command(&audio, 0x240, descriptors, 0, 0, 0, 0);
	CHECK(audio.voice[0].loop == 0);
	xavix2_audio_render(&audio, 96000);
	CHECK(xavix2_audio_status(&audio, 0) == 0);

	store_address(voice0, 0x02, 0x06, sizeof(rom));
	xavix2_audio_command(&audio, 0x40, descriptors, 0, 0, 0, 0);
	CHECK(xavix2_audio_status(&audio, 0) == 0);
	CHECK(xavix2_audio_status(&audio, 8) == 0);

	puts("xavix2 audio tests passed");
	return 0;
}
