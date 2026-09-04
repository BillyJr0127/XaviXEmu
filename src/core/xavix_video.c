// SPDX-License-Identifier: BSD-3-Clause
// MAME-derived portions copyright-holder: David Haywood
// XaviXEmu port and modifications:
// Copyright (c) 2026 Billy Jr. and contributors
/*
 * Compact C port of the game-used XaviX/SSD 2000 video paths.
 *
 * Rendering semantics and the Y/C/H palette approximation are derived from
 * David Haywood's BSD-3-Clause MAME implementation in xavix_v.cpp.  This port
 * deliberately owns no CPU, address space, screen, palette, or ROM objects.
 */

#include "xavix_video.h"

#include <string.h>

enum
{
	OWNER_NONE = 0,
	OWNER_WATCHED_SPRITE = 1,
	INLINE_HEADER_LIMIT = 256
};

typedef struct render_context
{
	xavix_video *video;
	const xavix_video_inputs *input;
	xavix_video_bounds clip;
} render_context;

typedef struct bit_reader
{
	render_context *render;
	uint32_t address;
	uint8_t bit;
	uint8_t byte;
	uint8_t loaded;
} bit_reader;

static const double s_hues[32][3] =
{
	{ 1.00, 0.00, 0.00 }, { 1.00, 0.25, 0.00 },
	{ 1.00, 0.50, 0.00 }, { 1.00, 0.75, 0.00 },
	{ 1.00, 1.00, 0.00 }, { 0.75, 1.00, 0.00 },
	{ 0.50, 1.00, 0.00 }, { 0.25, 1.00, 0.00 },
	{ 0.00, 1.00, 0.00 }, { 0.00, 1.00, 0.25 },
	{ 0.00, 1.00, 0.50 }, { 0.00, 1.00, 0.75 },
	{ 0.00, 1.00, 1.00 }, { 0.00, 0.75, 1.00 },
	{ 0.00, 0.50, 1.00 }, { 0.00, 0.25, 1.00 },
	{ 0.00, 0.00, 1.00 }, { 0.25, 0.00, 1.00 },
	{ 0.50, 0.00, 1.00 }, { 0.75, 0.00, 1.00 },
	{ 1.00, 0.00, 1.00 }, { 1.00, 0.00, 0.75 },
	{ 1.00, 0.00, 0.50 }, { 1.00, 0.00, 0.25 },
	{ 0.00, 0.00, 0.00 }, { 0.00, 0.00, 0.00 },
	{ 0.00, 0.00, 0.00 }, { 0.00, 0.00, 0.00 },
	{ 0.00, 0.00, 0.00 }, { 0.00, 0.00, 0.00 },
	{ 0.00, 0.00, 0.00 }, { 0.00, 0.00, 0.00 }
};

static uint8_t clamp_component(double value)
{
	if (value <= 0.0)
		return 0;
	if (value >= 1.0)
		return 255;
	return (uint8_t)(value * 255.0);
}

uint32_t xavix_video_palette_decode(uint8_t sh, uint8_t l, int *opaque)
{
	const uint16_t packed = (uint16_t)sh | ((uint16_t)l << 8);
	const unsigned y_raw = ((((packed & 0x1f00U) >> 7) |
		((packed & 0x8000U) >> 15)) >> 1) & 31U;
	const unsigned c_raw = ((((packed & 0x00e0U) >> 4) |
		((packed & 0x4000U) >> 14)) >> 1) & 7U;
	const unsigned h_raw = ((((packed & 0x001fU) << 1) |
		((packed & 0x2000U) >> 13)) >> 1) & 31U;
	double y = (double)y_raw / 20.0;
	double c = (double)c_raw / 5.0;
	const double r0 = s_hues[h_raw][0];
	const double g0 = s_hues[h_raw][1];
	const double b0 = s_hues[h_raw][2];
	const double z = (0.299 * r0) + (0.587 * g0) + (0.114 * b0);
	double r;
	double g;
	double b;

	if (y < z && z != 0.0)
		c *= y / z;
	else if (z < 1.0)
		c *= (1.0 - y) / (1.0 - z);

	r = ((r0 - z) * c + y) * 0.92;
	g = ((g0 - z) * c + y) * 0.92;
	b = ((b0 - z) * c + y) * 0.92;

	if (opaque)
		*opaque = ((sh & 0x1fU) < 24U);

	return UINT32_C(0xff000000) |
		((uint32_t)clamp_component(r) << 16) |
		((uint32_t)clamp_component(g) << 8) |
		(uint32_t)clamp_component(b);
}

