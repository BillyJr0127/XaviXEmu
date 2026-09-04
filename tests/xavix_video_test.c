// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "xavix_video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
			exit(EXIT_FAILURE); \
		} \
	} while (0)

typedef struct test_rom
{
	uint32_t base;
	uint8_t bytes[0x100];
} test_rom;

static uint8_t read_test_rom(void *opaque, uint32_t address)
{
	test_rom *rom = (test_rom *)opaque;
	if (address >= rom->base && address < rom->base + sizeof(rom->bytes))
		return rom->bytes[address - rom->base];
	return 0;
}

static void make_opaque_pen(uint8_t *sh, uint8_t *l, unsigned pen, unsigned hue)
{
	/* y_raw=16, c_raw=4, caller-selected visible hue. */
	const uint16_t value = (uint16_t)((16U << 8) | (4U << 5) | (hue & 0x1fU));
	sh[pen] = (uint8_t)value;
	l[pen] = (uint8_t)(value >> 8);
}

static void set_sprite_position(uint8_t *fragment, unsigned slot, int x, int y,
	unsigned width, unsigned height)
{
	const unsigned raw_x = ((unsigned)(x + 0x80 + (int)(width / 2U))) ^ 0x100U;
	int signed_y = y - 129 + (int)(height / 2U);
	unsigned transformed_y;

	if (signed_y < 0)
		transformed_y = 0x80U | (unsigned)(signed_y + 0x80);
	else
		transformed_y = (unsigned)signed_y;
	fragment[0x200U + slot] = (uint8_t)(transformed_y ^ 0xffU);
	fragment[0x300U + slot] = (uint8_t)raw_x;
	fragment[0x400U + slot] = (uint8_t)((raw_x >> 8) & 1U);
}

static void test_fragment_write_alias(void)
{
	uint8_t fragment[XAVIX_VIDEO_FRAGMENT_BYTES] = { 0 };

	xavix_video_fragment_write(fragment, 0x300, 0x80);
	CHECK(fragment[0x300] == 0x80);
	CHECK(fragment[0x400] == 1);
	CHECK((fragment[0x000] & 1U) == 1U);
	xavix_video_fragment_write(fragment, 0x400, 0x00);
	CHECK(fragment[0x400] == 0);
	CHECK((fragment[0x000] & 1U) == 0U);
}

static void test_tile_priority_and_clip(void)
{
	static xavix_video video;
	xavix_video_inputs input;
	uint8_t ram[0x4000] = { 0 };
	uint8_t fragment[XAVIX_VIDEO_FRAGMENT_BYTES] = { 0 };
	uint8_t palette_sh[XAVIX_VIDEO_PALETTE_ENTRIES] = { 0 };
	uint8_t palette_l[XAVIX_VIDEO_PALETTE_ENTRIES] = { 0 };
	uint8_t segments[XAVIX_VIDEO_SEGMENT_BYTES] = { 0 };
	uint8_t tile0[8] = { 0 };
	uint8_t tile1[8] = { 0 };
	test_rom rom;
	static uint32_t first_frame[XAVIX_VIDEO_PIXELS];
	uint32_t first_color;
	uint32_t second_color;

	memset(&input, 0, sizeof(input));
	memset(&rom, 0, sizeof(rom));
	xavix_video_init(&video);
	rom.base = 0x800000;
	memset(rom.bytes + 8, 0xff, 8);
	memset(rom.bytes + 16, 0xff, 8);
	/* Output row zero is native XaviX scanline 16 (tilemap row two). */
	ram[0x100 + 64] = 1;
	ram[0x200 + 64] = 2;
	segments[0] = 0x00;
	segments[1] = 0x80;
	make_opaque_pen(palette_sh, palette_l, 1, 0);
	make_opaque_pen(palette_sh, palette_l, 17, 8);
	first_color = xavix_video_palette_decode(palette_sh[1], palette_l[1], NULL);
	second_color = xavix_video_palette_decode(palette_sh[17], palette_l[17], NULL);

	tile0[0] = 0x01;
	tile0[6] = 0x02; /* palette 0, priority 2 */
	tile0[7] = 0x80; /* 8-bit tile number, 1bpp, enabled */
	tile1[0] = 0x02;
	tile1[6] = 0x11; /* palette 1, priority 1 */
	tile1[7] = 0x80;

	input.main_ram = ram;
	input.main_ram_size = sizeof(ram);
	input.fragment_ram = fragment;
	input.palette_sh = palette_sh;
	input.palette_l = palette_l;
	input.palette_entries = XAVIX_VIDEO_PALETTE_ENTRIES;
	input.segment_regs = segments;
	input.tilemap_regs[0] = tile0;
	input.tilemap_regs[1] = tile1;
	input.sprite_mode = 0x04;
	input.read_program_byte = read_test_rom;
	input.read_program_opaque = &rom;

	xavix_video_render(&video, &input);
	CHECK(video.framebuffer[0] == first_color);
	memcpy(first_frame, video.framebuffer, sizeof(first_frame));
	xavix_video_render(&video, &input);
	CHECK(memcmp(first_frame, video.framebuffer, sizeof(first_frame)) == 0);
	tile1[6] = 0x12; /* equal priority: later tilemap wins */
	xavix_video_render(&video, &input);
	CHECK(video.framebuffer[0] == second_color);

	input.arena_control = 1;
	input.arena_start = 100;
	input.arena_end = 10;
	xavix_video_render(&video, &input);
	CHECK(video.report.hardware_clip.valid);
	CHECK(video.report.hardware_clip.min_x == 8);
	CHECK(video.report.hardware_clip.max_x == 97);
	CHECK(video.framebuffer[0] == UINT32_C(0xff000000));
}

