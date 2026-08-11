// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors
//
// Independent implementation based on firmware traces and runtime tests.
// MAME's XaviX 2 driver at the reference revision does not emulate sound.

#ifndef XAVIXEMU_XAVIX2_AUDIO_H
#define XAVIXEMU_XAVIX2_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	XAVIX2_AUDIO_VOICES = 64,
	XAVIX2_AUDIO_OUTPUT_RATE = 48000,
	XAVIX2_AUDIO_OUTPUT_CHANNELS = 2,
	XAVIX2_AUDIO_FRAMES_PER_VIDEO_FRAME =
		XAVIX2_AUDIO_OUTPUT_RATE / 60,
	XAVIX2_AUDIO_DESCRIPTOR_SIZE = 0x40,
	XAVIX2_AUDIO_DESCRIPTOR_BYTES =
		XAVIX2_AUDIO_VOICES * XAVIX2_AUDIO_DESCRIPTOR_SIZE
};

typedef struct xavix2_audio_voice
{
	uint64_t position;
	uint32_t start_address;
	uint32_t end_address;
	uint16_t pitch;
	uint8_t volume_left;
	uint8_t volume_right;
	uint8_t active;
	uint8_t loop;
} xavix2_audio_voice;

typedef struct xavix2_audio
{
	const uint8_t *rom;
	size_t rom_size;
	xavix2_audio_voice voice[XAVIX2_AUDIO_VOICES];
	int16_t frame[XAVIX2_AUDIO_FRAMES_PER_VIDEO_FRAME *
		XAVIX2_AUDIO_OUTPUT_CHANNELS];
} xavix2_audio;

void xavix2_audio_init(xavix2_audio *audio, const uint8_t *rom,
	size_t rom_size);
void xavix2_audio_command(xavix2_audio *audio, uint16_t command,
	const uint8_t descriptors[XAVIX2_AUDIO_DESCRIPTOR_BYTES],
	uint16_t control_pitch, uint8_t control_left, uint8_t control_right);
void xavix2_audio_render(xavix2_audio *audio, uint32_t engine_rate);
uint8_t xavix2_audio_status(const xavix2_audio *audio, unsigned byte_index);
const int16_t *xavix2_audio_frame(const xavix2_audio *audio);

#ifdef __cplusplus
}
#endif

#endif /* XAVIXEMU_XAVIX2_AUDIO_H */