static void bounds_clear(xavix_video_bounds *bounds)
{
	bounds->min_x = 0;
	bounds->min_y = 0;
	bounds->max_x = 0;
	bounds->max_y = 0;
	bounds->valid = 0;
}

static void bounds_include(xavix_video_bounds *bounds, int x, int y)
{
	if (!bounds->valid)
	{
		bounds->min_x = (int16_t)x;
		bounds->max_x = (int16_t)x;
		bounds->min_y = (int16_t)y;
		bounds->max_y = (int16_t)y;
		bounds->valid = 1;
		return;
	}

	if (x < bounds->min_x)
		bounds->min_x = (int16_t)x;
	if (x > bounds->max_x)
		bounds->max_x = (int16_t)x;
	if (y < bounds->min_y)
		bounds->min_y = (int16_t)y;
	if (y > bounds->max_y)
		bounds->max_y = (int16_t)y;
}

void xavix_video_watch_drgqst_feather(xavix_video *video)
{
	if (!video)
		return;

	memset(&video->sprite_watch, 0, sizeof(video->sprite_watch));
	video->sprite_watch.first_address = UINT32_C(0xa17d80);
	video->sprite_watch.last_address = UINT32_C(0xa18080);
	video->sprite_watch.address_stride = 0x60;
	video->sprite_watch.required_sprite_mode = 0x04;
	video->sprite_watch.enabled = 1;
}

void xavix_video_set_sprite_watch(xavix_video *video, const xavix_video_sprite_watch *watch)
{
	if (!video)
		return;
	if (watch)
		video->sprite_watch = *watch;
	else
		memset(&video->sprite_watch, 0, sizeof(video->sprite_watch));
}

void xavix_video_reset(xavix_video *video)
{
	xavix_video_sprite_watch watch;

	if (!video)
		return;

	watch = video->sprite_watch;
	memset(video, 0, sizeof(*video));
	video->sprite_watch = watch;
}

void xavix_video_init(xavix_video *video)
{
	if (!video)
		return;
	memset(video, 0, sizeof(*video));
	xavix_video_watch_drgqst_feather(video);
}

const uint32_t *xavix_video_framebuffer(const xavix_video *video)
{
	return video ? video->framebuffer : NULL;
}

const xavix_video_frame_report *xavix_video_last_report(const xavix_video *video)
{
	return video ? &video->report : NULL;
}

int xavix_video_feather_visible(const xavix_video *video)
{
	return video && video->report.watched_sprite_visible;
}

int xavix_video_feather_bounds(const xavix_video *video, xavix_video_bounds *bounds)
{
	if (!video || !video->report.watched_sprite_visible)
	{
		if (bounds)
			bounds_clear(bounds);
		return 0;
	}

	if (bounds)
		*bounds = video->report.watched_sprite_bounds;
	return 1;
}

static void set_fragment_high_x(uint8_t *fragment_ram, unsigned index, int high)
{
	index &= 0xffU;
	fragment_ram[0x000U + index] =
		(uint8_t)((fragment_ram[0x000U + index] & 0xfeU) | (high ? 1U : 0U));
	fragment_ram[0x400U + index] = high ? 1U : 0U;
}

void xavix_video_fragment_write(uint8_t fragment_ram[XAVIX_VIDEO_FRAGMENT_BYTES],
	uint16_t offset, uint8_t data)
{
	if (!fragment_ram)
		return;

	offset &= 0x07ffU;
	if (offset < 0x0100U)
	{
		fragment_ram[offset] = data & 0xfeU;
		set_fragment_high_x(fragment_ram, offset, data & 0x01U);
	}
	else if (offset < 0x0300U)
	{
		fragment_ram[offset] = data;
	}
	else if (offset < 0x0400U)
	{
		fragment_ram[offset] = data;
		set_fragment_high_x(fragment_ram, offset, data & 0x80U);
	}
	else if (offset < 0x0500U)
	{
		set_fragment_high_x(fragment_ram, offset, data & 0x01U);
	}
	else
	{
		fragment_ram[offset] = data;
	}
}