static void test_partial_tilemap_uses_absolute_raster_position(void)
{
	static xavix_video video;
	xavix_video_inputs input;
	uint8_t ram[0x4000] = { 0 };
	uint8_t palette_sh[XAVIX_VIDEO_PALETTE_ENTRIES] = { 0 };
	uint8_t palette_l[XAVIX_VIDEO_PALETTE_ENTRIES] = { 0 };
	uint8_t segments[XAVIX_VIDEO_SEGMENT_BYTES] = { 0 };
	uint8_t tile0[8] = { 0 };
	test_rom rom;
	uint32_t color;

	memset(&input, 0, sizeof(input));
	memset(&rom, 0, sizeof(rom));
	xavix_video_init(&video);
	rom.base = 0x800000;
	memset(rom.bytes + 8, 0xff, 8);
	ram[0x100 + 64] = 1;
	ram[0x100 + 128] = 1;
	segments[0] = 0x00;
	segments[1] = 0x80;
	make_opaque_pen(palette_sh, palette_l, 1, 0);
	color = xavix_video_palette_decode(palette_sh[1], palette_l[1], NULL);
	tile0[0] = 0x01;
	tile0[6] = 0x02;
	tile0[7] = 0x80;
	input.main_ram = ram;
	input.main_ram_size = sizeof(ram);
	input.palette_sh = palette_sh;
	input.palette_l = palette_l;
	input.palette_entries = XAVIX_VIDEO_PALETTE_ENTRIES;
	input.segment_regs = segments;
	input.tilemap_regs[0] = tile0;
	input.read_program_byte = read_test_rom;
	input.read_program_opaque = &rom;

	xavix_video_begin_frame(&video);
	xavix_video_render_range(&video, &input, 16, 31);
	xavix_video_render_range(&video, &input, 32, 47);
	xavix_video_end_frame(&video);
	CHECK(video.framebuffer[0] == color);
	CHECK(video.framebuffer[16 * XAVIX_VIDEO_WIDTH] == color);
	CHECK(video.framebuffer[32 * XAVIX_VIDEO_WIDTH] == UINT32_C(0xff000000));
}
static void test_feather_visible_pixel_bounds(void)
{
	static xavix_video video;
	xavix_video_inputs input;
	uint8_t fragment[XAVIX_VIDEO_FRAGMENT_BYTES] = { 0 };
	uint8_t palette_sh[256] = { 0 };
	uint8_t palette_l[256] = { 0 };
	test_rom rom;
	xavix_video_bounds bounds;
	const unsigned slot = 1;
	const uint32_t address = UINT32_C(0xa17d80);

	memset(&input, 0, sizeof(input));
	memset(&rom, 0, sizeof(rom));
	xavix_video_init(&video);
	rom.base = address;
	memset(rom.bytes, 0xff, 8);
	make_opaque_pen(palette_sh, palette_l, 1, 12);
	fragment[0x100 + slot] = 0x30; /* priority 3, 8x8 */
	fragment[0x500 + slot] = (uint8_t)address;
	fragment[0x600 + slot] = (uint8_t)(address >> 8);
	fragment[0x700 + slot] = (uint8_t)(address >> 16);
	set_sprite_position(fragment, slot, 10,
		20 + XAVIX_VIDEO_VISIBLE_Y_START, 8, 8);

	input.fragment_ram = fragment;
	input.palette_sh = palette_sh;
	input.palette_l = palette_l;
	input.palette_entries = 256;
	input.sprite_mode = 0x04;
	input.read_program_byte = read_test_rom;
	input.read_program_opaque = &rom;
	xavix_video_render(&video, &input);

	CHECK(xavix_video_feather_visible(&video));
	CHECK(xavix_video_feather_bounds(&video, &bounds));
	CHECK(bounds.min_x == 10 && bounds.max_x == 17);
	CHECK(bounds.min_y == 20 && bounds.max_y == 27);
	CHECK(video.report.watched_sprite_pixels == 64);

	/* Lower-numbered slots draw later.  An opaque equal-priority sprite must
	 * remove covered pixels from the final visible-feather report. */
	fragment[0x000 + slot] = 0x00; /* watched pen 1 */
	fragment[0x000 + 0] = 0x20; /* non-watched pen 33 */
	make_opaque_pen(palette_sh, palette_l, 33, 4);
	fragment[0x100 + 0] = 0x30;
	fragment[0x500 + 0] = (uint8_t)(address + 0x40U);
	fragment[0x600 + 0] = (uint8_t)((address + 0x40U) >> 8);
	fragment[0x700 + 0] = (uint8_t)((address + 0x40U) >> 16);
	set_sprite_position(fragment, 0, 10,
		20 + XAVIX_VIDEO_VISIBLE_Y_START, 8, 8);
	memset(rom.bytes + 0x40, 0xff, 8);
	xavix_video_render(&video, &input);
	CHECK(!xavix_video_feather_visible(&video));
	CHECK(video.report.watched_sprite_pixels == 0);

	/* Star Wars US and Japan use separate cursor graphics ranges.  Confirm
	 * that a match in the optional second range is reported independently. */
	{
		xavix_video_sprite_watch watch;
		memset(&watch, 0, sizeof(watch));
		watch.first_address = address + 0x80U;
		watch.last_address = address + 0x80U;
		watch.second_first_address = address;
		watch.second_last_address = address;
		watch.required_sprite_mode = 0x04;
		watch.enabled = 1;
		xavix_video_set_sprite_watch(&video, &watch);
		fragment[0x100] = 0;
		xavix_video_render(&video, &input);
		CHECK(xavix_video_feather_visible(&video));
		CHECK(video.report.watched_sprite_pixels == 64);
	}
}

static void test_colmix_layer_enables(void)
{
	static xavix_video video;
	xavix_video_inputs input;
	uint8_t palette_sh[256] = { 0 };
	uint8_t palette_l[256] = { 0 };

	memset(&input, 0, sizeof(input));
	xavix_video_init(&video);
	input.palette_sh = palette_sh;
	input.palette_l = palette_l;
	input.flags = XAVIX_VIDEO_INPUT_COLMIX_ENABLES_VALID;
	input.colmix_control = 0;
	xavix_video_render(&video, &input);
	CHECK(video.report.opaque_pixels_drawn == 0);
}

int main(void)
{
	test_fragment_write_alias();
	test_tile_priority_and_clip();
	test_partial_tilemap_uses_absolute_raster_position();
	test_feather_visible_pixel_bounds();
	test_colmix_layer_enables();
	puts("xavix_video_test: ok");
	return 0;
}