static uint8_t read_byte(render_context *render, uint32_t address)
{
	const xavix_video_inputs *input = render->input;

	address &= UINT32_C(0x00ffffff);
	render->video->report.memory_reads++;

	if (input->main_ram && address < input->main_ram_size)
		return input->main_ram[address];
	if (input->fragment_ram && address >= 0x6000U && address < 0x6800U)
		return input->fragment_ram[address - 0x6000U];
	if (input->palette_sh && address >= 0x6800U && address < 0x6900U)
		return input->palette_sh[address - 0x6800U];
	if (input->palette_l && address >= 0x6900U && address < 0x6a00U)
		return input->palette_l[address - 0x6900U];
	if (input->segment_regs && address >= 0x6a00U && address < 0x6a20U)
		return input->segment_regs[address - 0x6a00U];
	if (input->read_program_byte)
		return input->read_program_byte(input->read_program_opaque, address);
	return 0;
}

static uint32_t segment_base(const xavix_video_inputs *input, unsigned segment)
{
	if (!input->segment_regs || segment >= 16U)
		return 0;
	return ((uint32_t)input->segment_regs[(segment * 2U) + 1U] << 16) |
		((uint32_t)input->segment_regs[segment * 2U] << 8);
}

static void bit_reader_init(bit_reader *reader, render_context *render,
	uint32_t address, unsigned bit)
{
	reader->render = render;
	reader->address = address + (bit >> 3);
	reader->bit = (uint8_t)(bit & 7U);
	reader->byte = 0;
	reader->loaded = 0;
}

static unsigned bit_reader_next(bit_reader *reader)
{
	unsigned result;

	if (!reader->loaded)
	{
		reader->byte = read_byte(reader->render, reader->address);
		reader->loaded = 1;
	}
	result = (reader->byte >> reader->bit) & 1U;

	reader->bit++;
	if (reader->bit == 8U)
	{
		reader->bit = 0;
		reader->address++;
		reader->loaded = 0;
	}
	return result;
}

static uint8_t bit_reader_bits(bit_reader *reader, unsigned count)
{
	uint8_t result = 0;
	unsigned bit;

	for (bit = 0; bit < count; ++bit)
		result = (uint8_t)(result | (bit_reader_next(reader) << bit));
	return result;
}

static uint8_t bit_reader_byte(bit_reader *reader)
{
	return bit_reader_bits(reader, 8);
}

static int point_in_clip(const xavix_video_bounds *clip, int x, int y)
{
	return clip->valid && x >= clip->min_x && x <= clip->max_x &&
		y >= clip->min_y && y <= clip->max_y;
}

static void put_pixel(render_context *render, int x, int y, unsigned pen,
	unsigned priority, uint8_t owner)
{
	xavix_video *video = render->video;
	unsigned position;
	unsigned output_y;

	if (!point_in_clip(&render->clip, x, y))
		return;
	pen &= 0x1ffU;
	if (!video->palette_opaque[pen])
		return;

	output_y = (unsigned)(y - XAVIX_VIDEO_VISIBLE_Y_START);
	position = output_y * XAVIX_VIDEO_WIDTH + (unsigned)x;
	if (priority < video->zbuffer[position])
		return;

	video->framebuffer[position] = video->palette_argb[pen];
	video->zbuffer[position] = (uint8_t)priority;
	video->pixel_owner[position] = owner;
	video->report.opaque_pixels_drawn++;
}

static void draw_gfx_line(render_context *render, uint32_t address,
	unsigned bpp, int x, int y, unsigned height, unsigned width,
	int flip_x, int flip_y, unsigned palette, unsigned priority,
	unsigned source_line, uint8_t owner)
{
	bit_reader reader;
	unsigned source_x;

	if (!render->clip.valid || y < render->clip.min_y || y > render->clip.max_y ||
		x > render->clip.max_x || x + (int)width - 1 < render->clip.min_x)
		return;

	if (bpp > 4U)
		palette &= 0x0fU << (bpp - 4U);
	if (flip_y)
		source_line = height - 1U - source_line;

	bit_reader_init(&reader, render, address,
		source_line * width * bpp);
	for (source_x = 0; source_x < width; ++source_x)
	{
		const uint8_t pixel = bit_reader_bits(&reader, bpp);
		const int destination_x = flip_x ?
			x + (int)width - 1 - (int)source_x : x + (int)source_x;
		const unsigned pen = (pixel + (palette << 4)) & 0xffU;

		put_pixel(render, destination_x, y, pen, priority, owner);
	}
}

static int decode_inline_header(render_context *render, uint32_t *address,
	int *flip_x, int *flip_y, unsigned *palette)
{
	bit_reader reader;
	unsigned count;
	int first = 1;

	bit_reader_init(&reader, render, *address, 0);
	*flip_x = 0;
	*flip_y = 0;

	for (count = 0; count < INLINE_HEADER_LIMIT; ++count)
	{
		const uint8_t value = bit_reader_byte(&reader);

		if (first)
		{
			*palette = value >> 4;
			switch (value & 0x0fU)
			{
			case 0x01:
			case 0x03:
				break;
			case 0x05:
			case 0x07:
				*flip_x = 1;
				break;
			case 0x09:
			case 0x0b:
				*flip_y = 1;
				break;
			case 0x0d:
			case 0x0f:
				*flip_x = 1;
				*flip_y = 1;
				break;
			default:
				break;
			}
			first = 0;
		}

		if ((value & 0x0fU) == 0x06U)
		{
			*address = reader.address;
			return 1;
		}
	}

	render->video->report.malformed_inline_headers++;
	return 0;
}

static void draw_tilemap_line(render_context *render, unsigned which, int line)
{
	const xavix_video_inputs *input = render->input;
	const uint8_t *regs = input->tilemap_regs[which];
	unsigned y_dimension;
	unsigned x_dimension;
	unsigned tile_height;
	unsigned tile_width;
	unsigned y_shift;
	unsigned draw_line;
	unsigned map_y;
	unsigned source_line;
	unsigned x;
	unsigned addressing = 0;
	const int inline_header = regs && (regs[7] & 0x10U);

	if (!regs || !(regs[7] & 0x80U))
		return;

	switch (regs[3] & 0x30U)
	{
	default:
	case 0x00:
		y_dimension = 32; x_dimension = 32;
		tile_height = 8; tile_width = 8; y_shift = 3;
		break;
	case 0x10:
		y_dimension = 32; x_dimension = 16;
		tile_height = 8; tile_width = 16; y_shift = 3;
		break;
	case 0x20:
		y_dimension = 16; x_dimension = 32;
		tile_height = 16; tile_width = 8; y_shift = 4;
		break;
	case 0x30:
		y_dimension = 16; x_dimension = 16;
		tile_height = 16; tile_width = 16; y_shift = 4;
		break;
	}

	if (regs[7] & 0x02U)
		addressing = 1;
	if ((regs[7] & 0x7fU) == 0x04U)
		addressing = 2;

	draw_line = ((unsigned)line + regs[5]) &
		((y_dimension * tile_height) - 1U);
	map_y = draw_line >> y_shift;
	source_line = draw_line & (tile_height - 1U);

	for (x = 0; x < x_dimension; ++x)
	{
		const unsigned entry = map_y * x_dimension + x;
		uint32_t tile = 0;
		unsigned bpp = ((regs[3] & 0x0eU) >> 1) + 1U;
		unsigned palette = regs[6] >> 4;
		const unsigned priority = regs[6] & 0x0fU;
		const int scroll_x = regs[4];
		int flip_x = (regs[3] >> 6) & 1U;
		int flip_y = (regs[3] >> 7) & 1U;

		render->video->report.tile_entries_visited[which]++;
		if (regs[0] != 0)
			tile |= read_byte(render, ((uint32_t)regs[0] << 8) + entry);
		if ((regs[7] & 0x7fU) != 0x00U &&
			(regs[7] & 0x7fU) != 0x08U)
			tile |= (uint32_t)read_byte(render,
				((uint32_t)regs[1] << 8) + entry) << 8;
		if (addressing == 2U)
			tile |= (uint32_t)read_byte(render,
				((uint32_t)regs[2] << 8) + entry) << 16;

		if (tile == 0)
			continue;

		if (!inline_header)
		{
			if (addressing == 0U)
			{
				const unsigned bytes_per_tile =
					(tile_height * tile_width * bpp) / 8U;
				tile = segment_base(input, 0) + tile * bytes_per_tile;
			}
			else if (addressing == 1U)
			{
				unsigned segment;
				if (regs[7] & 0x01U)
				{
					segment = (tile >> 12) & 0x0fU;
					tile = segment_base(input, segment) + (tile & 0x0fffU);
				}
				else
				{
					segment = (tile >> 13) & 0x07U;
					tile = segment_base(input, segment) +
						((tile & 0x1fffU) * 8U);
				}
			}

			if (regs[7] & 0x08U)
			{
				const uint8_t attribute = read_byte(render,
					((uint32_t)regs[2] << 8) + entry);
				palette = attribute >> 4;
				/* Attribute priority replaces the layer-wide value. */
				draw_gfx_line(render, tile, bpp,
					(int)(x * tile_width) + scroll_x, line,
					tile_height, tile_width, flip_x, flip_y,
					palette, attribute & 0x0fU, source_line, OWNER_NONE);
				draw_gfx_line(render, tile, bpp,
					(int)(x * tile_width) + scroll_x - 256, line,
					tile_height, tile_width, flip_x, flip_y,
					palette, attribute & 0x0fU, source_line, OWNER_NONE);
				continue;
			}
		}
		else
		{
			if (regs[7] == 0x94U)
				tile |= (uint32_t)read_byte(render,
					((uint32_t)regs[2] << 8) + entry) << 16;
			else
			{
				const unsigned segment = (tile >> 12) & 0x0fU;
				tile = segment_base(input, segment) + (tile & 0x0fffU);
			}
			if (!decode_inline_header(render, &tile, &flip_x, &flip_y, &palette))
				continue;
		}

		draw_gfx_line(render, tile, bpp,
			(int)(x * tile_width) + scroll_x, line,
			tile_height, tile_width, flip_x, flip_y,
			palette, priority, source_line, OWNER_NONE);
		draw_gfx_line(render, tile, bpp,
			(int)(x * tile_width) + scroll_x - 256, line,
			tile_height, tile_width, flip_x, flip_y,
			palette, priority, source_line, OWNER_NONE);
	}
}

static void draw_tilemap(render_context *render, unsigned which)
{
	int y;

	for (y = render->clip.min_y; y <= render->clip.max_y; ++y)
		draw_tilemap_line(render, which, y);
}

static int address_matches_watch(uint32_t address, uint32_t first,
	uint32_t last, uint16_t stride)
{
	if (!first || address < first || address > last)
		return 0;
	if (!stride)
		return address == first;
	return ((address - first) % stride) == 0;
}

static int sprite_is_watched(const xavix_video *video, uint8_t mode,
	uint32_t address)
{
	const xavix_video_sprite_watch *watch = &video->sprite_watch;

	if (!watch->enabled || mode != watch->required_sprite_mode)
		return 0;
	return address_matches_watch(address, watch->first_address,
		watch->last_address, watch->address_stride) ||
		address_matches_watch(address, watch->second_first_address,
		watch->second_last_address, watch->second_address_stride);
}

static void draw_sprites(render_context *render)
{
	const xavix_video_inputs *input = render->input;
	const uint8_t *fragment = input->fragment_ram;
	const uint8_t mode = input->sprite_mode & 0x7fU;
	unsigned addressing;
	int slot;

	if (!fragment)
		return;

	switch (mode)
	{
	case 0x00:
	case 0x01:
	case 0x05:
		addressing = 1;
		break;
	case 0x02:
		addressing = 2;
		break;
	case 0x04:
		addressing = 0;
		break;
	case 0x07:
		addressing = 3;
		break;
	default:
		addressing = 0;
		render->video->report.sprite_mode_supported = 0;
		break;
	}

	for (slot = 0xff; slot >= 0; --slot)
	{
		const uint8_t attr0 = fragment[0x000 + slot];
		const uint8_t attr1 = fragment[0x100 + slot];
		uint32_t address = fragment[0x500 + slot];
		uint32_t raw_address;
		unsigned bpp = ((attr0 & 0x0eU) >> 1) + 1U;
		const unsigned palette = attr0 >> 4;
		const unsigned priority = attr1 >> 4;
		const int flip_x = attr1 & 0x01U;
		const int flip_y = attr1 & 0x02U;
		const unsigned width = (attr1 & 0x04U) ? 16U : 8U;
		const unsigned height = (attr1 & 0x08U) ? 16U : 8U;
		int x;
		int y;
		unsigned line;
		uint8_t owner;

		render->video->report.sprites_visited++;
		if (mode == 0x04U || mode == 0x07U || mode == 0x15U)
			address |= (uint32_t)fragment[0x700 + slot] << 16;
		if (mode != 0x00U)
			address |= (uint32_t)fragment[0x600 + slot] << 8;
		raw_address = address;

		y = fragment[0x200 + slot] ^ 0xff;
		if (y & 0x80)
			y = -0x80 + (y & 0x7f);
		else
			y &= 0x7f;
		y += 129 - (int)(height / 2U);

		x = fragment[0x300 + slot] |
		((fragment[0x400 + slot] & 1U) << 8);
		x ^= 0x100;
		x -= 0x80 + (int)(width / 2U);

		if (addressing == 1U)
			address = segment_base(input, 0) +
				(address * width * height * bpp) / 8U;
		else if (addressing == 2U)
		{
			const uint32_t byte_address = address * 8U;
			const unsigned segment = (byte_address >> 16) & 0x0fU;
			address = segment_base(input, segment) + (byte_address & 0xffffU);
		}
		else if (addressing == 3U)
			address *= 8U;

		owner = sprite_is_watched(render->video, mode, raw_address) ?
			OWNER_WATCHED_SPRITE : OWNER_NONE;
		for (line = 0; line < height; ++line)
			draw_gfx_line(render, address, bpp, x, y + (int)line,
				height, width, flip_x, flip_y, palette, priority, line, owner);
	}
}

static void rebuild_palette(xavix_video *video, const xavix_video_inputs *input)
{
	unsigned count = input->palette_entries ? input->palette_entries : 256U;
	unsigned index;
	int opaque;

	if (count > XAVIX_VIDEO_PALETTE_ENTRIES)
		count = XAVIX_VIDEO_PALETTE_ENTRIES;
	for (index = 0; index < count; ++index)
	{
		const uint8_t sh = input->palette_sh ? input->palette_sh[index] : 0;
		const uint8_t l = input->palette_l ? input->palette_l[index] : 0;
		video->palette_argb[index] = xavix_video_palette_decode(sh, l, &opaque);
		video->palette_opaque[index] = (uint8_t)opaque;
	}
	for (; index < XAVIX_VIDEO_PALETTE_ENTRIES; ++index)
	{
		video->palette_argb[index] = UINT32_C(0xff000000);
		video->palette_opaque[index] = 0;
	}
	video->report.colmix_argb = xavix_video_palette_decode(
		input->colmix_sh, input->colmix_l, NULL);
}

static xavix_video_bounds make_hardware_clip(const xavix_video_inputs *input)
{
	xavix_video_bounds clip;

	clip.min_x = 0;
	clip.min_y = XAVIX_VIDEO_VISIBLE_Y_START;
	clip.max_x = XAVIX_VIDEO_WIDTH - 1;
	clip.max_y = XAVIX_VIDEO_VISIBLE_Y_END;
	clip.valid = 1;

	if ((input->arena_control & 0x01U) && input->arena_start != 0x00U &&
		input->arena_end != 0x00U && input->arena_start != 0xffU &&
		input->arena_end != 0xffU)
	{
		clip.max_x = (int16_t)((int)input->arena_start - 3);
		clip.min_x = (int16_t)((int)input->arena_end - 2);
		if (clip.min_x < 0)
			clip.min_x = 0;
		if (clip.max_x >= XAVIX_VIDEO_WIDTH)
			clip.max_x = XAVIX_VIDEO_WIDTH - 1;
		if (clip.min_x > clip.max_x)
			clip.valid = 0;
	}
	return clip;
}

static void finish_watched_sprite_report(xavix_video *video)
{
	unsigned position;

	bounds_clear(&video->report.watched_sprite_bounds);
	video->report.watched_sprite_pixels = 0;
	for (position = 0; position < XAVIX_VIDEO_PIXELS; ++position)
	{
		if (video->pixel_owner[position] == OWNER_WATCHED_SPRITE)
		{
			const int x = (int)(position % XAVIX_VIDEO_WIDTH);
			const int y = (int)(position / XAVIX_VIDEO_WIDTH);
			bounds_include(&video->report.watched_sprite_bounds, x, y);
			video->report.watched_sprite_pixels++;
		}
	}
	video->report.watched_sprite_visible =
		video->report.watched_sprite_bounds.valid;
}

void xavix_video_begin_frame(xavix_video *video)
{
	unsigned position;

	if (!video)
		return;
	memset(&video->report, 0, sizeof(video->report));
	video->report.sprite_mode_supported = 1;
	for (position = 0; position < XAVIX_VIDEO_PIXELS; ++position)
	{
		video->framebuffer[position] = UINT32_C(0xff000000);
		video->zbuffer[position] = 0;
		video->pixel_owner[position] = OWNER_NONE;
	}
}

static void include_clip(xavix_video_bounds *total,
	const xavix_video_bounds *part)
{
	if (!part->valid)
		return;
	if (!total->valid)
	{
		*total = *part;
		return;
	}
	if (part->min_x < total->min_x)
		total->min_x = part->min_x;
	if (part->min_y < total->min_y)
		total->min_y = part->min_y;
	if (part->max_x > total->max_x)
		total->max_x = part->max_x;
	if (part->max_y > total->max_y)
		total->max_y = part->max_y;
}

void xavix_video_render_range(xavix_video *video,
	const xavix_video_inputs *inputs, int min_y, int max_y)
{
	render_context render;
	uint32_t background;
	uint8_t layer_mask = XAVIX_VIDEO_COLMIX_TILEMAP_0 |
		XAVIX_VIDEO_COLMIX_TILEMAP_1 | XAVIX_VIDEO_COLMIX_SPRITES;

	if (!video || !inputs || min_y > max_y)
		return;

	rebuild_palette(video, inputs);
	render.video = video;
	render.input = inputs;
	render.clip = make_hardware_clip(inputs);
	if (min_y > render.clip.min_y)
		render.clip.min_y = (int16_t)min_y;
	if (max_y < render.clip.max_y)
		render.clip.max_y = (int16_t)max_y;
	if (render.clip.min_y > render.clip.max_y)
		render.clip.valid = 0;

	include_clip(&video->report.hardware_clip, &render.clip);
	background = video->palette_argb[0];

	if (render.clip.valid)
	{
		int y;
		for (y = render.clip.min_y; y <= render.clip.max_y; ++y)
		{
			int x;
			for (x = render.clip.min_x; x <= render.clip.max_x; ++x)
				video->framebuffer[
					(unsigned)(y - XAVIX_VIDEO_VISIBLE_Y_START) * XAVIX_VIDEO_WIDTH +
					(unsigned)x] =
					background;
		}
	}

	if (inputs->flags & XAVIX_VIDEO_INPUT_COLMIX_ENABLES_VALID)
		layer_mask = inputs->colmix_control & 0x07U;
	if (render.clip.valid && (layer_mask & XAVIX_VIDEO_COLMIX_TILEMAP_0))
		draw_tilemap(&render, 0);
	if (render.clip.valid && (layer_mask & XAVIX_VIDEO_COLMIX_TILEMAP_1))
		draw_tilemap(&render, 1);
	if (render.clip.valid && (layer_mask & XAVIX_VIDEO_COLMIX_SPRITES))
		draw_sprites(&render);
}

const xavix_video_frame_report *xavix_video_end_frame(xavix_video *video)
{
	if (!video)
		return NULL;
	finish_watched_sprite_report(video);
	return &video->report;
}

const xavix_video_frame_report *xavix_video_render(
	xavix_video *video, const xavix_video_inputs *inputs)
{

	if (!video || !inputs)
		return NULL;
	xavix_video_begin_frame(video);
	xavix_video_render_range(video, inputs, XAVIX_VIDEO_VISIBLE_Y_START,
		XAVIX_VIDEO_VISIBLE_Y_END);
	return xavix_video_end_frame(video);
}
