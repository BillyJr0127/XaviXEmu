// SPDX-License-Identifier: BSD-3-Clause
// MAME-derived portions copyright-holder: David Haywood
// XaviXEmu adaptation and modifications:
// Copyright (c) 2026 Billy Jr. and contributors

#include "xavix2_machine.h"

#include <stdlib.h>
#include <string.h>

/* Take-copter's origin-0110 form of mode 0x1608 is a 640-by-240 logical
 * surface whose scan lines are sent twice to the 480-line television output.
 * Keep the doubled host surface
 * outside xavix2_machine_t: it is presentation-only state and must not make
 * runtime saves another 1.2 MiB larger. */
static uint32_t line_doubled_frame[640 * 480];

/* The optional host-only 2x target is deliberately kept out of the emulated
 * machine structure.  Save states therefore remain hardware-only and retain
 * their existing format.  The largest native XaviX2 mode is 640x480, so its
 * enhanced presentation target is 1280x960. */
typedef struct high_resolution_3d_state
{
	const xavix2_machine_t *owner;
	int enabled;
	unsigned presented_scale;
} high_resolution_3d_state;

static high_resolution_3d_state high_resolution_3d;
static uint32_t high_resolution_3d_frame[1280 * 960];
static const xavix2_machine_t *skip_render_owner;
static int skip_render_enabled;
/* Cache the internal polygon bit once per native source pixel before the 2x
 * presentation pass.  Testing the packed internal mask four times for every
 * output pixel dominated the optional enhancement path at 640x480 and made
 * otherwise-fast frames miss their 16.7 ms presentation deadline. */
static uint8_t high_resolution_3d_visible_mask[640 * 480];
/* One bit per native internal pixel records the final layer as polygonal.
 * The fast enhancement pass smooths only those pixels, leaving 2D sprites,
 * text and HUD art pixel-perfect. */
static uint8_t high_resolution_3d_polygon_mask[(0x800 * 0x400) / 8];
/* Terrain seam closure runs after a complete RPU pass.  Keep sprite ownership
 * independently from the optional high-resolution mask so the gap repair can
 * never mistake a short opaque HUD/effect run for untouched background. */
static uint8_t rpu_sprite_coverage_mask[(0x800 * 0x400) / 8];

static void rpu_mark_sprite_pixel(uint32_t x, uint32_t y)
{
	uint32_t pixel;
	if (x >= 0x800 || y >= 0x400)
		return;
	pixel = y * 0x800 + x;
	rpu_sprite_coverage_mask[pixel >> 3] |=
		(uint8_t)(1U << (pixel & 7));
}

static int rpu_pixel_is_sprite(uint32_t x, uint32_t y)
{
	uint32_t pixel;
	if (x >= 0x800 || y >= 0x400)
		return 0;
	pixel = y * 0x800 + x;
	return (rpu_sprite_coverage_mask[pixel >> 3] >> (pixel & 7)) & 1;
}

/* GPU0 and GPU1 are two halves of one RPU submission.  E408 remains
 * observable to the guest before E414 arrives, so the legacy first half is
 * still drawn immediately.  Keep the surface which existed before that
 * first half outside the saved machine state; E414 can then restore exactly
 * that base and rebuild only the current Polygon/Sprite pair scanline by
 * scanline without erasing submissions that were completed earlier in the
 * same frame. */
typedef struct rpu_surface_snapshot
{
	const xavix2_machine_t *owner;
	uint64_t frame_count;
	uint16_t x;
	uint16_t y;
	uint16_t width;
	uint16_t height;
	int valid;
	int consumed;
} rpu_surface_snapshot_t;

static rpu_surface_snapshot_t rpu_surface_snapshot;
static uint32_t rpu_surface_snapshot_pixels[0x800 * 0x400];
static uint8_t rpu_surface_snapshot_polygon_mask[(0x800 * 0x400) / 8];
enum
{
	CPU_FLAG_INTERRUPT_ENABLE = 0x10,
	IRQ_TIMER = 7,
	IRQ_MOTION = 10,
	IRQ_DMA = 12,
	AUDIO_DESCRIPTOR_RAM_OFFSET = 0xf800,
	GPU_STATUS_REGISTER = 0x60a,
	GPU_STATUS_VIDEO_MODE = 0x0040,
	GPU_STATUS_TRIANGLE_READY = 0x0100,
	GPU_STATUS_SPRITE_READY = 0x0200,
	PROJECTOR_COMMAND_REGISTER = 0x858,
	PROJECTOR_FOCAL_REGISTER = 0x840,
	PROJECTOR_NEAR_REGISTER = 0x844,
	PROJECTOR_FAR_REGISTER = 0x846,
	PROJECTOR_VIEWPORT_X_REGISTER = 0x848,
	PROJECTOR_VIEWPORT_Y_REGISTER = 0x84a,
	PROJECTOR_AMBIENT_REGISTER = 0x84c,
	PROJECTOR_LIGHT_X_REGISTER = 0x850,
	PROJECTOR_LIGHT_Y_REGISTER = 0x852,
	PROJECTOR_LIGHT_Z_REGISTER = 0x854,
	PROJECTOR_DEPTH_BIAS_REGISTER = 0x856,
	PROJECTOR_COUNT_REGISTER = 0x85a,
	PROJECTOR_OUTPUT_COUNT_REGISTER = 0x85c,
	PROJECTOR_SOURCE_REGISTER = 0x860,
	PROJECTOR_DESTINATION_REGISTER = 0x862,
	PROJECTOR_POLYGON_REGISTER = 0x864,
	CONTROLLER_POWER_STATUS_REGISTER = 0xc48,
	CONTROLLER_POWER_STATUS_COUNTER_MASK = 0x03,
	CONTROLLER_POWER_STATUS_GOOD = 0x04
};

enum
{
	XAVIX2_STATE_HEADER_SIZE = 16,
	XAVIX2_STATE_VERSION = 2,
	XAVIX2_STATE_LEGACY_VERSION = 1
};

static const uint8_t XAVIX2_STATE_MAGIC[8] =
{
	'X', 'A', 'V', 'I', 'X', '2', 'S', 'T'
};

static uint8_t machine_read8(void *opaque, uint32_t address);
static uint32_t pio_read(const xavix2_machine_t *machine);

static uint8_t machine_fetch8(void *opaque, uint32_t address)
{
	xavix2_machine_t *machine = (xavix2_machine_t *)opaque;
	if (address < XAVIX2_PROGRAM_RAM_SIZE)
		return machine->program_ram[address];
	return machine_read8(opaque, address);
}

static uint16_t load16(const uint8_t *data)
{
	return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t load32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
		((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void store32(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)(value >> 16);
	data[3] = (uint8_t)(value >> 24);
}

static void store16(uint8_t *data, uint16_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
}


static void geometry_matrix_multiply(xavix2_machine_t *machine,
	uint16_t source, uint16_t destination)
{
	int32_t left[12];
	int32_t right[12];
	int32_t result[12];
	unsigned row;
	unsigned column;
	unsigned inner;

	if ((uint32_t)source + sizeof(right) > XAVIX2_LOW_RAM_SIZE ||
		(uint32_t)destination + sizeof(result) > XAVIX2_LOW_RAM_SIZE)
		return;
	for (row = 0; row < 12; ++row)
	{
		left[row] = (int32_t)load32(machine->mmio + 0x800 + row * 4);
		right[row] = (int32_t)load32(machine->low_ram + source + row * 4);
	}
	for (row = 0; row < 3; ++row)
	{
		for (column = 0; column < 4; ++column)
		{
			/* MatrixMulti computes source * registers.  Keeping the source
			 * translation on the left prevents an object scale from scaling its
			 * world position. */
			int64_t sum = column == 3 ?
				(int64_t)right[row * 4 + 3] << 16 : 0;
			for (inner = 0; inner < 3; ++inner)
				sum += (int64_t)right[row * 4 + inner] *
					left[inner * 4 + column];
			sum >>= 16;
			if (sum > INT32_MAX)
				result[row * 4 + column] = INT32_MAX;
			else if (sum < INT32_MIN)
				result[row * 4 + column] = INT32_MIN;
			else
				result[row * 4 + column] = (int32_t)sum;
		}
	}
	for (row = 0; row < 12; ++row)
		store32(machine->low_ram + destination + row * 4,
			(uint32_t)result[row]);
}

static int32_t geometry_signed10(uint32_t value)
{
	return (int32_t)(value << 22) >> 22;
}

static int32_t geometry_vector10_component(uint32_t packed, unsigned shift)
{
	return geometry_signed10(packed >> shift);
}

static int32_t geometry_saturate32(int64_t value)
{
	if (value > INT32_MAX)
		return INT32_MAX;
	if (value < INT32_MIN)
		return INT32_MIN;
	return (int32_t)value;
}

static int16_t geometry_saturate16(int64_t value)
{
	if (value > INT16_MAX)
		return INT16_MAX;
	if (value < INT16_MIN)
		return INT16_MIN;
	return (int16_t)value;
}

static int64_t geometry_arithmetic_shift_right(int64_t value, unsigned bits)
{
	uint64_t magnitude;
	uint64_t rounded;

	if (!bits)
		return value;
	if (value >= 0)
		return value >> bits;
	magnitude = (uint64_t)(-(value + 1)) + 1;
	rounded = (magnitude + ((UINT64_C(1) << bits) - 1)) >> bits;
	return -(int64_t)rounded;
}

static int64_t geometry_scale_product(int32_t coefficient, int32_t coordinate,
	unsigned scale)
{
	int64_t product = (int64_t)coefficient * coordinate;
	/* Affine basis elements are signed Q7.8 and Vector10 components are
	 * signed Q0.9.  Their product has 17 fractional bits, so Scale=1 is
	 * already Q15.16; Scale=0 shifts right once and Scale=2/3 shifts left. */
	if (!scale)
		return geometry_arithmetic_shift_right(product, 1);
	return product * (INT64_C(1) << (scale - 1));
}

static int32_t geometry_affine_coefficient(uint32_t packed)
{
	/* Affine basis elements are signed 16-bit fixed-point values.  Firmware
	 * writes them with halfword stores, so any upper 16 bits left by a prior
	 * MatrixMulti command are unrelated and must be ignored. */
	return (int32_t)(int16_t)(packed & UINT32_C(0x0000ffff));
}

static void geometry_affine_vertex(const xavix2_machine_t *machine,
	uint32_t packed, int32_t result[3])
{
	int32_t coordinate[3] =
	{
		geometry_vector10_component(packed, 22),
		geometry_vector10_component(packed, 12),
		geometry_vector10_component(packed, 2)
	};
	unsigned scale = packed & 3;
	unsigned row;
	unsigned column;

	for (row = 0; row < 3; ++row)
	{
		int64_t sum = (int32_t)load32(
			machine->mmio + 0x80c + row * 0x10);
		for (column = 0; column < 3; ++column)
		{
			int32_t coefficient = geometry_affine_coefficient(load32(machine->mmio + 0x800 +
				row * 0x10 + column * 4));
			sum += geometry_scale_product(coefficient,
				coordinate[column], scale);
		}
		result[row] = geometry_saturate32(sum);
	}
}

static void geometry_transform_vectors16(xavix2_machine_t *machine,
	uint16_t source, uint16_t destination, uint32_t count)
{
	int32_t rotation[9];
	uint32_t index;
	unsigned row;
	unsigned column;

	if ((uint32_t)source + count * 6 > XAVIX2_LOW_RAM_SIZE ||
		(uint32_t)destination + count * 6 > XAVIX2_LOW_RAM_SIZE)
		return;
	/* Command 07 transforms signed XYZ16 vectors.  DBZ submits two six-byte
	 * vectors at 201C and reads the first six-byte result at 2010 into the
	 * E850/E852/E854 light-vector registers.  Its wrapper loads the same
	 * contiguous Q8.8 3x3 matrix used by command 0F. */
	for (row = 0; row < 3; ++row)
		for (column = 0; column < 3; ++column)
			rotation[row * 3 + column] = (int32_t)load32(
				machine->mmio + 0x800 + (row * 3 + column) * 4);
	for (index = 0; index < count; ++index)
	{
		int32_t coordinate[3];
		for (column = 0; column < 3; ++column)
			coordinate[column] = (int16_t)load16(machine->low_ram + source +
				index * 6 + column * 2);
		for (row = 0; row < 3; ++row)
		{
			int64_t result = 0;
			for (column = 0; column < 3; ++column)
				result += (int64_t)rotation[row * 3 + column] *
					coordinate[column];
			store16(machine->low_ram + destination + index * 6 + row * 2,
				(uint16_t)(int16_t)(result >> 8));
		}
	}
}

static void geometry_transform_normal(const xavix2_machine_t *machine,
	uint32_t packed, int16_t transformed[3])
{
	unsigned row;
	unsigned column;
	int32_t coordinate[3] =
	{
		geometry_vector10_component(packed, 22),
		geometry_vector10_component(packed, 12),
		geometry_vector10_component(packed, 2)
	};

	/* Vector10 normals are signed Q0.9 in bits X=31:22, Y=21:12 and
	 * Z=11:2. Trans ignores Scale. DBZ loads a signed Q7.8 matrix into the
	 * low halves of GR0..GR8; Q0.9*Q7.8 is shifted to the Q0.15 form consumed
	 * by Dot. */
	for (row = 0; row < 3; ++row)
	{
		int64_t result = 0;
		for (column = 0; column < 3; ++column)
			result += (int64_t)(int16_t)load16(machine->mmio + 0x800 +
				(row * 3 + column) * 4) * coordinate[column];
		transformed[row] = geometry_saturate16(
			geometry_arithmetic_shift_right(result, 2));
	}
}

static uint8_t geometry_dot_normal(const xavix2_machine_t *machine,
	const int16_t normal[3])
{
	int64_t dot = (int64_t)normal[0] *
		(int16_t)load16(machine->mmio + PROJECTOR_LIGHT_X_REGISTER) +
		(int64_t)normal[1] *
		(int16_t)load16(machine->mmio + PROJECTOR_LIGHT_Y_REGISTER) +
		(int64_t)normal[2] *
		(int16_t)load16(machine->mmio + PROJECTOR_LIGHT_Z_REGISTER);
	/* Dot's SUM is the negated L dot N value. Its byte is
	 * {SUM sign, transformed-Nz sign, 0, abs(SUM)[29:25]}. */
	int64_t sum = -dot;
	uint64_t magnitude = sum < 0 ? (uint64_t)(-sum) : (uint64_t)sum;
	return (uint8_t)((sum < 0 ? 0x80 : 0) |
		(normal[2] < 0 ? 0x40 : 0) | ((magnitude >> 25) & 0x1f));
}

static uint32_t geometry_brightness(const xavix2_machine_t *machine,
	uint8_t dot, int double_sided)
{
	uint32_t s1 = dot >> 7;
	uint32_t s2b = (dot >> 6) & 1;
	uint32_t zero_diffuse = double_sided ? (s1 ^ s2b) : !s1;
	uint32_t diffuse = zero_diffuse ? 0 : (dot & 0x1f);
	uint32_t brightness = diffuse +
		(machine->mmio[PROJECTOR_AMBIENT_REGISTER] & 0x1f) + 1;
	return brightness > 31 ? 31 : brightness;
}

static uint16_t geometry_light_rgb555(uint16_t color, uint32_t brightness)
{
	uint32_t red = ((color & 31) * (brightness + 1)) >> 5;
	uint32_t green = (((color >> 5) & 31) * (brightness + 1)) >> 5;
	uint32_t blue = (((color >> 10) & 31) * (brightness + 1)) >> 5;
	return (uint16_t)(red | (green << 5) | (blue << 10));
}

static void geometry_transform_and_dot_normals(xavix2_machine_t *machine,
	uint16_t source, uint16_t destination, uint32_t count)
{
	uint32_t index;
	if ((uint32_t)source + count * 4 > XAVIX2_LOW_RAM_SIZE ||
		(uint32_t)destination + count > XAVIX2_LOW_RAM_SIZE)
		return;
	/* Command 0F is TD, not a raw transformed-vector output. Trans is kept
	 * inside the GE and exactly one Dot byte per input normal reaches RAM. */
	for (index = 0; index < count; ++index)
	{
		int16_t transformed[3];
		geometry_transform_normal(machine,
			load32(machine->low_ram + source + index * 4), transformed);
		machine->low_ram[destination + index] =
			geometry_dot_normal(machine, transformed);
	}
}

static void geometry_calculate_vertex_colors(xavix2_machine_t *machine,
	uint16_t source, uint16_t polygon, uint32_t count)
{
	uint32_t index;
	if ((uint32_t)polygon + count * 16 > XAVIX2_LOW_RAM_SIZE)
		return;
	for (index = 0; index < count; ++index)
	{
		uint8_t *record = machine->low_ram + polygon + index * 16;
		uint32_t d0 = load32(record);
		uint32_t d1 = load32(record + 4);
		uint32_t d2 = load32(record + 8);
		uint32_t d3 = load32(record + 12);
		uint16_t vertex[3] = { (uint16_t)(d0 >> 16),
			(uint16_t)d1, (uint16_t)(d1 >> 16) };
		uint16_t color[3] = { (uint16_t)(d2 & 0x7fff),
			(uint16_t)((d2 >> 16) & 0x7fff), (uint16_t)(d3 & 0x7fff) };
		unsigned corner;
		if (!(d0 & 1))
			continue;
		for (corner = 0; corner < 3; ++corner)
		{
			uint32_t dot_address = (uint32_t)source + vertex[corner];
			if (dot_address >= XAVIX2_LOW_RAM_SIZE)
				continue;
			color[corner] = geometry_light_rgb555(color[corner],
				geometry_brightness(machine, machine->low_ram[dot_address],
					(d0 >> 1) & 1));
		}
		d2 = (d2 & UINT32_C(0x80008000)) | color[0] |
			((uint32_t)color[1] << 16);
		d3 = (d3 & ~UINT32_C(0x00007fff)) | color[2];
		store32(record + 8, d2);
		store32(record + 12, d3);
	}
}

static void geometry_calculate_polygon_lights(xavix2_machine_t *machine,
	uint16_t source, uint16_t polygon, uint32_t count)
{
	uint32_t index;
	if ((uint32_t)source + count * 4 > XAVIX2_LOW_RAM_SIZE ||
		(uint32_t)polygon + count * 16 > XAVIX2_LOW_RAM_SIZE)
		return;
	for (index = 0; index < count; ++index)
	{
		uint8_t *record = machine->low_ram + polygon + index * 16;
		uint32_t d0 = load32(record);
		uint32_t d2;
		int16_t transformed[3];
		uint32_t brightness;
		if (d0 & 1)
			continue;
		geometry_transform_normal(machine,
			load32(machine->low_ram + source + index * 4), transformed);
		brightness = geometry_brightness(machine,
			geometry_dot_normal(machine, transformed), (d0 >> 1) & 1);
		d2 = load32(record + 8);
		d2 = (d2 & ~UINT32_C(0x00007c00)) | (brightness << 10);
		store32(record + 8, d2);
	}
}

static void geometry_transform_vertices(xavix2_machine_t *machine,
	uint16_t source, uint16_t destination, uint32_t count)
{
	int32_t rotation[9];
	int32_t translation[3];
	uint32_t index;
	unsigned row;
	unsigned column;

	if ((uint32_t)source + count * 4 > XAVIX2_LOW_RAM_SIZE ||
		(uint32_t)destination + count * 12 > XAVIX2_LOW_RAM_SIZE)
		return;
	/* Command 0C consumes nine signed 32-bit coefficient registers at E800,
	 * E804, E808, ... and translations at E80C/E81C/E82C.  Its wrapper updates
	 * only each coefficient's low word after command 10 has populated the full
	 * register, intentionally preserving the sign/upper half. */
	for (row = 0; row < 3; ++row)
	{
		for (column = 0; column < 3; ++column)
			rotation[row * 3 + column] = (int32_t)load32(
				machine->mmio + 0x800 + row * 0x10 + column * 4);
		translation[row] = (int32_t)load32(
			machine->mmio + 0x80c + row * 0x10);
	}
	for (index = 0; index < count; ++index)
	{
		uint32_t packed = load32(machine->low_ram + source + index * 4);
		int32_t coordinate[3];
		coordinate[0] = geometry_signed10(packed);
		coordinate[1] = geometry_signed10(packed >> 10);
		coordinate[2] = geometry_signed10(packed >> 20);
		for (row = 0; row < 3; ++row)
		{
			int64_t result = translation[row];
			for (column = 0; column < 3; ++column)
				result += (int64_t)rotation[row * 3 + column] *
					coordinate[column];
			store32(machine->low_ram + destination + index * 12 + row * 4,
				(uint32_t)result);
		}
	}
}

static void geometry_transform_points(xavix2_machine_t *machine,
	uint16_t source, uint16_t destination, uint32_t count)
{
	int32_t rotation[9];
	int32_t translation[3];
	uint32_t index;
	unsigned row;
	unsigned column;

	if ((uint32_t)source + count * 4 > XAVIX2_LOW_RAM_SIZE ||
		(uint32_t)destination + count * 12 > XAVIX2_LOW_RAM_SIZE)
		return;
	/* Command 01 consumes the signed low words written by the common matrix
	 * loader and returns Q16.16 coordinates.  DB2J uses their length as the
	 * large-object distance: a translation (3,0,4) must become
	 * (0x30000,0,0x40000), whose length is 0x50000.  Treating this as command
	 * 0C returned length 5 and forced every enemy sprite to maximum scale. */
	for (row = 0; row < 3; ++row)
	{
		for (column = 0; column < 3; ++column)
			rotation[row * 3 + column] = (int16_t)load16(
				machine->mmio + 0x800 + row * 0x10 + column * 4);
		translation[row] = (int32_t)load32(
			machine->mmio + 0x80c + row * 0x10);
	}
	for (index = 0; index < count; ++index)
	{
		uint32_t packed = load32(machine->low_ram + source + index * 4);
		int32_t coordinate[3];
		coordinate[0] = geometry_signed10(packed);
		coordinate[1] = geometry_signed10(packed >> 10);
		coordinate[2] = geometry_signed10(packed >> 20);
		for (row = 0; row < 3; ++row)
		{
			int64_t result = (int64_t)translation[row] << 16;
			for (column = 0; column < 3; ++column)
				result += (int64_t)rotation[row * 3 + column] *
					coordinate[column] * UINT32_C(0x10000);
			store32(machine->low_ram + destination + index * 12 + row * 4,
				(uint32_t)(int32_t)result);
		}
	}
}

static void geometry_project_points(xavix2_machine_t *machine,
	uint16_t source, uint16_t polygon_address, uint32_t count)
{
	int32_t focal = (int16_t)load16(machine->mmio + PROJECTOR_FOCAL_REGISTER);
	int32_t center_x2 = (int32_t)load16(machine->mmio + 0x848);
	int32_t center_y2 = (int32_t)load16(machine->mmio + 0x84a);
	uint32_t polygon_index;

	if ((uint32_t)polygon_address + count * 16 > XAVIX2_LOW_RAM_SIZE)
		return;
	/* Command 0D projects the indexed integer points produced by command 0C.
	 * Unlike command 4D it does not replace the polygon record: firmware uses
	 * the projected coordinate pair in each source point to position a large
	 * tiled object, and consumes the polygon depth field for its scale. */
	for (polygon_index = 0; polygon_index < count; ++polygon_index)
	{
		uint8_t *polygon = machine->low_ram + polygon_address +
			polygon_index * 16;
		uint32_t d0 = load32(polygon);
		uint32_t d1 = load32(polygon + 4);
		uint32_t vertex_index[3] = {
			d0 >> 16,
			d1 & 0xffff,
			d1 >> 16
		};
		int64_t depth_sum = 0;
		unsigned vertex;
		int valid = 1;

		for (vertex = 0; vertex < 3; ++vertex)
		{
			uint32_t address = (uint32_t)source + vertex_index[vertex] * 12;
			int32_t x;
			int32_t y;
			int32_t z;
			int64_t projected_x2;
			int64_t projected_y2;
			if (address + 12 > XAVIX2_LOW_RAM_SIZE)
			{
				valid = 0;
				break;
			}
			x = (int32_t)load32(machine->low_ram + address);
			y = (int32_t)load32(machine->low_ram + address + 4);
			z = (int32_t)load32(machine->low_ram + address + 8);
			if (z <= 0)
			{
				valid = 0;
				break;
			}
			/* Command 0D retains one wrap page above each packed GPU axis.
			 * X is 11-bit (12-bit when doubled), Y is 10-bit (11-bit when
			 * doubled).  DB2J averages these unwrapped values for large-object
			 * culling, then masks them down when it emits the sprite tiles. */
			projected_x2 = 0x1000 + center_x2 +
				(int64_t)x * focal * 2 / z;
			projected_y2 = 0x0800 + center_y2 +
				(int64_t)y * focal * 2 / z;
			store32(machine->low_ram + address + 4,
				((uint32_t)projected_x2 & 0xffff) << 16 |
				((uint32_t)projected_y2 & 0xffff));
			depth_sum += z;
		}
		if (valid)
		{
			uint32_t d3 = load32(polygon + 12);
			/* The indexed-list depth field is Q8 relative to the integer model
			 * coordinate.  DB2J's (3,0,4) billboard anchor therefore becomes
			 * depth 0x400, matching the scale selected by its firmware. */
			uint32_t depth = (uint32_t)(depth_sum / 3) << 8;
			d3 = (d3 & ~UINT32_C(0x07fc0000)) |
				((depth << 15) & UINT32_C(0x07fc0000));
			store32(polygon + 12, d3);
		}
	}
}

typedef struct geometry_clip_vertex
{
	double x;
	double y;
	double z;
	double color[3];
} geometry_clip_vertex;

static geometry_clip_vertex geometry_clip_lerp(
	const geometry_clip_vertex *from, const geometry_clip_vertex *to,
	double amount)
{
	geometry_clip_vertex result;
	unsigned component;
	result.x = from->x + (to->x - from->x) * amount;
	result.y = from->y + (to->y - from->y) * amount;
	result.z = from->z + (to->z - from->z) * amount;
	for (component = 0; component < 3; ++component)
		result.color[component] = from->color[component] +
			(to->color[component] - from->color[component]) * amount;
	return result;
}

static unsigned geometry_clip_plane(const geometry_clip_vertex *input,
	unsigned input_count, geometry_clip_vertex *output, unsigned axis,
	double limit, int keep_greater)
{
	unsigned output_count = 0;
	unsigned index;
	geometry_clip_vertex previous;
	double previous_coordinate;
	int previous_inside;
	if (!input_count)
		return 0;
	previous = input[input_count - 1];
	previous_coordinate = axis == 0 ? previous.x :
		axis == 1 ? previous.y : previous.z;
	previous_inside = keep_greater ? previous_coordinate >= limit :
		previous_coordinate <= limit;
	for (index = 0; index < input_count; ++index)
	{
		geometry_clip_vertex current = input[index];
		double current_coordinate = axis == 0 ? current.x :
			axis == 1 ? current.y : current.z;
		int current_inside = keep_greater ? current_coordinate >= limit :
			current_coordinate <= limit;
		if (current_inside != previous_inside)
		{
			double denominator = current_coordinate - previous_coordinate;
			double amount = denominator != 0.0 ?
				(limit - previous_coordinate) / denominator : 0.0;
			geometry_clip_vertex intersection = geometry_clip_lerp(
				&previous, &current, amount);
			if (axis == 0) intersection.x = limit;
			else if (axis == 1) intersection.y = limit;
			else intersection.z = limit;
			if (output_count < 8) output[output_count++] = intersection;
		}
		if (current_inside && output_count < 8)
			output[output_count++] = current;
		previous = current;
		previous_coordinate = current_coordinate;
		previous_inside = current_inside;
	}
	return output_count;
}

static uint16_t geometry_clip_color(const geometry_clip_vertex *vertex)
{
	uint16_t result = 0;
	unsigned component;
	for (component = 0; component < 3; ++component)
	{
		int value = (int)(vertex->color[component] + 0.5);
		if (value < 0) value = 0;
		if (value > 31) value = 31;
		result |= (uint16_t)value << (component * 5);
	}
	return result;
}

static uint32_t geometry_emit_clipped_gouraud(xavix2_machine_t *machine,
	uint16_t source, uint16_t polygon_address, uint32_t output_count,
	uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3, int32_t focal,
	int32_t center_x, int32_t center_y)
{
	geometry_clip_vertex first[8];
	geometry_clip_vertex second[8];
	geometry_clip_vertex *input = first;
	geometry_clip_vertex *output = second;
	uint32_t vertex_index[3] = { d0 >> 16, d1 & 0xffff, d1 >> 16 };
	uint16_t raw_color[3] = {
		(uint16_t)(d2 & 0x7fff),
		(uint16_t)((d2 >> 16) & 0x7fff),
		(uint16_t)(d3 & 0x7fff)
	};
	unsigned count = 3;
	unsigned vertex;
	unsigned triangle;
	int32_t crop_x = (int32_t)load16(machine->mmio + 0x656);
	int32_t crop_y = (int32_t)load16(machine->mmio + 0x658);
	for (vertex = 0; vertex < 3; ++vertex)
	{
		uint32_t address = (uint32_t)source + vertex_index[vertex] * 12;
		unsigned component;
		if (address + 12 > XAVIX2_LOW_RAM_SIZE)
			return output_count;
		input[vertex].x = (int32_t)load32(machine->low_ram + address);
		input[vertex].y = (int32_t)load32(machine->low_ram + address + 4);
		input[vertex].z = (int32_t)load32(machine->low_ram + address + 8);
		for (component = 0; component < 3; ++component)
			input[vertex].color[component] =
				(raw_color[vertex] >> (component * 5)) & 31;
	}
	/* With E846=7fff the explicit near plane is disabled, but projection still
	 * clips geometry crossing the eye plane.  One Q16.16 unit avoids an
	 * unstable divide while remaining negligible beside the traced terrain. */
	count = geometry_clip_plane(input, count, output, 2, 65536.0, 1);
	if (count < 3)
		return output_count;
	for (vertex = 0; vertex < count; ++vertex)
	{
		output[vertex].x = center_x + output[vertex].x * focal / output[vertex].z;
		output[vertex].y = center_y + output[vertex].y * focal / output[vertex].z;
		output[vertex].z = 1.0;
	}
	input = output;
	output = input == first ? second : first;
	count = geometry_clip_plane(input, count, output, 0, crop_x, 1);
	input = output; output = input == first ? second : first;
	count = geometry_clip_plane(input, count, output, 0, crop_x + 319, 0);
	input = output; output = input == first ? second : first;
	count = geometry_clip_plane(input, count, output, 1, crop_y, 1);
	input = output; output = input == first ? second : first;
	count = geometry_clip_plane(input, count, output, 1, crop_y + 239, 0);
	input = output;
	for (triangle = 1; triangle + 1 < count; ++triangle)
	{
		const geometry_clip_vertex *corner[3] = {
			input, input + triangle, input + triangle + 1
		};
		uint32_t x[3];
		uint32_t y[3];
		uint8_t *record;
		for (vertex = 0; vertex < 3; ++vertex)
		{
			x[vertex] = (uint32_t)(corner[vertex]->x + 0.5);
			y[vertex] = (uint32_t)(corner[vertex]->y + 0.5);
		}
		if (x[0] == x[1] && x[1] == x[2]) continue;
		if (y[0] == y[1] && y[1] == y[2]) continue;
		if ((uint32_t)polygon_address + (output_count + 1) * 16 >
			XAVIX2_LOW_RAM_SIZE)
			break;
		record = machine->low_ram + polygon_address + output_count * 16;
		store32(record, 1 | (y[0] << 1) | (x[0] << 11) | (y[1] << 22));
		store32(record + 4, x[1] | (y[2] << 11) | (x[2] << 21));
		store32(record + 8, (d2 & UINT32_C(0x80008000)) |
			geometry_clip_color(corner[0]) |
			((uint32_t)geometry_clip_color(corner[1]) << 16));
		store32(record + 12, (d3 & ~UINT32_C(0x00007fff)) |
			geometry_clip_color(corner[2]));
		output_count++;
	}
	return output_count;
}

static void geometry_project_triangles(xavix2_machine_t *machine,
	uint16_t source, uint16_t polygon_address, uint32_t count)
{
	int32_t focal = (int16_t)load16(machine->mmio + PROJECTOR_FOCAL_REGISTER);
	int16_t near_setting = (int16_t)load16(machine->mmio +
		PROJECTOR_NEAR_REGISTER);
	/* EPOCH uses 0x7fff as a disabled-near-clip sentinel and expects clipped
	 * faces to be emitted from a stable copy of the input list. */
	int epoch_clipped_projection = near_setting == INT16_MAX;
	int32_t near_depth = epoch_clipped_projection ? 0 :
		(int32_t)near_setting * INT32_C(65536);
	int32_t center_x = (int32_t)load16(machine->mmio + 0x848) / 2;
	int32_t center_y = (int32_t)load16(machine->mmio + 0x84a) / 2;
	uint8_t *input_copy = NULL;
	uint32_t input_index;
	uint32_t output_count = 0;

	store16(machine->mmio + PROJECTOR_OUTPUT_COUNT_REGISTER, 0);
	if ((uint32_t)polygon_address + count * 16 > XAVIX2_LOW_RAM_SIZE)
		return;
	if (epoch_clipped_projection)
	{
		input_copy = (uint8_t *)malloc((size_t)count * 16);
		if (!input_copy) return;
		memcpy(input_copy, machine->low_ram + polygon_address, (size_t)count * 16);
	}
	for (input_index = 0; input_index < count; ++input_index)
	{
		const uint8_t *input = (input_copy ? input_copy :
			machine->low_ram + polygon_address) + input_index * 16;
		uint32_t d0 = load32(input);
		uint32_t d1 = load32(input + 4);
		uint32_t d2 = load32(input + 8);
		uint32_t d3 = load32(input + 12);
		if (input_copy && (d0 & 1))
		{
			output_count = geometry_emit_clipped_gouraud(machine, source,
				polygon_address, output_count, d0, d1, d2, d3, focal,
				center_x, center_y);
			continue;
		}
		uint32_t vertex_index[3] = {
			d0 >> 16,
			d1 & 0xffff,
			d1 >> 16
		};
		int32_t projected_x[3];
		int32_t projected_y[3];
		int32_t vertex_depth[3];
		unsigned vertex;
		int valid = 1;
		int64_t area;
		for (vertex = 0; vertex < 3; ++vertex)
		{
			uint32_t vertex_address = (uint32_t)source + vertex_index[vertex] * 12;
			int32_t x;
			int32_t y;
			int32_t z;
			int64_t screen_x;
			int64_t screen_y;
			if (vertex_address + 12 > XAVIX2_LOW_RAM_SIZE)
			{
				valid = 0;
				break;
			}
			x = (int32_t)load32(machine->low_ram + vertex_address);
			y = (int32_t)load32(machine->low_ram + vertex_address + 4);
			z = (int32_t)load32(machine->low_ram + vertex_address + 8);
			/* E846 is the guest-selected near plane in integer world units.
			 * DB2J/DBZ feed command 4D Q16.16 vertices; accepting a triangle
			 * which crosses this plane turns it into a screen-sized slab.  Cull
			 * that primitive until the hardware's exact clipped-edge output is
			 * characterized. */
			if (z <= 0 || z < near_depth)
			{
				valid = 0;
				break;
			}
			screen_x = center_x + (int64_t)x * focal / z;
			screen_y = center_y + (int64_t)y * focal / z;
			/* Values this far outside the 11x10-bit render target cannot cross
			 * its viewport and would make a host-side area product needlessly
			 * large.  Normal guest geometry is well inside this guard. */
			if (screen_x < -65536 || screen_x > 65535 ||
				screen_y < -65536 || screen_y > 65535)
			{
				valid = 0;
				break;
			}
			projected_x[vertex] = (int32_t)screen_x;
			projected_y[vertex] = (int32_t)screen_y;
			vertex_depth[vertex] = z;
		}
		if (!valid)
			continue;
		/* Packed GPU coordinates are unsigned 11x10-bit values.  Wrapping a
		 * projected point outside that range creates a screen-spanning polygon
		 * on the opposite side (DB2J's large brown battle slabs).  Hardware
		 * clips against the selected viewport before packing; conservatively
		 * discard that primitive until clipped-edge generation is modeled. */
		for (vertex = 0; vertex < 3; ++vertex)
			if (projected_x[vertex] < 0 || projected_x[vertex] > 0x7ff ||
				projected_y[vertex] < 0 || projected_y[vertex] > 0x3ff)
			{
				valid = 0;
				break;
			}
		if (!valid)
			continue;
		area = (int64_t)(projected_x[1] - projected_x[0]) *
			(projected_y[2] - projected_y[0]) -
			(int64_t)(projected_y[1] - projected_y[0]) *
			(projected_x[2] - projected_x[0]);
		/* Screen Y grows downward, so positive screen-space winding is the
		 * mathematical front face used by the firmware's indexed records. */
		/* Take-copter disables the near plane and submits both windings for its
		 * terrain mesh; zero-area records remain non-renderable. */
		if (!area || (near_setting != INT16_MAX && area < 0))
			continue;
		{
			int32_t min_x = projected_x[0];
			int32_t max_x = projected_x[0];
			int32_t min_y = projected_y[0];
			int32_t max_y = projected_y[0];
			int32_t crop_x = (int32_t)load16(machine->mmio + 0x656);
			int32_t crop_y = (int32_t)load16(machine->mmio + 0x658);

			for (vertex = 1; vertex < 3; ++vertex)
			{
				if (projected_x[vertex] < min_x) min_x = projected_x[vertex];
				if (projected_x[vertex] > max_x) max_x = projected_x[vertex];
				if (projected_y[vertex] < min_y) min_y = projected_y[vertex];
				if (projected_y[vertex] > max_y) max_y = projected_y[vertex];
			}
			/* The polygon unit rejects primitives wholly outside the selected
			 * 320x240 viewport before packing them into the shared GPU list.
			 * DB2J reserves each following model's list slot on that basis; keeping
			 * off-screen faces here lets one model overwrite the next one. */
			if (max_x < crop_x || min_x >= crop_x + 320 ||
				max_y < crop_y || min_y >= crop_y + 240)
				continue;
		}
		{
			int32_t packed_x[3] = {
				projected_x[0] & 0x7ff,
				projected_x[1] & 0x7ff,
				projected_x[2] & 0x7ff
			};
			int32_t packed_y[3] = {
				projected_y[0] & 0x3ff,
				projected_y[1] & 0x3ff,
				projected_y[2] & 0x3ff
			};
			int64_t packed_area =
				(int64_t)(packed_x[1] - packed_x[0]) *
					(packed_y[2] - packed_y[0]) -
				(int64_t)(packed_y[1] - packed_y[0]) *
					(packed_x[2] - packed_x[0]);
			uint8_t *output = machine->low_ram + polygon_address + output_count * 16;

			/* Full hardware clips the primitive to its selected viewport before
			 * packing 11x10-bit screen coordinates.  Until that edge generator is
			 * modeled, reject a triangle whose wrapped coordinates reverse its
			 * front-face winding.  Keeping it produces a screen-sized polygon (the
			 * DBZ battle's black slab) rather than a clipped edge. */
			if (!packed_area || ((packed_area < 0) != (area < 0)))
				continue;
			if (!(d0 & 1))
			{
				uint64_t weight_b = ((uint64_t)(uint32_t)vertex_depth[0] << 6) /
					(uint32_t)vertex_depth[1];
				uint64_t weight_c = ((uint64_t)(uint32_t)vertex_depth[0] << 6) /
					(uint32_t)vertex_depth[2];

				/* The texture record stores Aw as the implicit Q2.6 value 64,
				 * then Bw=Az/Bz and Cw=Az/Cz.  Command 4D has the transformed
				 * view-space depths, so replace only those perspective fields
				 * while preserving the guest's texture, light, and layer state. */
				if (weight_b > UINT8_MAX) weight_b = UINT8_MAX;
				if (weight_c > UINT8_MAX) weight_c = UINT8_MAX;
				d2 = (d2 & ~UINT32_C(0x000000ff)) | (uint32_t)weight_b;
				d3 = (d3 & ~UINT32_C(0x00007f80)) |
					((uint32_t)weight_c << 7);
			}
			uint32_t packed0 = (d0 & 1) |
				((uint32_t)packed_y[0] << 1) |
				((uint32_t)packed_x[0] << 11) |
				((uint32_t)packed_y[1] << 22);
			uint32_t packed1 = (uint32_t)packed_x[1] |
				((uint32_t)packed_y[2] << 11) |
				((uint32_t)packed_x[2] << 21);
			store32(output, packed0);
			store32(output + 4, packed1);
			store32(output + 8, d2);
			store32(output + 12, d3);
			output_count++;
		}
	}
	free(input_copy);
	store16(machine->mmio + PROJECTOR_OUTPUT_COUNT_REGISTER,
		(uint16_t)output_count);
}

static void geometry_store_vector16(uint8_t *output, int32_t depth,
	int32_t horizontal, int32_t vertical, int clipping)
{
	store32(output, ((uint32_t)depth & UINT32_C(0x7fffffff)) |
		(clipping ? UINT32_C(0x80000000) : 0));
	store16(output + 4, (uint16_t)geometry_saturate16(vertical));
	store16(output + 6, (uint16_t)geometry_saturate16(horizontal));
}

static void geometry_project_vector(const xavix2_machine_t *machine,
	const int32_t input[3], int viewport, uint8_t *output)
{
	uint32_t distance = load16(machine->mmio + PROJECTOR_FOCAL_REGISTER);
	uint32_t near_clip = load16(machine->mmio + PROJECTOR_NEAR_REGISTER);
	uint32_t far_clip = load16(machine->mmio + PROJECTOR_FAR_REGISTER);
	int32_t horizontal = 0;
	int32_t vertical = 0;
	int clipping = input[2] <= (int64_t)near_clip * INT32_C(65536) ||
		input[2] >= (int64_t)far_clip * INT32_C(65536);

	if (input[2] > 0)
	{
		/* Vector16 X/Y are signed Q15.1 in the GE patent.  Both stand-alone
		 * Pproj and composite APV therefore retain one fractional bit before
		 * View adds its Q15.1 viewport offset. */
		int32_t scale = 2;
		horizontal = geometry_saturate16(
			(int64_t)input[0] * distance * scale / input[2]);
		vertical = geometry_saturate16(
			(int64_t)input[1] * distance * scale / input[2]);
	}
	else
		clipping = 1;
	if (viewport)
	{
		horizontal += load16(machine->mmio + PROJECTOR_VIEWPORT_X_REGISTER);
		vertical += load16(machine->mmio + PROJECTOR_VIEWPORT_Y_REGISTER);
		if (horizontal < 0 || horizontal > 2047 * 2 ||
			vertical < 0 || vertical > 1023 * 2)
			clipping = 1;
	}
	geometry_store_vector16(output, input[2], horizontal, vertical, clipping);
}

static void geometry_affine(xavix2_machine_t *machine, uint16_t source,
	uint16_t destination, uint32_t count)
{
	uint32_t index;

	if ((uint32_t)source + count * 4 > XAVIX2_LOW_RAM_SIZE ||
		(uint32_t)destination + count * 12 > XAVIX2_LOW_RAM_SIZE)
		return;
	for (index = 0; index < count; ++index)
	{
		int32_t transformed[3];
		geometry_affine_vertex(machine,
			load32(machine->low_ram + source + index * 4), transformed);
		/* Vector32 is laid out as Z, Y, X in little-endian RAM. */
		store32(machine->low_ram + destination + index * 12,
			(uint32_t)transformed[2]);
		store32(machine->low_ram + destination + index * 12 + 4,
			(uint32_t)transformed[1]);
		store32(machine->low_ram + destination + index * 12 + 8,
			(uint32_t)transformed[0]);
	}
}

static void geometry_perspective_project(xavix2_machine_t *machine,
	uint16_t source, uint16_t destination, uint32_t count)
{
	uint32_t index;

	if ((uint32_t)source + count * 12 > XAVIX2_LOW_RAM_SIZE ||
		(uint32_t)destination + count * 8 > XAVIX2_LOW_RAM_SIZE)
		return;
	for (index = 0; index < count; ++index)
	{
		const uint8_t *input = machine->low_ram + source + index * 12;
		int32_t vector[3] =
		{
			(int32_t)load32(input + 8),
			(int32_t)load32(input + 4),
			(int32_t)load32(input)
		};
		geometry_project_vector(machine, vector, 0,
			machine->low_ram + destination + index * 8);
	}
}

static void geometry_viewport(xavix2_machine_t *machine, uint16_t source,
	uint16_t destination, uint32_t count)
{
	uint32_t index;

	if ((uint32_t)source + count * 8 > XAVIX2_LOW_RAM_SIZE ||
		(uint32_t)destination + count * 8 > XAVIX2_LOW_RAM_SIZE)
		return;
	for (index = 0; index < count; ++index)
	{
		const uint8_t *input = machine->low_ram + source + index * 8;
		uint8_t *output = machine->low_ram + destination + index * 8;
		uint32_t depth = load32(input);
		int32_t vertical = (int16_t)load16(input + 4) +
			load16(machine->mmio + PROJECTOR_VIEWPORT_Y_REGISTER);
		int32_t horizontal = (int16_t)load16(input + 6) +
			load16(machine->mmio + PROJECTOR_VIEWPORT_X_REGISTER);
		int clipping = (depth >> 31) != 0;

		if (horizontal < 0 || horizontal > 2047 * 2 ||
			vertical < 0 || vertical > 1023 * 2)
			clipping = 1;
		geometry_store_vector16(output, (int32_t)(depth & UINT32_C(0x7fffffff)),
			horizontal, vertical, clipping);
	}
}

static void geometry_apv(xavix2_machine_t *machine, uint16_t source,
	uint16_t destination, uint32_t count)
{
	uint32_t index;

	if ((uint32_t)source + count * 4 > XAVIX2_LOW_RAM_SIZE ||
		(uint32_t)destination + count * 8 > XAVIX2_LOW_RAM_SIZE)
		return;
	for (index = 0; index < count; ++index)
	{
		int32_t transformed[3];
		geometry_affine_vertex(machine,
			load32(machine->low_ram + source + index * 4), transformed);
		geometry_project_vector(machine, transformed, 1,
			machine->low_ram + destination + index * 8);
	}
}

static uint16_t geometry_depth_float(uint64_t sum_q16, unsigned bias)
{
	unsigned highest = 0;
	int exponent;
	int encoded_exponent;
	uint64_t base_q16;
	uint64_t mantissa;

	if (!sum_q16)
		return 0;
	{
		uint64_t value = sum_q16;
		while (value >>= 1)
			++highest;
	}
	exponent = (int)highest - 16;
	encoded_exponent = exponent + (int)bias;
	if (encoded_exponent < 0)
		encoded_exponent = 0;
	if (encoded_exponent > 15)
		return UINT16_C(0x0fff);
	exponent = encoded_exponent - (int)bias;
	if (exponent < -16)
		base_q16 = 1;
	else if (exponent > 46)
		return UINT16_C(0x0fff);
	else
		base_q16 = UINT64_C(1) << (exponent + 16);
	if (sum_q16 <= base_q16)
		mantissa = 0;
	else
		mantissa = ((sum_q16 - base_q16) << 8) / base_q16;
	if (mantissa > 0xff)
		mantissa = 0xff;
	return (uint16_t)((encoded_exponent << 8) | mantissa);
}

static int32_t geometry_signed12(uint32_t value)
{
	return (int32_t)(value << 20) >> 20;
}

static void geometry_store_polygon_coordinates(uint8_t *output, uint32_t type,
	const int32_t x[3], const int32_t y[3], uint32_t d2, uint32_t d3)
{
	store32(output, (type & 1) | ((uint32_t)y[0] << 1) |
		((uint32_t)x[0] << 11) | ((uint32_t)y[1] << 22));
	store32(output + 4, (uint32_t)x[1] | ((uint32_t)y[2] << 11) |
		((uint32_t)x[2] << 21));
	store32(output + 8, d2);
	store32(output + 12, d3);
}

static void geometry_cull_and_calculate(xavix2_machine_t *machine,
	uint16_t source, uint16_t polygon_address, uint32_t count,
	int remove_invalid, int calculate_parameters)
{
	uint32_t input_index;
	uint32_t output_count = 0;

	if ((uint32_t)polygon_address + count * 16 > XAVIX2_LOW_RAM_SIZE)
		return;
	for (input_index = 0; input_index < count; ++input_index)
	{
		uint8_t *input = machine->low_ram + polygon_address + input_index * 16;
		uint32_t d0 = load32(input);
		uint32_t d1 = load32(input + 4);
		uint32_t d2 = load32(input + 8);
		uint32_t d3 = load32(input + 12);
		uint32_t type = d0 & 1;
		uint32_t face = (d0 >> 1) & 1;
		uint32_t vertex_index[3] = { d0 >> 16, d1 & 0xffff, d1 >> 16 };
		uint32_t depth[3] = { 0, 0, 0 };
		int32_t x[3] = { 2047, 2047, 2047 };
		int32_t y[3] = { 1023, 1023, 1023 };
		unsigned vertex;
		int valid = 1;

		for (vertex = 0; vertex < 3; ++vertex)
		{
			uint32_t address = (uint32_t)source + vertex_index[vertex] * 8;
			uint32_t packed_depth;
			if (address + 8 > XAVIX2_LOW_RAM_SIZE)
			{
				valid = 0;
				break;
			}
			packed_depth = load32(machine->low_ram + address);
			depth[vertex] = packed_depth & UINT32_C(0x7fffffff);
			x[vertex] = (int16_t)load16(machine->low_ram + address + 6) >> 1;
			y[vertex] = (int16_t)load16(machine->low_ram + address + 4) >> 1;
			if (packed_depth >> 31)
				valid = 0;
		}
		if (valid && !face)
		{
			int64_t cross = (int64_t)(x[0] - x[1]) * (y[2] - y[1]) -
				(int64_t)(y[0] - y[1]) * (x[2] - x[1]);
			if (cross >= 0)
				valid = 0;
		}
		if (!valid && remove_invalid)
			continue;
		if (valid && calculate_parameters)
		{
			if (!type)
			{
				uint64_t weight_b = depth[1] ?
					((uint64_t)depth[0] << 6) / depth[1] : UINT64_MAX;
				uint64_t weight_c = depth[2] ?
					((uint64_t)depth[0] << 6) / depth[2] : UINT64_MAX;
				if (weight_b > 0xff) weight_b = 0xff;
				if (weight_c > 0xff) weight_c = 0xff;
				d2 = (d2 & ~UINT32_C(0x000000ff)) | (uint32_t)weight_b;
				d3 = (d3 & ~UINT32_C(0x00007f80)) |
					((uint32_t)weight_c << 7);
			}
			{
				uint16_t calculated_depth = geometry_depth_float(
					(uint64_t)depth[0] + depth[1] + depth[2],
					machine->mmio[PROJECTOR_DEPTH_BIAS_REGISTER] & 7);
				int32_t initial_depth = geometry_signed12((d3 >> 15) & 0xfff);
				uint32_t result_depth =
					(uint32_t)(initial_depth + calculated_depth) & 0xfff;
				d3 = (d3 & ~UINT32_C(0x07ff8000)) | (result_depth << 15);
			}
		}
		if (!valid)
		{
			x[0] = x[1] = x[2] = 2047;
			y[0] = y[1] = y[2] = 1023;
		}
		geometry_store_polygon_coordinates(machine->low_ram + polygon_address +
			output_count * 16, type, x, y, d2, d3);
		++output_count;
	}
	store16(machine->mmio + PROJECTOR_OUTPUT_COUNT_REGISTER,
		(uint16_t)output_count);
}

static uint16_t geometry_integer_sqrt(uint32_t value)
{
	uint32_t result = 0;
	uint32_t bit = UINT32_C(1) << 30;

	while (bit > value)
		bit >>= 2;
	while (bit)
	{
		if (value >= result + bit)
		{
			value -= result + bit;
			result = (result >> 1) + bit;
		}
		else
			result >>= 1;
		bit >>= 2;
	}
	return (uint16_t)result;
}

static void projector_start(xavix2_machine_t *machine, uint8_t command)
{
	uint32_t source;
	uint32_t destination;
	uint32_t count;
	int32_t focal;
	uint32_t index;

	uint8_t command_code = command & 0x1f;
	uint32_t vector_count =
		(uint32_t)load16(machine->mmio + PROJECTOR_COUNT_REGISTER) + 1;

	/* GECommand is five command bits followed by InterruptEnable and
	 * InvalidStructureRemove.  0x4d is therefore CWD with compacting, not a
	 * second projection format. */
	if (command_code == 0x01)
	{
		geometry_affine(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_DESTINATION_REGISTER),
			vector_count);
		return;
	}
	if (command_code == 0x02)
	{
		geometry_perspective_project(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_DESTINATION_REGISTER),
			vector_count);
		return;
	}
	if (command_code == 0x03)
	{
		geometry_viewport(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_DESTINATION_REGISTER),
			vector_count);
		return;
	}
	if (command_code == 0x0c)
	{
		geometry_apv(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_DESTINATION_REGISTER),
			vector_count);
		return;
	}
	if (command_code == 0x04 || command_code == 0x0d)
	{
		geometry_cull_and_calculate(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_POLYGON_REGISTER),
			vector_count, (command & 0x40) != 0, command_code == 0x0d);
		return;
	}
	command = command_code;

	/* Command 2 projects signed Q16.16 depth/vertical/horizontal triples.
	 * Firmware applies the final /2 and screen-center translation itself.
	 * The unit is synchronous from the CPU's point of view, so E859's busy
	 * bit can remain clear after this routine returns. */
	if (command == 0x10)
	{
		geometry_matrix_multiply(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_DESTINATION_REGISTER));
		return;
	}
	if (command == 0x0c)
	{
		geometry_transform_vertices(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_DESTINATION_REGISTER),
			(uint32_t)load16(machine->mmio + PROJECTOR_COUNT_REGISTER) + 1);
		return;
	}
	if (command == 0x01)
	{
		geometry_transform_points(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_DESTINATION_REGISTER),
			(uint32_t)load16(machine->mmio + PROJECTOR_COUNT_REGISTER) + 1);
		return;
	}
	if (command == 0x0d)
	{
		geometry_project_points(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_POLYGON_REGISTER),
			(uint32_t)load16(machine->mmio + PROJECTOR_COUNT_REGISTER) + 1);
		return;
	}
	if (command == 0x07)
	{
		geometry_transform_vectors16(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_DESTINATION_REGISTER),
			(uint32_t)load16(machine->mmio + PROJECTOR_COUNT_REGISTER) + 1);
		return;
	}
	if (command == 0x0f)
	{
		geometry_transform_and_dot_normals(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_DESTINATION_REGISTER),
			(uint32_t)load16(machine->mmio + PROJECTOR_COUNT_REGISTER) + 1);
		return;
	}
	if (command == 0x0b)
	{
		geometry_calculate_vertex_colors(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_POLYGON_REGISTER),
			(uint32_t)load16(machine->mmio + PROJECTOR_COUNT_REGISTER) + 1);
		return;
	}
	if (command == 0x0e)
	{
		geometry_calculate_polygon_lights(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_POLYGON_REGISTER),
			(uint32_t)load16(machine->mmio + PROJECTOR_COUNT_REGISTER) + 1);
		return;
	}
	if (command == 0x4d)
	{
		geometry_project_triangles(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_POLYGON_REGISTER),
			(uint32_t)load16(machine->mmio + PROJECTOR_COUNT_REGISTER) + 1);
		return;
	}
	if (command == 0x11)
	{
		uint32_t input = load32(machine->mmio + 0x800);
		uint16_t result = geometry_integer_sqrt(input);
		/* Firmware uses command 11 as an unsigned 32-bit square root.  The
		 * input is written to E800 and the 16-bit floor result is read from
		 * E804.  Naruto uses it for live two-dimensional hit distances. */
		store16(machine->mmio + 0x804, result);
		return;
	}
	if (command != 2)
		return;
	source = load16(machine->mmio + PROJECTOR_SOURCE_REGISTER);
	destination = load16(machine->mmio + PROJECTOR_DESTINATION_REGISTER);
	count = (uint32_t)load16(machine->mmio + PROJECTOR_COUNT_REGISTER) + 1;
	focal = (int16_t)load16(machine->mmio + PROJECTOR_FOCAL_REGISTER);
	if (source + count * 12 > XAVIX2_LOW_RAM_SIZE ||
		destination + count * 8 > XAVIX2_LOW_RAM_SIZE)
		return;

	for (index = 0; index < count; ++index)
	{
		const uint8_t *input = machine->low_ram + source + index * 12;
		uint8_t *output = machine->low_ram + destination + index * 8;
		int32_t depth = (int32_t)load32(input);
		int32_t vertical = (int32_t)load32(input + 4);
		int32_t horizontal = (int32_t)load32(input + 8);
		int64_t projected_vertical = 0;
		int64_t projected_horizontal = 0;

		if (depth > 0)
		{
			projected_vertical = ((int64_t)vertical * focal * 2) / depth;
			projected_horizontal = ((int64_t)horizontal * focal * 2) / depth;
		}
		store16(output + 4, (uint16_t)projected_vertical);
		store16(output + 6, (uint16_t)projected_horizontal);
	}
}

static int sample_experimental_pio(xavix2_machine_t *machine)
{
	uint32_t previous = load32(machine->low_ram + 0x0da4);
	uint32_t current = pio_read(machine) & UINT32_C(0x005fffa0);
	uint32_t changed = previous ^ current;
	uint32_t rising = changed & current;
	uint32_t falling = changed & ~current;
	store32(machine->low_ram + 0x0da4, current);
	store32(machine->low_ram + 0x0da8, rising);
	store32(machine->low_ram + 0x0dac, falling);
	if (rising)
		store32(machine->low_ram + 0x0db0, rising);
	machine->experimental_sampled_pio = current;
	return changed != 0;
}

static uint64_t machine_read64(xavix2_machine_t *machine, uint32_t address)
{
	uint64_t result = 0;
	unsigned index;
	for (index = 0; index < 8; ++index)
		result |= (uint64_t)machine_read8(machine, address + index) << (index * 8);
	return result;
}

static void refresh_interrupt_line(xavix2_machine_t *machine)
{
	xavix2_cpu_set_interrupt(&machine->cpu, machine->interrupt_pending != 0);
}

static unsigned first_interrupt_level(uint32_t lines)
{
	unsigned level;
	for (level = 0; level < 32; ++level)
		if (lines & (UINT32_C(1) << level))
			return level;
	return 32;
}

static void clear_interrupts(xavix2_machine_t *machine, uint32_t mask)
{
	machine->interrupt_active &= ~mask;
	machine->interrupt_pending &= ~mask;
	if (machine->interrupt_latched_valid &&
		(mask & (UINT32_C(1) << machine->interrupt_latched_level)))
		machine->interrupt_latched_valid = 0;
	refresh_interrupt_line(machine);
}

static void acknowledge_interrupt(void *opaque)
{
	xavix2_machine_t *machine = (xavix2_machine_t *)opaque;
	unsigned level = first_interrupt_level(machine->interrupt_pending);
	if (level < 32)
	{
		machine->interrupt_pending &= ~(UINT32_C(1) << level);
		machine->interrupt_latched_level = (uint8_t)level;
		machine->interrupt_latched_valid = 1;
	}
	refresh_interrupt_line(machine);
}

static void raise_interrupt(xavix2_machine_t *machine, unsigned level)
{
	uint32_t line = UINT32_C(1) << level;
	if ((machine->interrupt_enabled | machine->interrupt_nmi) & line)
	{
		if (!(machine->interrupt_active & line))
			machine->interrupt_pending |= line;
		machine->interrupt_active |= line;
		refresh_interrupt_line(machine);
	}
}

void xavix2_machine_raise_irq(xavix2_machine_t *machine, unsigned level)
{
	if (machine && level < 32)
		raise_interrupt(machine, level);
}

void xavix2_machine_clear_irq(xavix2_machine_t *machine, unsigned level)
{
	if (machine && level < 32)
		clear_interrupts(machine, UINT32_C(1) << level);
}

static int service_external_interrupt(xavix2_machine_t *machine,
	unsigned level)
{
	uint64_t interrupts_before;
	uint64_t level_reads_before;
	uint32_t line;
	unsigned steps;
	int interrupted_wait;

	if (!machine || level >= 32 || (!machine->cpu.waiting &&
		!(machine->cpu.hr[4] & CPU_FLAG_INTERRUPT_ENABLE)))
		return 0;
	interrupted_wait = machine->cpu.waiting != 0;
	line = UINT32_C(1) << level;
	if ((machine->interrupt_active | machine->interrupt_pending) & line)
		return 0;
	interrupts_before = machine->cpu.interrupt_count;
	level_reads_before = machine->irq_level_read_count;
	raise_interrupt(machine, level);
	if (!(machine->interrupt_active & line))
		return 0;
	for (steps = 0; steps < 256 &&
		machine->irq_level_read_count == level_reads_before; ++steps)
		(void)xavix2_cpu_execute(&machine->cpu, 1);
	clear_interrupts(machine, line);
	if (machine->cpu.interrupt_count == interrupts_before ||
		machine->irq_level_read_count == level_reads_before)
		return 0;
	for (steps = 0; steps < 4096 &&
		!(interrupted_wait ? machine->cpu.waiting :
			(machine->cpu.hr[4] & CPU_FLAG_INTERRUPT_ENABLE)); ++steps)
		(void)xavix2_cpu_execute(&machine->cpu, 1);
	return interrupted_wait ? machine->cpu.waiting != 0 :
		(machine->cpu.hr[4] & CPU_FLAG_INTERRUPT_ENABLE) != 0;
}

int xavix2_machine_transmit_epoch_ir(xavix2_machine_t *machine,
	uint32_t serial_word)
{
	const uint32_t receiver_lines = (UINT32_C(1) << 2) | (UINT32_C(1) << 10);
	uint32_t saved_pio;
	unsigned counter;
	int bit;

	if (!machine || machine->interrupt_pending ||
		((machine->interrupt_enabled | machine->interrupt_nmi) &
			receiver_lines) != receiver_lines ||
		(!machine->cpu.waiting &&
			!(machine->cpu.hr[4] & CPU_FLAG_INTERRUPT_ENABLE)) ||
		(machine->interrupt_active & receiver_lines))
		return 0;
	saved_pio = machine->pio_input;
	/* Epoch's Take-copter receiver starts a sample with IRQ2, then clocks
	 * PIO7 into the firmware's shift register with up to 27 IRQ10 samples.
	 * Once armed, IRQ2 consumes the first count itself, so the firmware's
	 * live counter determines whether this packet needs 26 or 27 clocks. */
	if (!service_external_interrupt(machine, 2))
		return 0;
	counter = machine->low_ram[0x02e2];
	if (counter > 0x1a)
	{
		machine->pio_input = saved_pio;
		return 0;
	}
	for (bit = (int)counter; bit >= 0; --bit)
	{
		machine->pio_input = (saved_pio & ~(UINT32_C(1) << 7)) |
			(((serial_word >> bit) & 1) << 7);
		if (!service_external_interrupt(machine, 10))
		{
			machine->pio_input = saved_pio;
			return 0;
		}
	}
	machine->pio_input = saved_pio;
	return 1;
}

void xavix2_machine_set_capture(xavix2_machine_t *machine,
	uint16_t capture_a, uint16_t capture_b)
{
	if (!machine)
		return;
	machine->experimental_capture_readback = 1;
	machine->experimental_capture_a = capture_a;
	machine->experimental_capture_b = capture_b;
}

static int pio_uses_epoch_24c04_pins(const xavix2_machine_t *machine)
{
	uint32_t mode1 = load32(machine->mmio + 0x204);
	/* Epoch's documented 24C04 boards use PIO16/SDA and PIO17/SCL.  The
	 * Bandai wrist-receiver boards configure PIO20/21 instead; prefer those
	 * pins whenever their mode fields are active so unrelated PIO16 outputs
	 * cannot steal the EEPROM bus. */
	return !(mode1 & UINT32_C(0x00000f00)) &&
		(mode1 & UINT32_C(0x0000000f));
}

static void update_pio(xavix2_machine_t *machine)
{
	uint32_t mode0 = load32(machine->mmio + 0x200);
	uint32_t mode1 = load32(machine->mmio + 0x204);
	uint32_t data = load32(machine->mmio + 0x208);
	uint32_t mask = 0;
	unsigned bit;

	for (bit = 0; bit < 32; ++bit)
	{
		uint32_t modes = bit < 16 ? mode0 : mode1;
		unsigned shift = (bit & 15) * 2;
		if (((modes >> shift) & 3) == 3)
			mask |= UINT32_C(1) << bit;
	}
	machine->pio_output_mask = mask;
	if (pio_uses_epoch_24c04_pins(machine))
		xavix_eeprom24c04_set_lines(&machine->eeprom,
			(mask & (UINT32_C(1) << 17)) ?
				!!(data & (UINT32_C(1) << 17)) : 0,
			(mask & (UINT32_C(1) << 16)) ?
				!!(data & (UINT32_C(1) << 16)) : 1);
	else
		xavix_eeprom24c08_set_lines(&machine->eeprom,
			(mask & (UINT32_C(1) << 20)) ?
				!!(data & (UINT32_C(1) << 20)) : 0,
			(mask & (UINT32_C(1) << 21)) ?
				!!(data & (UINT32_C(1) << 21)) : 1);
}

static uint32_t pio_read(const xavix2_machine_t *machine)
{
	uint32_t input = machine->pio_input;
	uint32_t output = load32(machine->mmio + 0x208);
	if (pio_uses_epoch_24c04_pins(machine))
	{
		if (xavix_eeprom24c08_read_sda(&machine->eeprom))
			input |= UINT32_C(1) << 16;
		else
			input &= ~(UINT32_C(1) << 16);
	}
	else if (xavix_eeprom24c08_read_sda(&machine->eeprom))
		input |= UINT32_C(1) << 21;
	else
		input &= ~(UINT32_C(1) << 21);
	return (input & ~machine->pio_output_mask) |
		(output & machine->pio_output_mask);
}

static void note_unmapped_read(xavix2_machine_t *machine, uint32_t address)
{
	if (!machine->unmapped_read_count)
		machine->first_unmapped_read = address;
	machine->unmapped_read_count++;
}

static void note_unmapped_write(xavix2_machine_t *machine, uint32_t address)
{
	if (!machine->unmapped_write_count)
		machine->first_unmapped_write = address;
	machine->unmapped_write_count++;
}

static void note_audio_mmio_access(xavix2_machine_t *machine,
	uint32_t offset, uint8_t data, int write)
{
	xavix2_capture_trace_entry *entry;
	if (offset < XAVIX2_AUDIO_MMIO_FIRST ||
		offset >= XAVIX2_AUDIO_MMIO_FIRST + XAVIX2_AUDIO_MMIO_SIZE)
		return;
	entry = &machine->audio_mmio_trace[machine->audio_mmio_trace_next];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)offset;
	entry->data = data;
	entry->write = (uint8_t)write;
	machine->audio_mmio_trace_next =
		(machine->audio_mmio_trace_next + 1) % XAVIX2_AUDIO_TRACE_CAPACITY;
	machine->audio_mmio_trace_total++;
}

static void note_capture_access(xavix2_machine_t *machine, uint32_t offset,
	uint8_t data, int write)
{
	unsigned index = (unsigned)(offset - XAVIX2_CAPTURE_REGISTER_FIRST);
	xavix2_capture_trace_entry *entry;
	if (index >= XAVIX2_CAPTURE_REGISTER_COUNT)
		return;
	if (write)
		machine->capture_write_count[index]++;
	else
		machine->capture_read_count[index]++;
	if (machine->capture_trace_count >= XAVIX2_CAPTURE_TRACE_CAPACITY)
	{
		machine->capture_trace_dropped++;
		return;
	}
	entry = &machine->capture_trace[machine->capture_trace_count++];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)offset;
	entry->data = data;
	entry->write = (uint8_t)(write != 0);
}

static uint8_t capture_read(xavix2_machine_t *machine, uint32_t offset)
{
	uint8_t value;
	if (machine->experimental_capture_readback && offset >= 0x244 && offset <= 0x245)
		value = (uint8_t)(machine->experimental_capture_a >> ((offset - 0x244) * 8));
	else if (machine->experimental_capture_readback && offset >= 0x24a && offset <= 0x24b)
		value = (uint8_t)(machine->experimental_capture_b >> ((offset - 0x24a) * 8));
	else if ((offset >= 0x244 && offset <= 0x245) ||
		(offset >= 0x24a && offset <= 0x24b))
		value = 0;
	else
		value = machine->mmio[offset];
	note_capture_access(machine, offset, value, 0);
	return value;
}

static void note_sensor_buffer_access(xavix2_machine_t *machine,
	uint32_t address, uint8_t data, int write)
{
	xavix2_capture_trace_entry *entry;
	if (write)
		machine->sensor_buffer_write_count++;
	else
		machine->sensor_buffer_read_count++;
	if (machine->sensor_buffer_trace_count >= XAVIX2_CAPTURE_TRACE_CAPACITY)
	{
		machine->sensor_buffer_trace_dropped++;
		return;
	}
	entry = &machine->sensor_buffer_trace[machine->sensor_buffer_trace_count++];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)address;
	entry->data = data;
	entry->write = (uint8_t)(write != 0);
}

static void note_irq_context_access(xavix2_machine_t *machine,
	uint32_t address, uint8_t data, int write)
{
	xavix2_capture_trace_entry *entry;
	if (machine->cpu.total_cycles < machine->diagnostic_trace_start_cycle)
		return;
	if (write)
		machine->irq_context_write_count++;
	else
		machine->irq_context_read_count++;
	if (machine->irq_context_trace_count >= XAVIX2_CAPTURE_TRACE_CAPACITY)
	{
		machine->irq_context_trace_dropped++;
		return;
	}
	entry = &machine->irq_context_trace[machine->irq_context_trace_count++];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)address;
	entry->data = data;
	entry->write = (uint8_t)(write != 0);
}

static void note_sensor_decoded_access(xavix2_machine_t *machine,
	uint32_t address, uint8_t data, int write)
{
	xavix2_capture_trace_entry *entry;
	if (machine->cpu.total_cycles < machine->diagnostic_trace_start_cycle)
		return;
	if (write)
		machine->sensor_decoded_write_count++;
	else
		machine->sensor_decoded_read_count++;
	if (machine->sensor_decoded_trace_count >= XAVIX2_CAPTURE_TRACE_CAPACITY)
	{
		machine->sensor_decoded_trace_dropped++;
		return;
	}
	entry = &machine->sensor_decoded_trace[machine->sensor_decoded_trace_count++];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)address;
	entry->data = data;
	entry->write = (uint8_t)(write != 0);
}

static void note_motion_sample_access(xavix2_machine_t *machine,
	uint32_t address, uint8_t data, int write)
{
	xavix2_capture_trace_entry *entry;
	if (machine->cpu.total_cycles < machine->diagnostic_trace_start_cycle)
		return;
	if (write)
		machine->motion_sample_write_count++;
	else
		machine->motion_sample_read_count++;
	if (machine->motion_sample_trace_count >= XAVIX2_CAPTURE_TRACE_CAPACITY)
	{
		machine->motion_sample_trace_dropped++;
		return;
	}
	entry = &machine->motion_sample_trace[machine->motion_sample_trace_count++];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)address;
	entry->data = data;
	entry->write = (uint8_t)(write != 0);
}

static void note_motion_source_access(xavix2_machine_t *machine,
	uint32_t address, uint8_t data, int write)
{
	xavix2_capture_trace_entry *entry;
	if (machine->cpu.total_cycles < machine->diagnostic_trace_start_cycle)
		return;
	if (write)
		machine->motion_source_write_count++;
	else
		machine->motion_source_read_count++;
	if (machine->motion_source_trace_count >= XAVIX2_CAPTURE_TRACE_CAPACITY)
	{
		machine->motion_source_trace_dropped++;
		return;
	}
	entry = &machine->motion_source_trace[machine->motion_source_trace_count++];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)address;
	entry->data = data;
	entry->write = (uint8_t)(write != 0);
}

static void note_action_state_access(xavix2_machine_t *machine,
	uint32_t address, uint8_t data, int write)
{
	xavix2_capture_trace_entry *entry;
	if (machine->cpu.total_cycles < machine->diagnostic_trace_start_cycle)
		return;
	if (write)
		machine->action_state_write_count++;
	else
		machine->action_state_read_count++;
	if (machine->action_state_trace_count >= XAVIX2_CAPTURE_TRACE_CAPACITY)
	{
		machine->action_state_trace_dropped++;
		return;
	}
	entry = &machine->action_state_trace[machine->action_state_trace_count++];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)address;
	entry->data = data;
	entry->write = (uint8_t)(write != 0);
}

static void note_diagnostic_ram_access(xavix2_machine_t *machine,
	uint32_t address, uint8_t data, int write)
{
	xavix2_capture_trace_entry *entry;
	if (machine->cpu.total_cycles < machine->diagnostic_trace_start_cycle ||
		address < machine->diagnostic_ram_first ||
		address > machine->diagnostic_ram_last)
		return;
	if (write)
		machine->diagnostic_ram_write_count++;
	else
		machine->diagnostic_ram_read_count++;
	if (machine->diagnostic_ram_trace_count >= XAVIX2_CAPTURE_TRACE_CAPACITY)
	{
		machine->diagnostic_ram_trace_dropped++;
		return;
	}
	entry = &machine->diagnostic_ram_trace[machine->diagnostic_ram_trace_count++];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)address;
	entry->data = data;
	entry->write = (uint8_t)(write != 0);
}

static uint8_t machine_read8(void *opaque, uint32_t address)
{
	xavix2_machine_t *machine = (xavix2_machine_t *)opaque;
	uint32_t offset;
	uint32_t value;

	if (address < XAVIX2_LOW_RAM_SIZE)
	{
		note_diagnostic_ram_access(machine, address,
			machine->low_ram[address], 0);
		if (address >= XAVIX2_MOTION_SAMPLE_FIRST &&
			address < XAVIX2_MOTION_SAMPLE_FIRST + XAVIX2_MOTION_SAMPLE_SIZE)
			note_motion_sample_access(machine, address,
				machine->low_ram[address], 0);
		if (address >= XAVIX2_IRQ_CONTEXT_FIRST &&
			address < XAVIX2_IRQ_CONTEXT_FIRST + XAVIX2_IRQ_CONTEXT_SIZE)
			note_irq_context_access(machine, address,
				machine->low_ram[address], 0);
		if (address >= XAVIX2_SENSOR_BUFFER_FIRST &&
			address < XAVIX2_SENSOR_BUFFER_FIRST + XAVIX2_SENSOR_BUFFER_SIZE)
			note_sensor_buffer_access(machine, address,
				machine->low_ram[address], 0);
		if (address >= XAVIX2_SENSOR_DECODED_FIRST &&
			address < XAVIX2_SENSOR_DECODED_FIRST + XAVIX2_SENSOR_DECODED_SIZE)
			note_sensor_decoded_access(machine, address,
				machine->low_ram[address], 0);
		if (address >= XAVIX2_MOTION_SOURCE_FIRST &&
			address < XAVIX2_MOTION_SOURCE_FIRST + XAVIX2_MOTION_SOURCE_SIZE)
			note_motion_source_access(machine, address,
				machine->low_ram[address], 0);
		if (address >= XAVIX2_ACTION_STATE_FIRST &&
			address < XAVIX2_ACTION_STATE_FIRST + XAVIX2_ACTION_STATE_SIZE)
			note_action_state_access(machine, address,
				machine->low_ram[address], 0);
		if (machine->pio_input && address >= 0x0da4 && address <= 0x0db3)
		{
			machine->input_state_read_count++;
			machine->last_input_state_read_pc = machine->cpu.pc;
			machine->last_input_state_read_address = (uint16_t)address;
			memcpy(machine->last_input_state_regs, machine->cpu.r,
				sizeof(machine->last_input_state_regs));
		}
		return machine->low_ram[address];
	}
	if (address >= UINT32_C(0x00010000) && address <= UINT32_C(0x00ffffff))
		return address < machine->rom_size ? machine->rom[address] : 0;
	if (address >= UINT32_C(0x40000000) && address <= UINT32_C(0x40ffffff))
	{
		offset = address - UINT32_C(0x40000000);
		return offset < machine->rom_size ? machine->rom[offset] : 0;
	}
	if (address >= UINT32_C(0xc0000000) && address <= UINT32_C(0xc00007ff))
		return machine->palette_ram[address - UINT32_C(0xc0000000)];
	if (address >= UINT32_C(0xc0000800) && address <= UINT32_C(0xc001ffff))
		return machine->video_ram[address - UINT32_C(0xc0000800)];

	if (address >= UINT32_C(0xffffe000))
	{
		offset = address - UINT32_C(0xffffe000);
		if (offset < XAVIX2_MMIO_SIZE)
		{
			machine->mmio_read_counts[offset]++;
			machine->mmio_last_read_pc[offset] = machine->cpu.pc;
			note_audio_mmio_access(machine, offset, machine->mmio[offset], 0);
			if (offset == 0x010)
				return (machine->interrupt_active & (UINT32_C(1) << IRQ_DMA)) ? 6 : 0;
			/*
			 * The IRQ 8 prologue compares the undocumented 16-bit registers at
			 * e244 and e24a until they match.  They are not backed by storage in
			 * the current MAME map, so unknown writes must not make them diverge.
			 */
			if (offset >= XAVIX2_CAPTURE_REGISTER_FIRST &&
				offset < XAVIX2_CAPTURE_REGISTER_FIRST + XAVIX2_CAPTURE_REGISTER_COUNT)
				return capture_read(machine, offset);
			if (offset >= 0x208 && offset <= 0x20b)
			{
				value = pio_read(machine);
				machine->pio_read_count++;
				machine->last_pio_read_pc = machine->cpu.pc;
				machine->last_pio_read_value = value;
				if (machine->pio_input)
				{
					machine->pio_input_read_count++;
					machine->pio_observed_input_or |= value;
				}
				return (uint8_t)(value >> ((offset - 0x208) * 8));
			}
			if (offset == 0x238)
				return 0;
			if (offset == 0x239)
				return 2;
			if (offset == GPU_STATUS_REGISTER ||
				offset == GPU_STATUS_REGISTER + 1)
			{
				uint16_t status = GPU_STATUS_VIDEO_MODE |
					GPU_STATUS_TRIANGLE_READY |
					GPU_STATUS_SPRITE_READY;
				return (uint8_t)(status >>
					((offset - GPU_STATUS_REGISTER) * 8));
			}
			if (offset == 0x630 || offset == 0x632) return 0x10;
			if (offset == 0x631 || offset == 0x633) return 0x02;
			/*
			 * Firmware in multiple titles uses bits 1:0 as a debounce
			 * counter while bit 2 reflects an external controller/power
			 * status line.  A write must not clear that input line.
			 */
			if (offset == CONTROLLER_POWER_STATUS_REGISTER)
				return (machine->mmio[offset] &
					CONTROLLER_POWER_STATUS_COUNTER_MASK) |
					CONTROLLER_POWER_STATUS_GOOD;
			if (offset >= 0xa10 && offset <= 0xa17)
				return xavix2_audio_status(&machine->audio, offset - 0xa10);
			if (offset == 0x1c00)
			{
				machine->irq_level_read_count++;
				if (machine->interrupt_latched_valid)
					return machine->interrupt_latched_level;
				{
					unsigned level = first_interrupt_level(machine->interrupt_pending);
					if (level < 32)
						return (uint8_t)level;
				}
				return 0xff;
			}
			if (offset == 0x1c08 || offset == 0x1c09)
				return (uint8_t)(machine->interrupt_nmi >> ((offset - 0x1c08) * 8));
			if (offset == 0x1c0a || offset == 0x1c0b)
				return (uint8_t)(machine->interrupt_enabled >> ((offset - 0x1c0a) * 8));
			return machine->mmio[offset];
		}
	}

	note_unmapped_read(machine, address);
	return 0;
}

static void dma_start(xavix2_machine_t *machine, uint8_t control)
{
	uint32_t source;
	uint32_t destination;
	uint16_t count;
	uint32_t index;

	if (control != 3 && control != 7)
		return;
	source = load32(machine->mmio + 0x000);
	if (control == 7)
		source |= UINT32_C(0x40000000);
	destination = load16(machine->mmio + 0x004);
	count = load16(machine->mmio + 0x008);
	for (index = 0; index < count; ++index)
	{
		uint8_t data = machine_read8(machine, source + index);
		if (destination + index < XAVIX2_LOW_RAM_SIZE)
		{
			machine->program_ram[destination + index] = data;
			machine->low_ram[destination + index] = data;
		}
		else
			note_unmapped_write(machine, destination + index);
	}
	machine->dma_transfer_count++;
	machine->dma_completion_cycle = machine->cpu.total_cycles + (count ? count : 1);
}

static uint32_t rgb555(uint16_t color)
{
	uint32_t r = color & 31;
	uint32_t g = (color >> 5) & 31;
	uint32_t b = (color >> 10) & 31;
	r = (r << 3) | (r >> 2);
	g = (g << 3) | (g >> 2);
	b = (b << 3) | (b >> 2);
	return UINT32_C(0xff000000) | (r << 16) | (g << 8) | b;
}

static uint32_t rgb555_polygon_light(uint16_t color, uint32_t light)
{
	uint32_t source = rgb555(color);
	uint32_t output = UINT32_C(0xff000000);
	unsigned shift;

	/* Polygon Light is a five-bit fixed-point multiplier: zero means 1/32
	 * brightness and 31 means 32/32.  Sprites bypass this stage. */
	light = (light & 31) + 1;
	for (shift = 0; shift <= 16; shift += 8)
		output |= ((((source >> shift) & 0xff) * light) >> 5) << shift;
	return output;
}

static int palette_color_lit_coverage(uint16_t raw_color,
	uint32_t destination, uint32_t light, uint32_t *result,
	uint32_t *destination_weight)
{
	uint32_t source;
	uint32_t output = UINT32_C(0xff000000);
	uint32_t inverse_alpha;
	unsigned shift;

	if (!result)
		return 0;
	if (!(raw_color & UINT16_C(0x8000)))
	{
		*result = rgb555_polygon_light(raw_color, light);
		if (destination_weight)
			*destination_weight = 0;
		return 1;
	}

	/* Translucent entries interleave three-bit Nalpha with premultiplied
	 * RGB444: B3..B0,N2,G3..G0,N1,R3..R0,N0.  Clearing those Nalpha bits
	 * restores the equivalent RGB555 value with a zero channel LSB. */
	inverse_alpha = (((uint32_t)raw_color >> 8) & 4) |
		(((uint32_t)raw_color >> 4) & 2) | (raw_color & 1);
	/* Textured palette Nalpha=7 is the transparent endpoint.  EPOCH uses it
	 * for the complete rectangular border of clouds, character planes and
	 * title artwork; blending it as 7/8 leaves visible bounding boxes. */
	if (inverse_alpha == 7)
	{
		if (destination_weight)
			*destination_weight = 256;
		return 0;
	}
	source = rgb555_polygon_light(
		raw_color & (uint16_t)~UINT16_C(0x8421), light);
	for (shift = 0; shift <= 16; shift += 8)
	{
		uint32_t component = ((source >> shift) & 0xff) +
			((((destination >> shift) & 0xff) * inverse_alpha) >> 3);
		if (component > 0xff)
			component = 0xff;
		output |= component << shift;
	}
	*result = output;
	if (destination_weight)
		*destination_weight = inverse_alpha * 32;
	return 1;
}
static int palette_color_lit(uint16_t raw_color, uint32_t destination,
	uint32_t light, uint32_t *result)
{
	return palette_color_lit_coverage(raw_color, destination, light, result,
		NULL);
}
typedef struct gpu_order
{
	uint32_t priority;
	uint32_t area;
	uint32_t index;
} gpu_order_t;

static int compare_gpu_order(const void *left, const void *right)
{
	const gpu_order_t *a = (const gpu_order_t *)left;
	const gpu_order_t *b = (const gpu_order_t *)right;
	const uint32_t backing_minimum_area = UINT32_C(256) * 128;
	if (a->priority != b->priority)
		return a->priority < b->priority ? 1 : -1;
	/* Ordinary equal-depth objects retain firmware list order.  DB2J builds a
	 * character from many small tiles, then submits its attack effect at the
	 * same depth; sorting every object by area incorrectly puts that effect
	 * behind the character.  Presentation-scale backings are the exception:
	 * DBZ and DB2J place them on opposite sides of their overlay commands, so
	 * paint only those large surfaces first. */
	if ((a->area >= backing_minimum_area) !=
		(b->area >= backing_minimum_area))
		return a->area >= backing_minimum_area ? -1 : 1;
	return a->index < b->index ? -1 : a->index > b->index ? 1 : 0;
}

static uint32_t gpu_descriptor_size(xavix2_machine_t *machine,
	uint16_t table, uint32_t descriptor_index)
{
	return (uint32_t)machine_read8(machine,
		table + 4 * descriptor_index) |
		((uint32_t)machine_read8(machine,
			table + 4 * descriptor_index + 1) << 8) |
		((uint32_t)machine_read8(machine,
			table + 4 * descriptor_index + 2) << 16) |
		((uint32_t)machine_read8(machine,
			table + 4 * descriptor_index + 3) << 24);
}

static void restore_tiled_effect_painter_order(xavix2_machine_t *machine,
	gpu_order_t *order, uint32_t selected_count, uint16_t count,
	uint16_t address, uint16_t descriptor_table)
{
	uint32_t run_start = 0;
	uint32_t run_count = 0;
	uint32_t run_descriptor = UINT32_MAX;
	uint32_t effect_index = UINT32_MAX;
	uint32_t list_index;
	uint32_t effect_position = UINT32_MAX;
	uint32_t last_tile_position = 0;
	int found_tile_position = 0;

	/* Dragon Ball assembles a fighter from a long consecutive run of small,
	 * depth-zero tiles and immediately follows it with one large attack sprite.
	 * The weak red sphere remains depth zero while it grows past the generic
	 * backing-area threshold; the charged form can use a nonzero depth.  The
	 * hardware's active-line merger lets either later effect
	 * cover the assembled fighter.  Keep the normal depth sort for every other
	 * object and repair only this structurally identifiable overlap; preserving
	 * the whole E414 list breaks result screens whose backing/logo bands depend
	 * on Depth. */
	for (list_index = 0; list_index < count; ++list_index)
	{
		uint64_t command = machine_read64(machine,
			(uint32_t)address + 8 * list_index);
		uint32_t depth = (uint32_t)((command >> 21) & 0xff);
		uint32_t descriptor_index = (uint32_t)((command >> 30) & 0x3f);
		uint32_t descriptor = gpu_descriptor_size(machine, descriptor_table,
			descriptor_index);
		uint32_t width = 1 + (descriptor & 0xff);
		uint32_t height = 1 + ((descriptor >> 8) & 0xff);

		if (!depth && width <= 32 && height <= 32)
		{
			if (!run_count || descriptor_index != run_descriptor)
			{
				run_start = list_index;
				run_count = 1;
				run_descriptor = descriptor_index;
			}
			else
				run_count++;
			continue;
		}
		if (run_count >= 8 && width >= 64 && height >= 64)
		{
			effect_index = list_index;
			break;
		}
		run_count = 0;
		run_descriptor = UINT32_MAX;
	}
	if (effect_index == UINT32_MAX)
		return;
	for (list_index = 0; list_index < selected_count; ++list_index)
	{
		if (order[list_index].index == effect_index)
			effect_position = list_index;
		if (order[list_index].index >= run_start &&
			order[list_index].index < run_start + run_count)
		{
			last_tile_position = list_index;
			found_tile_position = 1;
		}
	}
	if (effect_position == UINT32_MAX || !found_tile_position ||
		effect_position > last_tile_position)
		return;
	{
		gpu_order_t effect = order[effect_position];
		memmove(order + effect_position, order + effect_position + 1,
			(size_t)(last_tile_position - effect_position) * sizeof(*order));
		order[last_tile_position] = effect;
	}
}

static int64_t triangle_edge(int32_t ax, int32_t ay, int32_t bx, int32_t by,
	int32_t px, int32_t py)
{
	return (int64_t)(px - ax) * (by - ay) -
		(int64_t)(py - ay) * (bx - ax);
}

static int triangle_edge_is_top_left(int32_t ax, int32_t ay, int32_t bx,
	int32_t by)
{
	int32_t dx = bx - ax;
	int32_t dy = by - ay;
	/* RPU scan conversion uses half-open edge intervals.  With screen Y
	 * increasing downwards, downward edges and leftward horizontal edges own
	 * their boundary samples; the adjacent triangle excludes the same samples.
	 * This is essential for translucent meshes, where drawing a shared edge
	 * twice applies Nalpha twice and exposes the triangulation as bright seams. */
	return dy > 0 || (dy == 0 && dx < 0);
}

static uint32_t triangle_texture_coordinate(uint32_t coordinate,
	uint32_t maximum, uint32_t mask_bits)
{
	if (mask_bits >= 8)
		coordinate = 0;
	else if (mask_bits)
		coordinate &= (UINT32_C(1) << (8 - mask_bits)) - 1;
	if (coordinate > maximum)
		coordinate = maximum;
	return coordinate;
}

typedef struct triangle_texture_sampler
{
	xavix2_machine_t *machine;
	uint32_t source;
	uint32_t width;
	uint32_t height;
	uint32_t bit_code;
	uint32_t bpp;
	uint32_t block_width;
	uint32_t block_height;
	uint32_t blocks_per_row;
	uint32_t mask_u_bits;
	uint32_t mask_v_bits;
	uint32_t map;
	uint32_t palette_base;
	uint32_t filter;
	uint32_t light;
	uint32_t cache_tag[16];
	uint64_t cache_data[16];
	uint16_t cache_valid;
} triangle_texture_sampler_t;

static int triangle_texture_sampler_init(triangle_texture_sampler_t *sampler,
	xavix2_machine_t *machine, uint16_t segment_table, uint32_t descriptor,
	uint32_t d2, uint32_t d3)
{
	static const uint8_t block_width[8] = { 8, 8, 7, 4, 4, 5, 3, 4 };
	static const uint8_t block_height[8] = { 8, 4, 3, 4, 3, 2, 3, 2 };
	static const uint8_t map0_width[8] =
		{ 64, 32, 21, 16, 12, 10, 9, 8 };
	static const uint8_t map0_height[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
	uint16_t texture_segment = (uint16_t)(d2 >> 16);
	uint32_t segment_index = texture_segment >> 10;
	uint32_t segment_offset = (texture_segment & 0x3ff) << 5;
	uint32_t segment_address = (uint32_t)segment_table + segment_index * 2;
	uint16_t segment;

	if (!sampler || segment_address + 2 > XAVIX2_LOW_RAM_SIZE)
		return 0;
	segment = load16(machine->low_ram + segment_address);
	sampler->machine = machine;
	sampler->source = ((uint32_t)segment << 14) + segment_offset;
	sampler->width = descriptor & 0xff;
	sampler->height = (descriptor >> 8) & 0xff;
	sampler->bit_code = (descriptor >> 24) & 7;
	sampler->bpp = sampler->bit_code + 1;
	sampler->block_width = block_width[sampler->bit_code];
	sampler->block_height = block_height[sampler->bit_code];
	sampler->blocks_per_row = sampler->width / sampler->block_width + 1;
	sampler->mask_u_bits = (descriptor >> 16) & 0x0f;
	sampler->mask_v_bits = (descriptor >> 20) & 0x0f;
	sampler->map = (d3 >> 6) & 1;
	sampler->block_width = sampler->map ? block_width[sampler->bit_code] :
		map0_width[sampler->bit_code];
	sampler->block_height = sampler->map ? block_height[sampler->bit_code] :
		map0_height[sampler->bit_code];
	sampler->blocks_per_row = sampler->width / sampler->block_width + 1;
	sampler->palette_base = ((descriptor >> 27) & 0x1f) << sampler->bpp;
	/* The firmware Type-0 packer maps flag bit 3 through (flags & 0x38) << 24,
	 * so Filter is d3 bit 27; bits 28..29 select the viewport. */
	sampler->filter = (d3 >> 27) & 1;
	sampler->light = (d2 >> 10) & 31;
	sampler->cache_valid = 0;
	return 1;
}

static uint16_t triangle_texture_raw_color(triangle_texture_sampler_t *sampler,
	uint32_t u, uint32_t v)
{
	uint32_t s = u;
	uint32_t t = v;
	uint32_t bit_address;
	uint32_t word_address;
	uint32_t cache_index;
	uint16_t cache_mask;
	uint64_t packed;
	uint32_t texel;
	uint32_t palette_index;
	u = triangle_texture_coordinate(u, sampler->width, sampler->mask_u_bits);
	v = triangle_texture_coordinate(v, sampler->height, sampler->mask_v_bits);
	s = u;
	t = v;

	/* SSD's block-address equations (US20090278845) apply to both map
	 * layouts.  Divided storage is selected only for an unmasked texture
	 * taller than one hardware block; Map 0 and Map 1 fold the lower half
	 * differently.  BAD deliberately keeps the original V row.  The
	 * sampler is shared by nearest and all four bilinear neighbours so folding
	 * and edge handling cannot diverge between the two filter modes. */
	if (!sampler->mask_u_bits && !sampler->mask_v_bits &&
		sampler->height > sampler->block_height &&
		v / sampler->block_height >
			sampler->height / (2 * sampler->block_height))
	{
		s = sampler->blocks_per_row * sampler->block_width - u - 1;
		t = sampler->map ?
			(sampler->height / sampler->block_height + 1) *
				sampler->block_height - v - 1 : sampler->height - v;
	}
	word_address = sampler->blocks_per_row * (t / sampler->block_height) +
		s / sampler->block_width;
	bit_address = ((v % sampler->block_height) * sampler->block_width +
		s % sampler->block_width) * sampler->bpp;
	/* Four adjacent texels normally share one 64-bit storage word.  Cache the
	 * exact existing machine_read64 result per triangle so bilinear filtering
	 * does not multiply eight-byte bus reads by four. */
	cache_index = (word_address ^ (word_address >> 4)) & 15;
	cache_mask = (uint16_t)(UINT16_C(1) << cache_index);
	if (!(sampler->cache_valid & cache_mask) ||
		sampler->cache_tag[cache_index] != word_address)
	{
		sampler->cache_tag[cache_index] = word_address;
		sampler->cache_data[cache_index] = machine_read64(sampler->machine,
			sampler->source + word_address * 8);
		sampler->cache_valid |= cache_mask;
	}
	packed = sampler->cache_data[cache_index];
	texel = (uint32_t)(packed >> bit_address) &
		((UINT32_C(1) << sampler->bpp) - 1);
	palette_index = sampler->palette_base | texel;
	return palette_index < 0x200 ? load16(sampler->machine->palette_ram +
		palette_index * 4) : UINT16_C(0x8000);
}

static void triangle_texture_premultiplied(uint16_t raw_color,
	uint32_t light, uint32_t component[3], uint32_t *alpha)
{
	uint32_t color;
	if (raw_color & UINT16_C(0x8000))
	{
		uint32_t inverse_alpha = (((uint32_t)raw_color >> 8) & 4) |
			(((uint32_t)raw_color >> 4) & 2) | (raw_color & 1);
		if (inverse_alpha == 7)
		{
			component[0] = component[1] = component[2] = 0;
			*alpha = 0;
			return;
		}
		color = rgb555_polygon_light(
			raw_color & (uint16_t)~UINT16_C(0x8421), light);
		*alpha = 256 - inverse_alpha * 32;
	}
	else
	{
		color = rgb555_polygon_light(raw_color, light);
		*alpha = 256;
	}
	component[0] = (color >> 16) & 0xff;
	component[1] = (color >> 8) & 0xff;
	component[2] = color & 0xff;
}
static int bilinear_palette_color(const uint16_t raw_color[4],
	uint32_t fraction_u, uint32_t fraction_v, uint32_t light,
	uint32_t *color, uint32_t *destination_weight)
{
	uint32_t sample[4][3];
	uint32_t alpha[4];
	uint64_t weight[4];
	uint32_t output = UINT32_C(0xff000000);
	uint64_t alpha_sum = 0;
	uint32_t output_alpha;
	unsigned index;
	unsigned component;

	if (!color)
		return 0;
	/* Constant 2x2 regions are common and exactly match the ordinary palette
	 * operation, including the interleaved RGB444/Nalpha encoding. */
	if (raw_color[0] == raw_color[1] && raw_color[0] == raw_color[2] &&
		raw_color[0] == raw_color[3])
		return palette_color_lit_coverage(raw_color[0], *color, light, color,
			destination_weight);
	weight[0] = (uint64_t)(UINT32_C(0x10000) - fraction_u) *
		(UINT32_C(0x10000) - fraction_v);
	weight[1] = (uint64_t)fraction_u *
		(UINT32_C(0x10000) - fraction_v);
	weight[2] = (uint64_t)(UINT32_C(0x10000) - fraction_u) * fraction_v;
	weight[3] = (uint64_t)fraction_u * fraction_v;
	/* With four opaque entries the output is also opaque. */
	if (!((raw_color[0] | raw_color[1] | raw_color[2] | raw_color[3]) &
		UINT16_C(0x8000)))
	{
		uint32_t palette_color_value[4];
		uint32_t red;
		uint32_t green;
		uint32_t blue;
		for (index = 0; index < 4; ++index)
			palette_color_value[index] = rgb555_polygon_light(raw_color[index],
				light);
		red = (uint32_t)((weight[0] * ((palette_color_value[0] >> 16) & 0xff) +
			weight[1] * ((palette_color_value[1] >> 16) & 0xff) +
			weight[2] * ((palette_color_value[2] >> 16) & 0xff) +
			weight[3] * ((palette_color_value[3] >> 16) & 0xff)) >> 32);
		green = (uint32_t)((weight[0] * ((palette_color_value[0] >> 8) & 0xff) +
			weight[1] * ((palette_color_value[1] >> 8) & 0xff) +
			weight[2] * ((palette_color_value[2] >> 8) & 0xff) +
			weight[3] * ((palette_color_value[3] >> 8) & 0xff)) >> 32);
		blue = (uint32_t)((weight[0] * (palette_color_value[0] & 0xff) +
			weight[1] * (palette_color_value[1] & 0xff) +
			weight[2] * (palette_color_value[2] & 0xff) +
			weight[3] * (palette_color_value[3] & 0xff)) >> 32);
		*color = UINT32_C(0xff000000) | (red << 16) | (green << 8) | blue;
		if (destination_weight)
			*destination_weight = 0;
		return 1;
	}
	for (index = 0; index < 4; ++index)
	{
		triangle_texture_premultiplied(raw_color[index], light,
			sample[index], alpha + index);
		alpha_sum += weight[index] * alpha[index];
	}
	output_alpha = (uint32_t)(alpha_sum >> 32);
	for (component = 0; component < 3; ++component)
	{
		uint64_t source_sum = 0;
		uint32_t source_component;
		uint32_t destination_component;
		uint32_t result;
		for (index = 0; index < 4; ++index)
			source_sum += weight[index] * sample[index][component];
		source_component = (uint32_t)(source_sum >> 32);
		destination_component = (*color >> (16 - component * 8)) & 0xff;
		result = source_component +
			(destination_component * (256 - output_alpha) >> 8);
		if (result > 0xff)
			result = 0xff;
		output |= result << (16 - component * 8);
	}
	*color = output;
	if (destination_weight)
		*destination_weight = 256 - output_alpha;
	return output_alpha != 0;
}
static int triangle_texture_color(triangle_texture_sampler_t *sampler,
	uint32_t u_fixed, uint32_t v_fixed, uint32_t *color)
{
	uint32_t u = u_fixed >> 16;
	uint32_t v = v_fixed >> 16;
	uint16_t raw_color[4];
	uint32_t fraction_u;
	uint32_t fraction_v;

	if (!color)
		return 0;
	if (sampler->filter)
	{
		raw_color[0] = triangle_texture_raw_color(sampler, u, v);
		return palette_color_lit(raw_color[0], *color, sampler->light, color);
	}
	/* CN101116112A, Fig. 21: Filter=0 uses the four texels surrounding the
	 * fractional (U,V), weighted by (1-u)(1-v), u(1-v), (1-u)v and uv.
	 * Keep 16 fractional bits through perspective division. */
	raw_color[0] = triangle_texture_raw_color(sampler, u, v);
	raw_color[1] = triangle_texture_raw_color(sampler, u + 1, v);
	raw_color[2] = triangle_texture_raw_color(sampler, u, v + 1);
	raw_color[3] = triangle_texture_raw_color(sampler, u + 1, v + 1);
	fraction_u = u_fixed & 0xffff;
	fraction_v = v_fixed & 0xffff;
	return bilinear_palette_color(raw_color, fraction_u, fraction_v,
		sampler->light, color, NULL);
}
static uint32_t blend_triangle_gouraud(const xavix2_machine_t *machine,
	uint32_t d2, uint32_t d3, int64_t w0, int64_t w1, int64_t w2,
	int64_t area, uint32_t x, uint32_t y, uint32_t destination)
{
	uint16_t vertex[3] = {
		(uint16_t)(d2 & 0x7fff),
		(uint16_t)((d2 >> 16) & 0x7fff),
		(uint16_t)(d3 & 0x7fff)
	};
	uint32_t source_component[3];
	uint32_t destination_component[3];
	uint32_t output_component[3];
	uint32_t inverse_alpha = (d3 >> 29) & 7;
	uint32_t dither = (machine->mmio[0x620] >>
		((((y & 1) << 1) | (x & 1)) * 2)) & 3;
	unsigned component;

	for (component = 0; component < 3; ++component)
	{
		unsigned shift = component * 5;
		int64_t interpolated = w0 * ((vertex[0] >> shift) & 31) +
			w1 * ((vertex[1] >> shift) & 31) +
			w2 * ((vertex[2] >> shift) & 31);
		int64_t positive_area = area;
		int32_t five_bit;
		if (positive_area < 0)
		{
			positive_area = -positive_area;
			interpolated = -interpolated;
		}
		/* CN101116112A, Fig. 20: E620 contains four two-bit values,
		 * selected by the X/Y coordinate LSBs and added to the fractional
		 * Gouraud result before RGB555 quantization. EPOCH programs C6 and
		 * Bandai programs D8; both are permutations of 0,1,2,3. */
		five_bit = (int32_t)((interpolated * 4 +
			positive_area * dither) / (positive_area * 4));
		if (five_bit < 0) five_bit = 0;
		if (five_bit > 31) five_bit = 31;
		source_component[component] =
			((uint32_t)five_bit << 3) | ((uint32_t)five_bit >> 2);
	}
	destination_component[0] = (destination >> 16) & 0xff;
	destination_component[1] = (destination >> 8) & 0xff;
	destination_component[2] = destination & 0xff;
	/* Nalpha stores 1-alpha: zero is opaque and seven retains 7/8 of the
	 * destination.  Like the indexed-palette path, Type-1 vertex RGB is
	 * already premultiplied by alpha.  Blue Dragon's boss-shadow strip makes
	 * this explicit: its RGB555 values decrease in lockstep with increasing
	 * Nalpha.  Multiplying the source by alpha again makes the deforming strip
	 * fade twice and visually disconnect from the dragon. */
	for (component = 0; component < 3; ++component)
	{
		uint32_t value = source_component[component] +
			(destination_component[component] * inverse_alpha >> 3);
		output_component[component] = value > 0xff ? 0xff : value;
	}
	return UINT32_C(0xff000000) | (output_component[0] << 16) |
		(output_component[1] << 8) | output_component[2];
}

#define POLYGON_COVERAGE_NEAR UINT32_C(0xfd000000)
#define POLYGON_COVERAGE_TERRAIN UINT32_C(0xfe000000)

static int high_resolution_3d_active(const xavix2_machine_t *machine)
{
	return machine && high_resolution_3d.enabled &&
		high_resolution_3d.owner == machine;
}

static int skip_render_active(const xavix2_machine_t *machine)
{
	return machine && skip_render_enabled && skip_render_owner == machine;
}

static void high_resolution_3d_mark_polygon(
	const xavix2_machine_t *machine, uint32_t x, uint32_t y)
{
	uint32_t pixel;
	if (!high_resolution_3d_active(machine) || x >= 0x800 || y >= 0x400)
		return;
	pixel = y * 0x800 + x;
	high_resolution_3d_polygon_mask[pixel >> 3] |=
		(uint8_t)(1U << (pixel & 7));
}

static void high_resolution_3d_mark_sprite(
	const xavix2_machine_t *machine, uint32_t x, uint32_t y,
	uint32_t destination_weight)
{
	uint32_t pixel;
	if (!high_resolution_3d_active(machine) || x >= 0x800 || y >= 0x400)
		return;
	/* The presentation filter follows the RGB already composed by the RPU.
	 * A fully opaque sprite replaces the polygon underneath and therefore owns
	 * the output pixel.  A translucent sprite retains part of that polygon via
	 * Nalpha, so keep the polygon mark as well.  Clearing it unconditionally
	 * made DBZ energy auras and other translucent effects switch to nearest
	 * neighbour exactly where they crossed 3D characters, exposing a detached
	 * rectangular layer instead of one continuous blended result. */
	if (destination_weight != 0)
		return;
	pixel = y * 0x800 + x;
	high_resolution_3d_polygon_mask[pixel >> 3] &=
		(uint8_t)~(1U << (pixel & 7));
}

static int high_resolution_3d_is_polygon(uint32_t x, uint32_t y)
{
	uint32_t pixel;
	if (x >= 0x800 || y >= 0x400)
		return 0;
	pixel = y * 0x800 + x;
	return (high_resolution_3d_polygon_mask[pixel >> 3] >>
		(pixel & 7)) & 1;
}
static void finish_polygon_coverage(xavix2_machine_t *machine)
{
	uint32_t origin_x = load16(machine->mmio + 0x656);
	uint32_t origin_y = load16(machine->mmio + 0x658);
	uint16_t display_mode = load16(machine->mmio + 0x650);
	uint32_t width = machine->mmio[0x650] == 0x08 ? 640 : 320;
	uint32_t height = display_mode == 0x0008 ||
		(display_mode == 0x1608 && origin_y != 0x0110) ? 480 : 240;
	uint32_t x;
	int have_rpu_base = rpu_surface_snapshot.valid &&
		rpu_surface_snapshot.owner == machine &&
		rpu_surface_snapshot.frame_count == machine->frame_count;

	if (origin_x + width > 0x800 || origin_y + height > 0x400)
	{
		/* Synthetic and malformed modes can omit a valid presentation window.
		 * Rendering may still have marked the internal target, so always restore
		 * its opaque presentation alpha even when seam closure is unavailable. */
		uint32_t pixel;
		for (pixel = 0; pixel < UINT32_C(0x800) * 0x400; ++pixel)
			if ((machine->screen_data[pixel] & UINT32_C(0xff000000)) ==
				POLYGON_COVERAGE_NEAR ||
				(machine->screen_data[pixel] & UINT32_C(0xff000000)) ==
				POLYGON_COVERAGE_TERRAIN)
				machine->screen_data[pixel] |= UINT32_C(0xff000000);
		if (have_rpu_base)
			rpu_surface_snapshot.valid = 0;
		return;
	}
	/* Separate terrain models occasionally meet with a short gap after the CWD
	 * unit has quantized both boundaries.  Until its exact cross-batch edge rule
	 * is characterized, close only short vertical runs bounded by far terrain
	 * on both sides.  Nearer geometry is marked separately and transparent
	 * texels retain their triangle coverage. */
	for (x = 0; x < width; ++x)
	{
		uint32_t y = 1;
		while (y + 1 < height)
		{
			uint32_t pixel = (origin_y + y) * 0x800 + origin_x + x;
			uint32_t start;
			if ((machine->screen_data[pixel] & UINT32_C(0xff000000)) !=
				UINT32_C(0xff000000) ||
				(machine->screen_data[pixel - 0x800] & UINT32_C(0xff000000)) !=
				POLYGON_COVERAGE_TERRAIN ||
				rpu_pixel_is_sprite(origin_x + x, origin_y + y) ||
				(have_rpu_base && machine->screen_data[pixel] !=
					rpu_surface_snapshot_pixels[pixel]))
			{
				++y;
				continue;
			}
			start = y;
			while (y < height &&
				(machine->screen_data[(origin_y + y) * 0x800 + origin_x + x] &
					UINT32_C(0xff000000)) == UINT32_C(0xff000000) &&
				!rpu_pixel_is_sprite(origin_x + x, origin_y + y) &&
				(!have_rpu_base ||
					machine->screen_data[(origin_y + y) * 0x800 + origin_x + x] ==
					rpu_surface_snapshot_pixels[
						(origin_y + y) * 0x800 + origin_x + x]))
				++y;
			if (y < height && y - start <= 16 &&
				(machine->screen_data[(origin_y + y) * 0x800 + origin_x + x] &
					UINT32_C(0xff000000)) == POLYGON_COVERAGE_TERRAIN)
			{
				uint32_t color = machine->screen_data[
					(origin_y + y) * 0x800 + origin_x + x] |
					UINT32_C(0xff000000);
				uint32_t fill;
				for (fill = start; fill < y; ++fill)
				{
					machine->screen_data[(origin_y + fill) * 0x800 +
						origin_x + x] = color;
					high_resolution_3d_mark_polygon(machine,
						origin_x + x, origin_y + fill);
				}
			}
		}
	}
	for (x = 0; x < width; ++x)
	{
		uint32_t y;
		for (y = 0; y < height; ++y)
		{
			uint32_t pixel = (origin_y + y) * 0x800 + origin_x + x;
			if ((machine->screen_data[pixel] & UINT32_C(0xff000000)) ==
				POLYGON_COVERAGE_NEAR ||
				(machine->screen_data[pixel] & UINT32_C(0xff000000)) ==
				POLYGON_COVERAGE_TERRAIN)
				machine->screen_data[pixel] |= UINT32_C(0xff000000);
		}
	}
	if (have_rpu_base)
		rpu_surface_snapshot.valid = 0;
}

static void render_triangle_gpu_rows(xavix2_machine_t *machine, uint16_t count,
	uint16_t address, uint16_t minimum_depth, uint16_t maximum_depth, int type,
	uint16_t minimum_render_y, uint16_t maximum_render_y)
{
	uint16_t descsize_address = load16(machine->mmio + 0x608);
	uint16_t descdata_address = load16(machine->mmio + 0x622);
	uint32_t record_index;

	for (record_index = 0; record_index < count; ++record_index)
	{
		uint32_t record = (uint32_t)address + record_index * 16;
		uint32_t d0;
		uint32_t d1;
		uint32_t d2;
		uint32_t d3;
		uint32_t descriptor_index;
		uint32_t depth;
		uint32_t descsize;
		uint32_t width;
		uint32_t height;
		uint32_t weight_b;
		uint32_t weight_c;
		triangle_texture_sampler_t texture_sampler;
		int32_t x[3];
		int32_t y[3];
		int32_t min_x;
		int32_t max_x;
		int32_t min_y;
		int32_t max_y;
		uint32_t visible_width;
		uint32_t visible_height;
		uint32_t visible_x;
		uint32_t visible_y;
		uint16_t display_mode;
		int64_t area;
		int32_t py;

		if (record + 16 > XAVIX2_LOW_RAM_SIZE)
			break;
		d0 = load32(machine->low_ram + record);
		d1 = load32(machine->low_ram + record + 4);
		d2 = load32(machine->low_ram + record + 8);
		d3 = load32(machine->low_ram + record + 12);
		depth = (d3 >> 15) & 0xfff;
		if (depth < minimum_depth || depth > maximum_depth)
			continue;
		x[0] = (int32_t)((d0 >> 11) & 0x7ff);
		y[0] = (int32_t)((d0 >> 1) & 0x3ff);
		x[1] = (int32_t)(d1 & 0x7ff);
		y[1] = (int32_t)((d0 >> 22) & 0x3ff);
		if (type >= 0 && (int)(d0 & 1) != type)
			continue;
		x[2] = (int32_t)((d1 >> 21) & 0x7ff);
		y[2] = (int32_t)((d1 >> 11) & 0x3ff);
		/* triangle_edge receives doubled pixel centers below; double the
		 * vertices here as well so its arithmetic stays integral. */
		area = triangle_edge(x[0] * 2, y[0] * 2, x[1] * 2, y[1] * 2,
			x[2] * 2, y[2] * 2);
		if (!area)
			continue;

		if (!(d0 & 1))
		{
			descriptor_index = d3 & 0x3f;
			if ((uint32_t)descsize_address + descriptor_index * 4 + 4 >
				XAVIX2_LOW_RAM_SIZE)
				continue;
			descsize = load32(machine->low_ram + descsize_address +
				descriptor_index * 4);
			width = descsize & 0xff;
			height = (descsize >> 8) & 0xff;
			/* Polygon record layout and perspective weights match
			 * CN101116112A, Fig. 13: A=(0,0,64), B=(W,0,Bw),
			 * C=(0,H,Cw). */
			weight_b = d2 & 0xff;
			weight_c = (d3 >> 7) & 0xff;
			if (!triangle_texture_sampler_init(&texture_sampler, machine,
				descdata_address, descsize, d2, d3))
				continue;
		}
		else
		{
			descsize = width = height = 0;
			weight_b = weight_c = 0;
		}

		min_x = x[0] < x[1] ? x[0] : x[1];
		if (x[2] < min_x) min_x = x[2];
		max_x = x[0] > x[1] ? x[0] : x[1];
		if (x[2] > max_x) max_x = x[2];
		min_y = y[0] < y[1] ? y[0] : y[1];
		if (y[2] < min_y) min_y = y[2];
		max_y = y[0] > y[1] ? y[0] : y[1];
		if (y[2] > max_y) max_y = y[2];
		if (min_x < 0) min_x = 0;
		if (min_y < 0) min_y = 0;
		if (max_x >= 0x800) max_x = 0x7ff;
		if (max_y >= 0x400) max_y = 0x3ff;

		/* screen_data is a presentation target and is never read by the guest.
		 * Rasterizing every off-screen part of the 2048x1024 internal surface
		 * made Dragon Ball's polygon-heavy battles hover around the real-time
		 * limit and stutter in the GUI.  Clip the host raster loop to the same
		 * guest-selected output window returned by visible_frame().  Keep the
		 * full target for reset-time synthetic tests where no origin is set. */
		visible_x = load16(machine->mmio + 0x656);
		visible_y = load16(machine->mmio + 0x658);
		display_mode = load16(machine->mmio + 0x650);
		visible_width = machine->mmio[0x650] == 0x08 ? 640 : 320;
		visible_height = display_mode == 0x0008 ||
			(display_mode == 0x1608 && visible_y != 0x0110) ? 480 : 240;
		if ((visible_x || visible_y) &&
			visible_x + visible_width <= 0x800 &&
			visible_y + visible_height <= 0x400)
		{
			int32_t clip_max_x = (int32_t)(visible_x + visible_width - 1);
			int32_t clip_max_y = (int32_t)(visible_y + visible_height - 1);
			if (max_x < (int32_t)visible_x || min_x > clip_max_x ||
				max_y < (int32_t)visible_y || min_y > clip_max_y)
				continue;
			if (min_x < (int32_t)visible_x) min_x = (int32_t)visible_x;
			if (max_x > clip_max_x) max_x = clip_max_x;
			if (min_y < (int32_t)visible_y) min_y = (int32_t)visible_y;
			if (max_y > clip_max_y) max_y = clip_max_y;
		}
		if (min_y < (int32_t)minimum_render_y)
			min_y = (int32_t)minimum_render_y;
		if (max_y > (int32_t)maximum_render_y)
			max_y = (int32_t)maximum_render_y;
		if (min_y > max_y)
			continue;

		for (py = min_y; py <= max_y; ++py)
		{
			int32_t px;
			for (px = min_x; px <= max_x; ++px)
			{
				int32_t sample_x = px * 2 + 1;
				int32_t sample_y = py * 2 + 1;
				int64_t w0 = triangle_edge(x[1] * 2, y[1] * 2,
					x[2] * 2, y[2] * 2, sample_x, sample_y);
				int64_t w1 = triangle_edge(x[2] * 2, y[2] * 2,
					x[0] * 2, y[0] * 2, sample_x, sample_y);
				int64_t w2 = triangle_edge(x[0] * 2, y[0] * 2,
					x[1] * 2, y[1] * 2, sample_x, sample_y);
				int reverse_edges = area < 0;
				int64_t coverage_w0 = reverse_edges ? -w0 : w0;
				int64_t coverage_w1 = reverse_edges ? -w1 : w1;
				int64_t coverage_w2 = reverse_edges ? -w2 : w2;
				uint32_t u_fixed = 0;
				uint32_t v_fixed = 0;
				uint32_t color;
				int64_t denominator;
				int64_t numerator_u;
				int64_t numerator_v;
				if (coverage_w0 < 0 || coverage_w1 < 0 || coverage_w2 < 0 ||
					(coverage_w0 == 0 && !triangle_edge_is_top_left(
						reverse_edges ? x[2] : x[1],
						reverse_edges ? y[2] : y[1],
						reverse_edges ? x[1] : x[2],
						reverse_edges ? y[1] : y[2])) ||
					(coverage_w1 == 0 && !triangle_edge_is_top_left(
						reverse_edges ? x[0] : x[2],
						reverse_edges ? y[0] : y[2],
						reverse_edges ? x[2] : x[0],
						reverse_edges ? y[2] : y[0])) ||
					(coverage_w2 == 0 && !triangle_edge_is_top_left(
						reverse_edges ? x[1] : x[0],
						reverse_edges ? y[1] : y[0],
						reverse_edges ? x[0] : x[1],
						reverse_edges ? y[0] : y[1])))
					continue;
				high_resolution_3d_mark_polygon(machine,
					(uint32_t)px, (uint32_t)py);

				{
					uint32_t marker = depth >= UINT32_C(0x0800) ?
						POLYGON_COVERAGE_TERRAIN : POLYGON_COVERAGE_NEAR;
					machine->screen_data[py * 0x800 + px] =
						(machine->screen_data[py * 0x800 + px] &
							UINT32_C(0x00ffffff)) | marker;
				}
				if (d0 & 1)
				{
					color = blend_triangle_gouraud(machine, d2, d3, w0, w1,
						w2, area, (uint32_t)px, (uint32_t)py,
						machine->screen_data[py * 0x800 + px]);
					machine->screen_data[py * 0x800 + px] =
						(color & UINT32_C(0x00ffffff)) |
						(depth >= UINT32_C(0x0800) ? POLYGON_COVERAGE_TERRAIN :
							POLYGON_COVERAGE_NEAR);
					machine->gpu_pixel_write_count++;
					continue;
				}
				denominator = w0 * 64 + w1 * weight_b + w2 * weight_c;
				if (!denominator)
					continue;
				numerator_u = w1 * (int64_t)width * weight_b;
				numerator_v = w2 * (int64_t)height * weight_c;
				if (denominator < 0)
				{
					denominator = -denominator;
					numerator_u = -numerator_u;
					numerator_v = -numerator_v;
				}
				/* Preserve the fractional perspective result for Filter=0.  The
				 * raw sampler applies descriptor masks and endpoint clamps to each
				 * of the four neighbours independently. */
				if (numerator_u > 0)
				{
					uint64_t value = ((uint64_t)numerator_u << 16) /
						(uint64_t)denominator;
					u_fixed = value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
				}
				if (numerator_v > 0)
				{
					uint64_t value = ((uint64_t)numerator_v << 16) /
						(uint64_t)denominator;
					v_fixed = value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
				}
				color = machine->screen_data[py * 0x800 + px];
				if (triangle_texture_color(&texture_sampler, u_fixed, v_fixed,
					&color))
				{
					machine->screen_data[py * 0x800 + px] =
						(color & UINT32_C(0x00ffffff)) |
						(depth >= UINT32_C(0x0800) ? POLYGON_COVERAGE_TERRAIN :
							POLYGON_COVERAGE_NEAR);
					machine->gpu_pixel_write_count++;
				}
			}
		}

	}
}

static void render_triangle_gpu(xavix2_machine_t *machine, uint16_t count,
	uint16_t address, uint16_t minimum_depth, uint16_t maximum_depth, int type)
{
	render_triangle_gpu_rows(machine, count, address, minimum_depth,
		maximum_depth, type, 0, 0x03ff);
}

static uint16_t triangle_minimum_depth(const xavix2_machine_t *machine,
	uint16_t count, uint16_t address)
{
	uint16_t minimum = UINT16_C(0x0fff);
	uint32_t index;
	int found = 0;

	for (index = 0; index < count; ++index)
	{
		uint32_t record = (uint32_t)address + index * 16;
		uint16_t depth;
		if (record + 16 > XAVIX2_LOW_RAM_SIZE)
			break;
		depth = (uint16_t)((load32(machine->low_ram + record + 12) >> 15) &
			0x0fff);
		if (!found || depth < minimum)
			minimum = depth;
		found = 1;
	}
	return found ? minimum : 0;
}

static int triangle_next_depth(const xavix2_machine_t *machine,
	uint16_t count, uint16_t address, uint16_t maximum, uint16_t *result)
{
	uint16_t best = 0;
	uint32_t index;
	int found = 0;

	for (index = 0; index < count; ++index)
	{
		uint32_t record = (uint32_t)address + index * 16;
		uint16_t depth;
		if (record + 16 > XAVIX2_LOW_RAM_SIZE)
			break;
		depth = (uint16_t)((load32(machine->low_ram + record + 12) >> 15) &
			0x0fff);
		if (depth <= maximum && (!found || depth > best))
		{
			best = depth;
			found = 1;
		}
	}
	if (found && result)
		*result = best;
	return found;
}

typedef struct sprite_texture_sampler
{
	xavix2_machine_t *machine;
	uint32_t source;
	uint32_t width;
	uint32_t height;
	uint32_t bpp;
	uint32_t palette_base;
	uint32_t texels_per_word;
	uint32_t words_per_row;
	uint32_t cache_tag[16];
	uint64_t cache_data[16];
	uint16_t cache_valid;
} sprite_texture_sampler_t;

static uint16_t sprite_texture_raw_color(sprite_texture_sampler_t *sampler,
	uint32_t u, uint32_t v)
{
	uint32_t word_address;
	uint32_t cache_index;
	uint16_t cache_mask;
	uint64_t packed;
	uint32_t texel;
	uint32_t palette_index;

	if (u >= sampler->width) u = sampler->width - 1;
	if (v >= sampler->height) v = sampler->height - 1;
	word_address = v * sampler->words_per_row +
		u / sampler->texels_per_word;
	cache_index = (word_address ^ (word_address >> 4)) & 15;
	cache_mask = (uint16_t)(UINT16_C(1) << cache_index);
	if (!(sampler->cache_valid & cache_mask) ||
		sampler->cache_tag[cache_index] != word_address)
	{
		sampler->cache_tag[cache_index] = word_address;
		sampler->cache_data[cache_index] = machine_read64(sampler->machine,
			sampler->source + word_address * 8);
		sampler->cache_valid |= cache_mask;
	}
	packed = sampler->cache_data[cache_index];
	texel = (uint32_t)(packed >>
		((u % sampler->texels_per_word) * sampler->bpp)) &
		((UINT32_C(1) << sampler->bpp) - 1);
	palette_index = sampler->palette_base | texel;
	return palette_index < 0x200 ? load16(sampler->machine->palette_ram +
		palette_index * 4) : UINT16_C(0x8000);
}

static uint32_t sprite_texture_coordinate(uint32_t pixel, uint32_t scale,
	uint32_t source_size)
{
	uint64_t coordinate;
	uint64_t maximum;

	if (!scale || source_size <= 1)
		return 0;
	/* Sample destination pixel centres.  Subtracting half a source texel
	 * keeps a 1.0x sprite bit-exact while giving magnified/minified sprites
	 * the fractional UV values consumed by Filter=0. */
	coordinate = (((uint64_t)pixel * 2 + 1) << 19) / scale;
	if (coordinate <= UINT32_C(0x8000))
		return 0;
	coordinate -= UINT32_C(0x8000);
	maximum = (uint64_t)(source_size - 1) << 16;
	return coordinate > maximum ? (uint32_t)maximum : (uint32_t)coordinate;
}

static int sprite_texture_color(sprite_texture_sampler_t *sampler,
	uint32_t u_fixed, uint32_t v_fixed, uint32_t filter, uint32_t *color,
	uint32_t *destination_weight)
{
	uint32_t u = u_fixed >> 16;
	uint32_t v = v_fixed >> 16;
	uint16_t raw_color[4];

	if (filter)
	{
		u = (u_fixed + UINT32_C(0x8000)) >> 16;
		v = (v_fixed + UINT32_C(0x8000)) >> 16;
		raw_color[0] = sprite_texture_raw_color(sampler, u, v);
		return palette_color_lit_coverage(raw_color[0], *color, 31, color,
			destination_weight);
	}
	raw_color[0] = sprite_texture_raw_color(sampler, u, v);
	raw_color[1] = sprite_texture_raw_color(sampler, u + 1, v);
	raw_color[2] = sprite_texture_raw_color(sampler, u, v + 1);
	raw_color[3] = sprite_texture_raw_color(sampler, u + 1, v + 1);
	return bilinear_palette_color(raw_color, u_fixed & 0xffff,
		v_fixed & 0xffff, 31, color, destination_weight);
}

static void render_rpu_sprite_rows(xavix2_machine_t *machine,
	uint64_t command, uint16_t minimum_render_y, uint16_t maximum_render_y)
{
	uint16_t descsize_address = load16(machine->mmio + 0x608);
	uint16_t descdata_address = load16(machine->mmio + 0x622);
	uint32_t descriptor_index = (uint32_t)((command >> 30) & 0x3f);
	uint32_t data_index = (uint32_t)((command >> 58) & 0x3f);
	uint32_t descsize = gpu_descriptor_size(machine, descsize_address,
		descriptor_index);
	uint16_t descdata = (uint16_t)(machine_read8(machine,
		descdata_address + 2 * data_index) |
		((uint16_t)machine_read8(machine,
			descdata_address + 2 * data_index + 1) << 8));
	uint32_t source = ((uint32_t)descdata << 14) +
		(uint32_t)((command >> 43) & 0x7fe0);
	uint32_t x = (uint32_t)(command & 0x7ff);
	uint32_t y = (uint32_t)((command >> 11) & 0x3ff);
	uint32_t source_width = 1 + (descsize & 0xff);
	uint32_t source_height = 1 + ((descsize >> 8) & 0xff);
	uint32_t scale_x = (uint32_t)((command >> 36) & 0x3f);
	uint32_t scale_y = (uint32_t)((command >> 42) & 0x3f);
	uint32_t output_width = (uint32_t)(((uint64_t)source_width * scale_x) >> 4);
	uint32_t output_height =
		(uint32_t)(((uint64_t)source_height * scale_y) >> 4);
	uint32_t bpp = 1 + ((descsize >> 24) & 7);
	uint32_t filter = (uint32_t)((command >> 29) & 1);
	uint32_t first_yy;
	uint32_t last_yy;
	uint32_t yy;
	sprite_texture_sampler_t sampler;

	if (!scale_x || !scale_y || !output_width || !output_height ||
		y >= 0x400 || y > maximum_render_y ||
		(uint64_t)y + output_height <= minimum_render_y)
		return;
	first_yy = minimum_render_y > y ? minimum_render_y - y : 0;
	last_yy = output_height - 1;
	if (maximum_render_y >= y && maximum_render_y - y < last_yy)
		last_yy = maximum_render_y - y;

	sampler.machine = machine;
	sampler.source = source;
	sampler.width = source_width;
	sampler.height = source_height;
	sampler.bpp = bpp;
	sampler.palette_base = ((descsize >> 27) & 0x1f) << bpp;
	sampler.texels_per_word = 64 / bpp;
	sampler.words_per_row = (source_width + sampler.texels_per_word - 1) /
		sampler.texels_per_word;
	sampler.cache_valid = 0;

	for (yy = first_yy; yy <= last_yy; ++yy)
	{
		uint32_t draw_y = y + yy;
		uint32_t v_fixed = sprite_texture_coordinate(yy, scale_y,
			source_height);
		uint32_t xx;
		for (xx = 0; xx < output_width; ++xx)
		{
			uint32_t draw_x = x + xx;
			uint32_t u_fixed;
			uint32_t color;
			uint32_t destination_weight;
			if (draw_x >= 0x800)
				continue;
			u_fixed = sprite_texture_coordinate(xx, scale_x, source_width);
			color = machine->screen_data[draw_y * 0x800 + draw_x];
			if (sprite_texture_color(&sampler, u_fixed, v_fixed, filter,
				&color, &destination_weight))
			{
				machine->screen_data[draw_y * 0x800 + draw_x] = color;
				machine->gpu_pixel_write_count++;
				rpu_mark_sprite_pixel(draw_x, draw_y);
				high_resolution_3d_mark_sprite(machine, draw_x, draw_y,
					destination_weight);
			}
		}
	}
}

static void render_gpu_depth_range(xavix2_machine_t *machine, uint16_t count,
	uint16_t address, uint8_t minimum_depth, uint8_t maximum_depth,
	int repair_tiled_effect_order)
{
	gpu_order_t *order;
	uint16_t descsize_address = load16(machine->mmio + 0x608);
	uint16_t descdata_address = load16(machine->mmio + 0x622);
	uint32_t list_index;
	uint32_t selected_count = 0;

	if (!count)
		return;
	order = (gpu_order_t *)malloc((size_t)count * sizeof(*order));
	if (!order)
		return;
	for (list_index = 0; list_index < count; ++list_index)
	{
		uint64_t command = machine_read64(machine,
			(uint32_t)address + 8 * list_index);
		uint32_t depth = (uint32_t)((command >> 21) & 0xff);
		uint32_t descriptor_index;
		uint32_t descriptor;
		uint32_t width;
		uint32_t height;
		if (depth < minimum_depth || depth > maximum_depth)
			continue;
		descriptor_index = (uint32_t)((command >> 30) & 0x3f);
		descriptor = (uint32_t)machine_read8(machine,
			descsize_address + 4 * descriptor_index) |
			((uint32_t)machine_read8(machine,
				descsize_address + 4 * descriptor_index + 1) << 8) |
			((uint32_t)machine_read8(machine,
				descsize_address + 4 * descriptor_index + 2) << 16) |
			((uint32_t)machine_read8(machine,
				descsize_address + 4 * descriptor_index + 3) << 24);
		width = (uint32_t)(((uint64_t)(1 + (descriptor & 0xff)) *
			((command >> 36) & 0x3f)) >> 4);
		height = (uint32_t)(((uint64_t)(1 + ((descriptor >> 8) & 0xff)) *
			((command >> 42) & 0x3f)) >> 4);
		order[selected_count].priority =
			(uint32_t)(command & UINT64_C(0x1fe00000));
		order[selected_count].area = width * height;
		order[selected_count].index = list_index;
		selected_count++;
	}
	qsort(order, selected_count, sizeof(*order), compare_gpu_order);
	if (repair_tiled_effect_order)
		restore_tiled_effect_painter_order(machine, order, selected_count,
			count, address, descsize_address);

	for (list_index = 0; list_index < selected_count; ++list_index)
	{
		uint32_t command_index = order[list_index].index;
		uint64_t command = machine_read64(machine,
			(uint32_t)address + 8 * command_index);
		uint32_t depth = (uint32_t)((command >> 21) & 0xff);
		uint32_t descriptor_index;
		uint32_t data_index;
		uint32_t descsize;
		uint16_t descdata;
		uint32_t source;
		uint32_t x;
		uint32_t y;
		uint32_t source_width;
		uint32_t source_height;
		uint32_t scale_x;
		uint32_t scale_y;
		uint32_t output_width;
		uint32_t output_height;
		uint32_t bpp;
		uint32_t filter;
		uint32_t yy;
		sprite_texture_sampler_t sampler;

		if (depth < minimum_depth || depth > maximum_depth)
			continue;
		descriptor_index = (uint32_t)((command >> 30) & 0x3f);
		data_index = (uint32_t)((command >> 58) & 0x3f);
		descsize = (uint32_t)machine_read8(machine,
			descsize_address + 4 * descriptor_index) |
			((uint32_t)machine_read8(machine,
				descsize_address + 4 * descriptor_index + 1) << 8) |
			((uint32_t)machine_read8(machine,
				descsize_address + 4 * descriptor_index + 2) << 16) |
			((uint32_t)machine_read8(machine,
				descsize_address + 4 * descriptor_index + 3) << 24);
		descdata = (uint16_t)(machine_read8(machine,
			descdata_address + 2 * data_index) |
			((uint16_t)machine_read8(machine,
				descdata_address + 2 * data_index + 1) << 8));
		source = ((uint32_t)descdata << 14) +
			(uint32_t)((command >> 43) & 0x7fe0);
		x = (uint32_t)(command & 0x7ff);
		y = (uint32_t)((command >> 11) & 0x3ff);
		source_width = 1 + (descsize & 0xff);
		source_height = 1 + ((descsize >> 8) & 0xff);
		/* The six-bit W/H fields are unsigned Q2.4 scale factors. */
		scale_x = (uint32_t)((command >> 36) & 0x3f);
		scale_y = (uint32_t)((command >> 42) & 0x3f);
		output_width = (uint32_t)(((uint64_t)source_width * scale_x) >> 4);
		output_height = (uint32_t)(((uint64_t)source_height * scale_y) >> 4);
		bpp = 1 + ((descsize >> 24) & 7);
		/* CN101116112A Fig. 16(a): unclipped sprite bit 29 is Filter;
		 * Fig. 21 defines zero as four-tap bilinear and one as nearest. */
		filter = (uint32_t)((command >> 29) & 1);
		if (!scale_x || !scale_y || !output_width || !output_height)
			continue;
		sampler.machine = machine;
		sampler.source = source;
		sampler.width = source_width;
		sampler.height = source_height;
		sampler.bpp = bpp;
		sampler.palette_base = ((descsize >> 27) & 0x1f) << bpp;
		sampler.texels_per_word = 64 / bpp;
		sampler.words_per_row = (source_width +
			sampler.texels_per_word - 1) / sampler.texels_per_word;
		sampler.cache_valid = 0;

		for (yy = 0; yy < output_height; ++yy)
		{
			uint32_t draw_y = y + yy;
			uint32_t v_fixed;
			uint32_t xx;
			if (draw_y >= 0x400)
				continue;
			v_fixed = sprite_texture_coordinate(yy, scale_y, source_height);
			for (xx = 0; xx < output_width; ++xx)
			{
				uint32_t draw_x = x + xx;
				uint32_t u_fixed;
				uint32_t color;
				uint32_t destination_weight;
				if (draw_x >= 0x800)
					continue;
				u_fixed = sprite_texture_coordinate(xx, scale_x, source_width);
				color = machine->screen_data[draw_y * 0x800 + draw_x];
				if (sprite_texture_color(&sampler, u_fixed, v_fixed,
					filter, &color, &destination_weight))
				{
					machine->screen_data[draw_y * 0x800 + draw_x] = color;
					machine->gpu_pixel_write_count++;
					rpu_mark_sprite_pixel(draw_x, draw_y);
					high_resolution_3d_mark_sprite(machine, draw_x, draw_y,
						destination_weight);
				}
			}
		}
	}
	free(order);
}

typedef enum rpu_primitive_kind
{
	RPU_PRIMITIVE_TEXTURED_POLYGON = 0,
	RPU_PRIMITIVE_SPRITE = 1,
	RPU_PRIMITIVE_GOURAUD_POLYGON = 2
} rpu_primitive_kind_t;

#define GPU_RPU_SCANLINE_PENDING UINT8_C(0xfc)
#define GPU_RPU_SCANLINE_MERGED UINT8_C(0xfd)

static int rpu_scanline_merge_enabled(void)
{
	const char *experimental = getenv("XAVIX2_EXPERIMENTAL_SCANLINE_COMPOSITOR");
	/* The patented active-line merger is the normal XaviX2 path.  Retain an
	 * explicit zero only as a diagnostic A/B escape hatch. */
	return !experimental || strcmp(experimental, "0");
}

typedef struct rpu_primitive
{
	uint16_t depth;
	uint16_t first_y;
	uint16_t last_y;
	uint16_t sort_y;
	uint16_t index;
	uint8_t kind;
} rpu_primitive_t;

enum
{
	RPU_MAX_POLYGONS = XAVIX2_LOW_RAM_SIZE / 16,
	RPU_MAX_SPRITES = XAVIX2_LOW_RAM_SIZE / 8,
	RPU_MAX_PRIMITIVES = RPU_MAX_POLYGONS + RPU_MAX_SPRITES
};

/* GPU triggers are synchronous and one XaviX2 machine is rendered at a time.
 * Reuse fixed host workspaces instead of allocating five arrays for every
 * E408/E414 pair; Windows heap variance otherwise becomes visible as frame
 * pacing spikes even when the average renderer throughput is above 60 FPS. */
static rpu_primitive_t rpu_polygon_workspace[RPU_MAX_POLYGONS];
static rpu_primitive_t rpu_sprite_workspace[RPU_MAX_SPRITES];
static rpu_primitive_t rpu_merged_workspace[RPU_MAX_PRIMITIVES];
static rpu_primitive_t rpu_recycle_workspace[RPU_MAX_PRIMITIVES];
static rpu_primitive_t rpu_next_recycle_workspace[RPU_MAX_PRIMITIVES];

static uint16_t rpu_primitive_sort_depth(const rpu_primitive_t *primitive)
{
	/* Bandai battle lists use Polygon depth 000 as a terrain band between
	 * Sprite FF (distant backing) and ordinary Sprite 00..FE (foreground).
	 * The published generic structure does not document this zero code, but
	 * captures and the DBZ/Blue Dragon overlap fixtures consistently do. */
	if (primitive->kind != RPU_PRIMITIVE_SPRITE && !primitive->depth)
		return UINT16_C(0x0fef);
	return primitive->depth;
}

static void restore_rpu_tiled_overlay_painter_order(
	xavix2_machine_t *machine, rpu_primitive_t *sprites,
	size_t sprite_count, uint16_t list_count, uint16_t list_address,
	uint16_t descriptor_table)
{
	uint32_t run_count = 0;
	uint32_t run_descriptor = UINT32_MAX;
	uint32_t list_index;

	/* Dragon Ball assembles a fighter from a long run of small depth-zero
	 * tiles, then immediately submits a presentation object.  Attack spheres
	 * are usually tall, while GAME OVER / MISSION CLEAR are wide, shallow
	 * banners.  Both are firmware painter-order overlays: their record follows
	 * the tiled fighter and must cover it even when the overlay's Depth field
	 * is nonzero.  Give only this structurally identifiable following record
	 * the run's effective RPU depth; the stable list index then paints it last.
	 * This avoids a global "text on top" rule and leaves HUD/backing depth
	 * relationships unchanged. */
	for (list_index = 0; list_index < list_count; ++list_index)
	{
		uint64_t command = machine_read64(machine,
			(uint32_t)list_address + list_index * 8);
		uint32_t depth = (uint32_t)((command >> 21) & 0xff);
		uint32_t descriptor_index = (uint32_t)((command >> 30) & 0x3f);
		uint32_t descriptor = gpu_descriptor_size(machine, descriptor_table,
			descriptor_index);
		uint32_t width = 1 + (descriptor & 0xff);
		uint32_t height = 1 + ((descriptor >> 8) & 0xff);

		if (!depth && width <= 32 && height <= 32)
		{
			if (!run_count || descriptor_index != run_descriptor)
			{
				run_count = 1;
				run_descriptor = descriptor_index;
			}
			else
				run_count++;
			continue;
		}
		if (run_count >= 8 && depth && depth <= 0x0f &&
			descriptor == UINT32_C(0xf10021b5) &&
			width >= 128 && height >= 24 && height <= 48)
		{
			size_t primitive_index;
			for (primitive_index = 0; primitive_index < sprite_count;
				++primitive_index)
				if (sprites[primitive_index].index == list_index)
				{
					sprites[primitive_index].depth = 0;
					return;
				}
		}
		run_count = 0;
		run_descriptor = UINT32_MAX;
	}
}

static int compare_rpu_stream_primitive(const void *left, const void *right)
{
	const rpu_primitive_t *a = (const rpu_primitive_t *)left;
	const rpu_primitive_t *b = (const rpu_primitive_t *)right;
	uint16_t a_depth = rpu_primitive_sort_depth(a);
	uint16_t b_depth = rpu_primitive_sort_depth(b);
	/* US20090278845A1/WO2007043293A1 YSU rules 1 to 3: appearance
	 * (minimum Y) first, then descending Depth only at the same appearance Y.
	 * Records above the display top have already been clamped to LN in sort_y. */
	if (a->sort_y != b->sort_y)
		return a->sort_y < b->sort_y ? -1 : 1;
	if (a_depth != b_depth)
		return a_depth > b_depth ? -1 : 1;
	return a->index < b->index ? -1 : a->index > b->index ? 1 : 0;
}

static int rpu_stream_is_sorted(const rpu_primitive_t *stream, size_t count)
{
	size_t index;
	for (index = 1; index < count; ++index)
		if (compare_rpu_stream_primitive(stream + index - 1,
			stream + index) > 0)
			return 0;
	return 1;
}

static int rpu_polygon_precedes_sprite(const rpu_primitive_t *polygon,
	const rpu_primitive_t *sprite)
{
	uint16_t polygon_depth = rpu_primitive_sort_depth(polygon);
	uint16_t sprite_depth = rpu_primitive_sort_depth(sprite);
	if (polygon->sort_y != sprite->sort_y)
		return polygon->sort_y < sprite->sort_y;
	if (polygon_depth != sprite_depth)
		return polygon_depth > sprite_depth;
	/* The published merge rules do not define a further discriminator.  Select
	 * Polygon first on an exact cross-stream tie and keep both source streams
	 * stable; captures can refine this single remaining hardware tie later. */
	return 1;
}

static int rpu_depth_comparator_prefers_prefetch(
	const rpu_primitive_t *prefetch, const rpu_primitive_t *recycle)
{
	uint16_t prefetch_depth = rpu_primitive_sort_depth(prefetch);
	uint16_t recycle_depth = rpu_primitive_sort_depth(recycle);
	int prefetch_sprite = prefetch->kind == RPU_PRIMITIVE_SPRITE;
	int recycle_sprite = recycle->kind == RPU_PRIMITIVE_SPRITE;
	if (prefetch_depth != recycle_depth)
		return prefetch_depth > recycle_depth;
	/* The patent leaves equal Depth unspecified.  The command streams provide
	 * the observable stable tie: later equal-depth records are foreground even
	 * when their vertical spans cause one record to enter through prefetch and
	 * the other through recycle on a later line (DB2J's tiled fighter/effect).
	 * Within a stream, therefore, consume the lower firmware index first. */
	if (prefetch_sprite == recycle_sprite)
		return prefetch->index < recycle->index;
	/* Match the merge sorter's exact cross-stream tie. */
	return !prefetch_sprite;
}

static int render_rpu_scanline_merge(xavix2_machine_t *machine,
	uint16_t polygon_count, uint16_t polygon_address,
	uint16_t sprite_count, uint16_t sprite_address)
{
	size_t capacity = (size_t)polygon_count + sprite_count;
	rpu_primitive_t *polygons;
	rpu_primitive_t *sprites;
	rpu_primitive_t *merged;
	rpu_primitive_t *recycle;
	rpu_primitive_t *next_recycle;
	size_t valid_polygon_count = 0;
	size_t valid_sprite_count = 0;
	size_t merged_count = 0;
	uint16_t descsize_address = load16(machine->mmio + 0x608);
	uint32_t index;
	uint16_t minimum_render_y = 0;
	uint16_t maximum_render_y = 0x03ff;
	uint32_t visible_x = load16(machine->mmio + 0x656);
	uint32_t visible_y = load16(machine->mmio + 0x658);
	uint16_t display_mode = load16(machine->mmio + 0x650);
	uint32_t visible_width = machine->mmio[0x650] == 0x08 ? 640 : 320;
	uint32_t visible_height = display_mode == 0x0008 ||
		(display_mode == 0x1608 && visible_y != 0x0110) ? 480 : 240;
	uint32_t line;

	if (!capacity || polygon_count > RPU_MAX_POLYGONS ||
		sprite_count > RPU_MAX_SPRITES || capacity > RPU_MAX_PRIMITIVES)
		return 0;
	polygons = rpu_polygon_workspace;
	sprites = rpu_sprite_workspace;
	merged = rpu_merged_workspace;
	recycle = rpu_recycle_workspace;
	next_recycle = rpu_next_recycle_workspace;
	if ((visible_x || visible_y) &&
		visible_x + visible_width <= 0x800 &&
		visible_y + visible_height <= 0x400)
	{
		minimum_render_y = (uint16_t)visible_y;
		maximum_render_y = (uint16_t)(visible_y + visible_height - 1);
	}

	for (index = 0; index < polygon_count; ++index)
	{
		uint32_t record = (uint32_t)polygon_address + index * 16;
		uint32_t d0;
		uint32_t d1;
		uint32_t d3;
		uint32_t y0;
		uint32_t y1;
		uint32_t y2;
		uint32_t first_y;
		uint32_t last_y;
		if (record + 16 > XAVIX2_LOW_RAM_SIZE)
			break;
		d0 = load32(machine->low_ram + record);
		d1 = load32(machine->low_ram + record + 4);
		d3 = load32(machine->low_ram + record + 12);
		y0 = (d0 >> 1) & 0x3ff;
		y1 = (d0 >> 22) & 0x3ff;
		y2 = (d1 >> 11) & 0x3ff;
		first_y = y0 < y1 ? y0 : y1;
		if (y2 < first_y) first_y = y2;
		last_y = y0 > y1 ? y0 : y1;
		if (y2 > last_y) last_y = y2;
		polygons[valid_polygon_count].depth =
			(uint16_t)((d3 >> 15) & 0x0fff);
		polygons[valid_polygon_count].first_y = (uint16_t)first_y;
		polygons[valid_polygon_count].last_y = (uint16_t)last_y;
		polygons[valid_polygon_count].sort_y = (uint16_t)(
			first_y < minimum_render_y ? minimum_render_y : first_y);
		polygons[valid_polygon_count].index = (uint16_t)index;
		polygons[valid_polygon_count].kind = (uint8_t)((d0 & 1) ?
			RPU_PRIMITIVE_GOURAUD_POLYGON : RPU_PRIMITIVE_TEXTURED_POLYGON);
		valid_polygon_count++;
	}
	for (index = 0; index < sprite_count; ++index)
	{
		uint64_t command = machine_read64(machine,
			(uint32_t)sprite_address + index * 8);
		uint32_t descriptor_index = (uint32_t)((command >> 30) & 0x3f);
		uint32_t descriptor = gpu_descriptor_size(machine, descsize_address,
			descriptor_index);
		uint32_t source_width = 1 + (descriptor & 0xff);
		uint32_t source_height = 1 + ((descriptor >> 8) & 0xff);
		uint32_t scale_x = (uint32_t)((command >> 36) & 0x3f);
		uint32_t scale_y = (uint32_t)((command >> 42) & 0x3f);
		uint32_t output_width =
			(uint32_t)(((uint64_t)source_width * scale_x) >> 4);
		uint32_t output_height =
			(uint32_t)(((uint64_t)source_height * scale_y) >> 4);
		uint32_t first_y = (uint32_t)((command >> 11) & 0x3ff);
		uint64_t last_y;
		if (!scale_x || !scale_y || !output_width || !output_height ||
			first_y >= 0x400)
			continue;
		last_y = (uint64_t)first_y + output_height - 1;
		if (last_y >= 0x400)
			last_y = 0x03ff;
		sprites[valid_sprite_count].depth =
			(uint16_t)(((command >> 21) & 0xff) << 4);
		sprites[valid_sprite_count].first_y = (uint16_t)first_y;
		sprites[valid_sprite_count].last_y = (uint16_t)last_y;
		sprites[valid_sprite_count].sort_y = (uint16_t)(
			first_y < minimum_render_y ? minimum_render_y : first_y);
		sprites[valid_sprite_count].index = (uint16_t)index;
		sprites[valid_sprite_count].kind = RPU_PRIMITIVE_SPRITE;
		valid_sprite_count++;
	}
	restore_rpu_tiled_overlay_painter_order(machine, sprites,
		valid_sprite_count, sprite_count, sprite_address, descsize_address);
	if (!valid_polygon_count && !valid_sprite_count)
		return 0;

	/* Firmware commonly emits both source FIFOs in the patent's YSU order
	 * already.  Avoid paying for two general-purpose sorts on those frames;
	 * unsorted captures retain the exact existing qsort path. */
	if (!rpu_stream_is_sorted(polygons, valid_polygon_count))
		qsort(polygons, valid_polygon_count, sizeof(*polygons),
			compare_rpu_stream_primitive);
	if (!rpu_stream_is_sorted(sprites, valid_sprite_count))
		qsort(sprites, valid_sprite_count, sizeof(*sprites),
			compare_rpu_stream_primitive);
	{
		size_t polygon_position = 0;
		size_t sprite_position = 0;
		while (polygon_position < valid_polygon_count ||
			sprite_position < valid_sprite_count)
		{
			if (sprite_position >= valid_sprite_count ||
				(polygon_position < valid_polygon_count &&
				rpu_polygon_precedes_sprite(polygons + polygon_position,
					sprites + sprite_position)))
				merged[merged_count++] = polygons[polygon_position++];
			else
				merged[merged_count++] = sprites[sprite_position++];
		}
	}

	{
		size_t prefetch_position = 0;
		size_t recycle_count = 0;
		/* US20090278845A1/WO2007043293A1 blocks 108, 110 and 112:
		 * the prefetch FIFO supplies structures whose minimum Y is the current
		 * line, while the recycle FIFO supplies structures retained from the
		 * preceding line.  Both queues are already in descending Depth order.
		 * The depth comparator consumes the deeper queue head and the slicer
		 * writes a continuing structure into the next line's recycle FIFO. */
		line = minimum_render_y;
		while (line <= maximum_render_y)
		{
			size_t prefetch_end = prefetch_position;
			size_t current_prefetch;
			size_t current_recycle = 0;
			size_t next_recycle_count = 0;
			uint32_t span_end = maximum_render_y;
			int have_active = 0;
			/* Nothing is active before the next appearance line.  The hardware
			 * waits for VC to reach that line; skip the same empty interval. */
			if (!recycle_count && prefetch_position < merged_count &&
				merged[prefetch_position].sort_y > line)
			{
				line = merged[prefetch_position].sort_y;
				continue;
			}
			while (prefetch_end < merged_count &&
				merged[prefetch_end].sort_y == line)
				prefetch_end++;
			/* Between appearance and expiration events, both FIFO contents and
			 * their depth order are unchanged.  Render that whole row span in one
			 * call instead of rebuilding samplers once per horizontal line. */
			if (prefetch_end < merged_count &&
				merged[prefetch_end].sort_y > line &&
				(uint32_t)merged[prefetch_end].sort_y - 1 < span_end)
				span_end = (uint32_t)merged[prefetch_end].sort_y - 1;
			for (current_recycle = 0; current_recycle < recycle_count;
				++current_recycle)
			{
				if (recycle[current_recycle].last_y < line)
					continue;
				have_active = 1;
				if (recycle[current_recycle].last_y < span_end)
					span_end = recycle[current_recycle].last_y;
			}
			for (current_prefetch = prefetch_position;
				current_prefetch < prefetch_end; ++current_prefetch)
			{
				if (merged[current_prefetch].first_y > line ||
					merged[current_prefetch].last_y < line)
					continue;
				have_active = 1;
				if (merged[current_prefetch].last_y < span_end)
					span_end = merged[current_prefetch].last_y;
			}
			if (!have_active)
			{
				prefetch_position = prefetch_end;
				line++;
				continue;
			}
			current_prefetch = prefetch_position;
			current_recycle = 0;
			while (current_prefetch < prefetch_end ||
				current_recycle < recycle_count)
			{
				const rpu_primitive_t *item;
				if (current_recycle >= recycle_count)
					item = merged + current_prefetch++;
				else if (current_prefetch >= prefetch_end)
					item = recycle + current_recycle++;
				else
				{
					if (rpu_depth_comparator_prefers_prefetch(
						merged + current_prefetch,
						recycle + current_recycle))
						item = merged + current_prefetch++;
					else
						item = recycle + current_recycle++;
				}
				if (line >= item->first_y && line <= item->last_y)
				{
					if (item->kind == RPU_PRIMITIVE_SPRITE)
					{
						uint64_t command = machine_read64(machine,
							(uint32_t)sprite_address + item->index * 8);
						render_rpu_sprite_rows(machine, command,
							(uint16_t)line, (uint16_t)span_end);
					}
					else
						render_triangle_gpu_rows(machine, 1,
							(uint16_t)(polygon_address + item->index * 16),
							item->depth, item->depth,
							item->kind == RPU_PRIMITIVE_GOURAUD_POLYGON ? 1 : 0,
							(uint16_t)line, (uint16_t)span_end);
				}
				if (span_end < item->last_y)
					next_recycle[next_recycle_count++] = *item;
			}
			prefetch_position = prefetch_end;
			{
				rpu_primitive_t *temporary = recycle;
				recycle = next_recycle;
				next_recycle = temporary;
				recycle_count = next_recycle_count;
			}
			line = span_end + 1;
		}
	}
	return 1;
}

static void rpu_surface_bounds(xavix2_machine_t *machine, uint32_t *origin_x,
	uint32_t *origin_y, uint32_t *width, uint32_t *height)
{
	*origin_x = load16(machine->mmio + 0x656);
	*origin_y = load16(machine->mmio + 0x658);
	uint16_t display_mode = load16(machine->mmio + 0x650);
	*width = machine->mmio[0x650] == 0x08 ? 640 : 320;
	*height = display_mode == 0x0008 ||
		(display_mode == 0x1608 && *origin_y != 0x0110) ? 480 : 240;
	if (!(*origin_x || *origin_y) || *origin_x + *width > 0x800 ||
		*origin_y + *height > 0x400)
	{
		*origin_x = *origin_y = 0;
		*width = 0x800;
		*height = 0x400;
	}
}

static void copy_rpu_polygon_mask_region(uint8_t *destination,
	const uint8_t *source, uint32_t origin_x, uint32_t origin_y,
	uint32_t width, uint32_t height)
{
	uint32_t y;
	const uint32_t mask_stride = 0x800 / 8;
	/* All observed display windows are byte-aligned.  Keep a conservative full
	 * copy for a future odd window rather than complicating boundary bits. */
	if ((origin_x & 7) || (width & 7))
	{
		memcpy(destination, source,
			sizeof(rpu_surface_snapshot_polygon_mask));
		return;
	}
	for (y = 0; y < height; ++y)
	{
		size_t offset = (size_t)(origin_y + y) * mask_stride + origin_x / 8;
		memcpy(destination + offset, source + offset, width / 8);
	}
}

static void capture_rpu_visible_surface(xavix2_machine_t *machine)
{
	uint32_t origin_x;
	uint32_t origin_y;
	uint32_t width;
	uint32_t height;
	uint32_t y;

	rpu_surface_bounds(machine, &origin_x, &origin_y, &width, &height);
	for (y = 0; y < height; ++y)
		memcpy(rpu_surface_snapshot_pixels + (origin_y + y) * 0x800 + origin_x,
			machine->screen_data + (origin_y + y) * 0x800 + origin_x,
			width * sizeof(uint32_t));
	copy_rpu_polygon_mask_region(rpu_surface_snapshot_polygon_mask,
		high_resolution_3d_polygon_mask, origin_x, origin_y, width, height);
	rpu_surface_snapshot.owner = machine;
	rpu_surface_snapshot.frame_count = machine->frame_count;
	rpu_surface_snapshot.x = (uint16_t)origin_x;
	rpu_surface_snapshot.y = (uint16_t)origin_y;
	rpu_surface_snapshot.width = (uint16_t)width;
	rpu_surface_snapshot.height = (uint16_t)height;
	rpu_surface_snapshot.valid = 1;
	rpu_surface_snapshot.consumed = 0;
}

static int restore_rpu_visible_surface(xavix2_machine_t *machine)
{
	uint32_t y;
	if (!rpu_surface_snapshot.valid || rpu_surface_snapshot.owner != machine ||
		rpu_surface_snapshot.frame_count != machine->frame_count ||
		rpu_surface_snapshot.consumed)
		return 0;
	for (y = 0; y < rpu_surface_snapshot.height; ++y)
		memcpy(machine->screen_data +
			(rpu_surface_snapshot.y + y) * 0x800 + rpu_surface_snapshot.x,
			rpu_surface_snapshot_pixels +
			(rpu_surface_snapshot.y + y) * 0x800 + rpu_surface_snapshot.x,
			rpu_surface_snapshot.width * sizeof(uint32_t));
	copy_rpu_polygon_mask_region(high_resolution_3d_polygon_mask,
		rpu_surface_snapshot_polygon_mask, rpu_surface_snapshot.x,
		rpu_surface_snapshot.y, rpu_surface_snapshot.width,
		rpu_surface_snapshot.height);
	rpu_surface_snapshot.consumed = 1;
	return 1;
}

static void gpu_trigger(xavix2_machine_t *machine, uint32_t offset)
{
	uint16_t address = offset == 0x408 ? load16(machine->mmio + 0x400) :
		load16(machine->mmio + 0x40c);
	uint16_t count = offset == 0x408 ? load16(machine->mmio + 0x404) :
		load16(machine->mmio + 0x410);

	if (!machine->gpu_trigger_count)
		machine->first_gpu_pc = machine->cpu.pc;
	machine->gpu_trigger_count++;
	machine->last_gpu_pc = machine->cpu.pc;
	machine->last_gpu_register = UINT32_C(0xffffe000) + offset;
	machine->last_gpu_count = count;
	if (count > machine->maximum_gpu_count)
		machine->maximum_gpu_count = count;
	/* Host frame pacing may execute an emulation-only catch-up frame after a
	 * costly rendered frame.  The GPU remains instantaneously ready to the
	 * guest, while the last completed host surface is retained until the next
	 * normally rendered frame. */
	if (skip_render_active(machine))
	{
		machine->gpu_sprite_background_prepared = 0;
		if (rpu_surface_snapshot.owner == machine)
			rpu_surface_snapshot.valid = 0;
		return;
	}
	/* The RPU prefetchers latch one sorted polygon/sprite submission for the
	 * displayed frame.  Bandai's 120 Hz firmware can prepare and trigger the
	 * alternate command buffer again before the next 60 Hz vblank; painting
	 * that second complete pair into our software framebuffer composites two
	 * different palette epochs.  Reflected DB2J effects then expose the later
	 * buffer's normally transparent border as a brown or dark rectangle, and
	 * every such frame is rasterized twice.  Keep the first completed merged
	 * surface until vblank, just like the hardware line-buffer handoff.  The
	 * repeated E414 completion is already ignored below; also ignore its E408
	 * half so it cannot replace the completed-frame sentinel with PENDING. */
	if (offset == 0x408 && machine->gpu_sprite_background_prepared ==
		GPU_RPU_SCANLINE_MERGED)
		return;
	if (offset == 0x408)
	{
		uint16_t sprite_address = load16(machine->mmio + 0x40c);
		uint16_t sprite_count = load16(machine->mmio + 0x410);
		if (count && load16(machine->mmio + PROJECTOR_FAR_REGISTER) == 0x7fff)
		{
			uint16_t search_maximum = 0x0fff;
			uint16_t polygon_depth;
			/* EPOCH submits Gouraud sky/terrain, indexed Nalpha-graded water,
			 * and the final depth-zero transition through separate RPU classes.
			 * The RPU merger orders Type-0 and Type-1 primitives together by raw
			 * depth; drawing either complete class first exposes the horizon seams.
			 * Hold only the nearest Type-1 transition until E414 has supplied the
			 * character/cloud sprite stream. */
			while (triangle_next_depth(machine, count, address,
				search_maximum, &polygon_depth))
			{
				if (!polygon_depth)
					break;
				render_triangle_gpu(machine, count, address, polygon_depth,
					polygon_depth, -1);
				search_maximum = (uint16_t)(polygon_depth - 1);
			}
			/* Depth-zero indexed geometry belongs behind the deferred Gouraud
			 * transition, but is still part of this first GPU0 pass. */
			render_triangle_gpu(machine, count, address, 0, 0, 0);
			/* 0xfe is an in-frame host sentinel: E414 must draw every sprite and
			 * only then composite the deferred depth-zero transition. */
			machine->gpu_sprite_background_prepared = 0xfe;
			(void)sprite_address;
			(void)sprite_count;
			finish_polygon_coverage(machine);
			return;
		}
		if (count && sprite_count && rpu_scanline_merge_enabled())
		{
			capture_rpu_visible_surface(machine);
			/* E408 and E414 are the two halves of one RPU submission.  Presenting
			 * occurs after the pair, so defer the expensive intermediate raster and
			 * compose the captured base exactly once with E414's final palette. */
			machine->gpu_sprite_background_prepared =
				GPU_RPU_SCANLINE_PENDING;
			return;
		}
		/* During the startup logo the E408 submission precedes creation of the
		 * current sprite list.  Do not mark an empty/stale background as drawn;
		 * E414 must still be allowed to render its newly submitted depth-FF logo
		 * sprites. */
		if (count && triangle_minimum_depth(machine, count, address) != 0)
		{
			uint16_t search_maximum = 0x0fff;
			uint8_t sprite_maximum = 0xff;
			uint16_t polygon_depth;
			while (triangle_next_depth(machine, count, address, search_maximum,
				&polygon_depth))
			{
				uint32_t farther_sprite = ((uint32_t)polygon_depth >> 4) + 1;
				if (farther_sprite <= sprite_maximum && sprite_count &&
					!machine->gpu_sprite_background_prepared)
					render_gpu_depth_range(machine, sprite_count, sprite_address,
						(uint8_t)farther_sprite, sprite_maximum, 0);
				render_triangle_gpu(machine, count, address, polygon_depth,
					polygon_depth, -1);
				if (farther_sprite <= sprite_maximum)
					sprite_maximum = farther_sprite ?
						(uint8_t)(farther_sprite - 1) : 0;
				if (!polygon_depth)
					break;
				search_maximum = (uint16_t)(polygon_depth - 1);
			}
			if (sprite_count && !machine->gpu_sprite_background_prepared)
				machine->gpu_sprite_background_prepared =
					(uint8_t)(sprite_maximum + 1);
		}
		else
		{
			if (sprite_count && !machine->gpu_sprite_background_prepared)
			{
				render_gpu_depth_range(machine, sprite_count, sprite_address,
					0xff, 0xff, 0);
				machine->gpu_sprite_background_prepared = 0xff;
			}
			else if (!sprite_count)
				machine->gpu_sprite_background_prepared = 0;
			render_triangle_gpu(machine, count, address, 0, 0x0fff, -1);
		}
		if (count)
			finish_polygon_coverage(machine);
	}
	else
	{
		/* A repeated completion trigger must not blend translucent objects twice. */
		if (machine->gpu_sprite_background_prepared ==
			GPU_RPU_SCANLINE_MERGED)
			return;
		if (machine->gpu_sprite_background_prepared ==
			GPU_RPU_SCANLINE_PENDING)
		{
			uint16_t polygon_address = load16(machine->mmio + 0x400);
			uint16_t polygon_count = load16(machine->mmio + 0x404);
			if (restore_rpu_visible_surface(machine) &&
				render_rpu_scanline_merge(machine, polygon_count,
				polygon_address, count, address))
			{
				if (polygon_count)
					finish_polygon_coverage(machine);
				machine->gpu_sprite_background_prepared =
					GPU_RPU_SCANLINE_MERGED;
				return;
			}
			machine->gpu_sprite_background_prepared = 0;
		}
		if (load16(machine->mmio + PROJECTOR_FAR_REGISTER) == 0x7fff &&
			machine->gpu_sprite_background_prepared == 0xfe)
		{
			uint16_t polygon_address = load16(machine->mmio + 0x400);
			uint16_t polygon_count = load16(machine->mmio + 0x404);
			render_gpu_depth_range(machine, count, address, 0, 0xff, 0);
			render_triangle_gpu(machine, polygon_count, polygon_address, 0, 0, 1);
			if (polygon_count)
				finish_polygon_coverage(machine);
			machine->gpu_sprite_background_prepared = 0xff;
			return;
		}
		/* Some frames submit only the sprite stream.  Do not discard their
		 * depth-FF objects merely because GPU0 did not provide a point at which
		 * to prepaint the distant band. */
		if (!machine->gpu_sprite_background_prepared)
		{
			render_gpu_depth_range(machine, count, address, 0, 0xff, 0);
			machine->gpu_sprite_background_prepared = 0xff;
		}
		else if (machine->gpu_sprite_background_prepared > 0)
			render_gpu_depth_range(machine, count, address, 0,
				(uint8_t)(machine->gpu_sprite_background_prepared - 1), 1);
	}
}

static void machine_write8(void *opaque, uint32_t address, uint8_t data)
{
	xavix2_machine_t *machine = (xavix2_machine_t *)opaque;
	uint32_t offset;

	if (address < XAVIX2_LOW_RAM_SIZE)
	{
		machine->low_ram[address] = data;
		note_diagnostic_ram_access(machine, address, data, 1);
		if (address >= XAVIX2_MOTION_SAMPLE_FIRST &&
			address < XAVIX2_MOTION_SAMPLE_FIRST + XAVIX2_MOTION_SAMPLE_SIZE)
			note_motion_sample_access(machine, address, data, 1);
		if (address >= XAVIX2_IRQ_CONTEXT_FIRST &&
			address < XAVIX2_IRQ_CONTEXT_FIRST + XAVIX2_IRQ_CONTEXT_SIZE)
			note_irq_context_access(machine, address, data, 1);
		if (address >= XAVIX2_SENSOR_BUFFER_FIRST &&
			address < XAVIX2_SENSOR_BUFFER_FIRST + XAVIX2_SENSOR_BUFFER_SIZE)
			note_sensor_buffer_access(machine, address, data, 1);
		if (address >= XAVIX2_SENSOR_DECODED_FIRST &&
			address < XAVIX2_SENSOR_DECODED_FIRST + XAVIX2_SENSOR_DECODED_SIZE)
			note_sensor_decoded_access(machine, address, data, 1);
		if (address >= XAVIX2_MOTION_SOURCE_FIRST &&
			address < XAVIX2_MOTION_SOURCE_FIRST + XAVIX2_MOTION_SOURCE_SIZE)
			note_motion_source_access(machine, address, data, 1);
		if (address >= XAVIX2_ACTION_STATE_FIRST &&
			address < XAVIX2_ACTION_STATE_FIRST + XAVIX2_ACTION_STATE_SIZE)
			note_action_state_access(machine, address, data, 1);
		return;
	}
	if (address >= UINT32_C(0xc0000000) && address <= UINT32_C(0xc00007ff))
	{
		machine->palette_ram[address - UINT32_C(0xc0000000)] = data;
		return;
	}
	if (address >= UINT32_C(0xc0000800) && address <= UINT32_C(0xc001ffff))
	{
		machine->video_ram[address - UINT32_C(0xc0000800)] = data;
		return;
	}
	if (address >= UINT32_C(0xffffe000))
	{
		offset = address - UINT32_C(0xffffe000);
		if (offset < XAVIX2_MMIO_SIZE)
		{
			machine->mmio_write_counts[offset]++;
			machine->mmio_last_write_pc[offset] = machine->cpu.pc;
			note_audio_mmio_access(machine, offset, data, 1);
			if (offset == CONTROLLER_POWER_STATUS_REGISTER)
				machine->mmio[offset] = data &
					CONTROLLER_POWER_STATUS_COUNTER_MASK;
			else
				machine->mmio[offset] = data;
			if (offset == 0xa0b)
				xavix2_audio_command(&machine->audio,
					load16(machine->mmio + 0xa0a),
					machine->video_ram + AUDIO_DESCRIPTOR_RAM_OFFSET,
					load16(machine->mmio + 0xa18),
					load16(machine->mmio + 0xa1a), machine->mmio[0xa1c],
					machine->mmio[0xa1d]);
			if (offset == PROJECTOR_COMMAND_REGISTER)
				projector_start(machine, data);
			if (offset >= XAVIX2_CAPTURE_REGISTER_FIRST &&
				offset < XAVIX2_CAPTURE_REGISTER_FIRST + XAVIX2_CAPTURE_REGISTER_COUNT)
				note_capture_access(machine, offset, data, 1);
			if (offset == 0x00c)
				dma_start(machine, data);
			else if (offset == 0x010 && data == 2)
				clear_interrupts(machine, UINT32_C(1) << IRQ_DMA);
			else if ((offset >= 0x200 && offset <= 0x20b))
				update_pio(machine);
			else if (offset == 0x238)
			{
				if (data == '\n')
					machine->debug_length = 0;
				else if (data != '\r' && data && machine->debug_length + 1 < sizeof(machine->debug_text))
					machine->debug_text[machine->debug_length++] = data;
			}
			else if (offset == 0x408 || offset == 0x414)
				gpu_trigger(machine, offset);
			else if (offset == 0x1c04 || offset == 0x1c05)
			{
				unsigned shift = (offset - 0x1c04) * 8;
				machine->last_irq_clear_mask = (uint16_t)((machine->last_irq_clear_mask &
					~(UINT16_C(0xff) << shift)) | ((uint16_t)data << shift));
				machine->last_irq_clear_pc = machine->cpu.pc;
				machine->irq_clear_write_count++;
				clear_interrupts(machine,
					(uint32_t)data << ((offset - 0x1c04) * 8));
			}
			else if (offset == 0x1c08 || offset == 0x1c09)
			{
				unsigned shift = (offset - 0x1c08) * 8;
				machine->interrupt_nmi = (machine->interrupt_nmi & ~(UINT32_C(0xff) << shift)) |
					((uint32_t)data << shift);
			}
			else if (offset == 0x1c0a || offset == 0x1c0b)
			{
				unsigned shift = (offset - 0x1c0a) * 8;
				machine->interrupt_enabled = (machine->interrupt_enabled & ~(UINT32_C(0xff) << shift)) |
					((uint32_t)data << shift);
				clear_interrupts(machine,
					~(machine->interrupt_enabled | machine->interrupt_nmi));
			}
			return;
		}
	}

	note_unmapped_write(machine, address);
}

int xavix2_machine_init(xavix2_machine_t *machine, const uint8_t *rom,
	size_t rom_size)
{
	if (!machine || !rom || !rom_size)
		return 0;
	memset(machine, 0, sizeof(*machine));
	machine->rom = rom;
	machine->rom_size = rom_size;
	memset(machine->eeprom.data, 0xff, sizeof(machine->eeprom.data));
	xavix_eeprom24c08_init(&machine->eeprom, NULL, 0);
	xavix2_audio_init(&machine->audio, rom, rom_size);
	machine->motion_packet_address = XAVIX2_MOTION_PACKET_FIRST;
	xavix2_cpu_init(&machine->cpu, machine_read8, machine_write8, machine);
	xavix2_cpu_set_fetch(&machine->cpu, machine_fetch8);
	xavix2_cpu_set_interrupt_ack(&machine->cpu, acknowledge_interrupt, machine);
	machine->next_vblank_cycle = XAVIX2_CYCLES_PER_FRAME;
	machine->timer_rate_hz = 120;
	machine->next_timer_cycle = XAVIX2_TIMER_CYCLES;
	return 1;
}

void xavix2_machine_set_motion_packet_address(xavix2_machine_t *machine,
	uint16_t address)
{
	if (!machine || (uint32_t)address + XAVIX2_MOTION_PACKET_SIZE >
		XAVIX2_LOW_RAM_SIZE)
		return;
	machine->motion_packet_address = address;
}

void xavix2_machine_set_fixed_pio_input(xavix2_machine_t *machine,
	uint32_t input)
{
	if (!machine)
		return;
	machine->pio_input = (machine->pio_input & ~machine->pio_fixed_input) |
		input;
	machine->pio_fixed_input = input;
}

void xavix2_machine_set_timer_rate(xavix2_machine_t *machine,
	unsigned rate_hz)
{
	if (!machine || (rate_hz != 60 && rate_hz != 120))
		return;
	machine->timer_rate_hz = (uint8_t)rate_hz;
	machine->next_timer_cycle = machine->cpu.total_cycles +
		XAVIX2_CPU_CLOCK / rate_hz;
}

void xavix2_machine_update_takecopter_timer_rate(xavix2_machine_t *machine)
{
	if (!machine)
		return;
	/* Display width is independent of the firmware timer.  Treating the
	 * 640-wide menu as 120 Hz submits both GPU lists twice per video frame and
	 * makes the carousel and audio miss real time on ordinary hosts. */
	if (machine->timer_rate_hz != 60)
		xavix2_machine_set_timer_rate(machine, 60);
}

void xavix2_machine_set_high_resolution_3d(xavix2_machine_t *machine,
	int enabled)
{
	if (!machine)
		return;
	high_resolution_3d.owner = machine;
	high_resolution_3d.enabled = enabled != 0;
	high_resolution_3d.presented_scale = 1;
	memset(high_resolution_3d_polygon_mask, 0,
		sizeof(high_resolution_3d_polygon_mask));
}

void xavix2_machine_set_skip_render(xavix2_machine_t *machine, int enabled)
{
	skip_render_owner = machine;
	skip_render_enabled = machine && enabled;
}

unsigned xavix2_machine_frame_scale(const xavix2_machine_t *machine)
{
	return high_resolution_3d_active(machine) ?
		high_resolution_3d.presented_scale : 1U;
}

void xavix2_machine_reset(xavix2_machine_t *machine)
{
	const uint8_t *rom;
	size_t rom_size;
	uint16_t motion_packet_address;
	uint32_t pio_fixed_input;
	uint8_t timer_rate_hz;
	uint8_t eeprom[XAVIX_EEPROM24C08_SIZE];
	if (!machine)
		return;
	rom = machine->rom;
	rom_size = machine->rom_size;
	motion_packet_address = machine->motion_packet_address;
	pio_fixed_input = machine->pio_fixed_input;
	timer_rate_hz = machine->timer_rate_hz;
	memcpy(eeprom, machine->eeprom.data, sizeof(eeprom));
	(void)xavix2_machine_init(machine, rom, rom_size);
	xavix2_machine_set_motion_packet_address(machine, motion_packet_address);
	xavix2_machine_set_fixed_pio_input(machine, pio_fixed_input);
	xavix2_machine_set_timer_rate(machine, timer_rate_hz ? timer_rate_hz : 120);
	(void)xavix_eeprom24c08_load_image(&machine->eeprom, eeprom, sizeof(eeprom));
	if (high_resolution_3d.owner == machine)
		memset(high_resolution_3d_polygon_mask, 0,
			sizeof(high_resolution_3d_polygon_mask));
	if (skip_render_owner == machine)
		skip_render_enabled = 0;
}

static uint64_t next_event_cycle(const xavix2_machine_t *machine)
{
	uint64_t next = machine->next_vblank_cycle;
	if (machine->next_timer_cycle < next)
		next = machine->next_timer_cycle;
	if (machine->dma_completion_cycle && machine->dma_completion_cycle < next)
		next = machine->dma_completion_cycle;
	return next;
}

static void process_events(xavix2_machine_t *machine)
{
	uint64_t timer_cycles = XAVIX2_CPU_CLOCK /
		(machine->timer_rate_hz == 60 ? 60 : 120);
	while (machine->cpu.total_cycles >= machine->next_timer_cycle)
	{
		raise_interrupt(machine, IRQ_TIMER);
		machine->next_timer_cycle += timer_cycles;
	}
	while (machine->cpu.total_cycles >= machine->next_vblank_cycle)
	{
		uint16_t background = load16(machine->mmio + 0x60e);
		uint32_t color = rgb555(background);
		uint32_t pixel;
		if (!skip_render_active(machine))
		{
			for (pixel = 0; pixel < 0x400 * 0x800; ++pixel)
				machine->screen_data[pixel] = color;
			if (high_resolution_3d_active(machine))
				memset(high_resolution_3d_polygon_mask, 0,
					sizeof(high_resolution_3d_polygon_mask));
			memset(rpu_sprite_coverage_mask, 0,
				sizeof(rpu_sprite_coverage_mask));
		}
		if (rpu_surface_snapshot.owner == machine)
			rpu_surface_snapshot.valid = 0;
		machine->gpu_sprite_background_prepared = 0;
		machine->frame_count++;
		if (machine->experimental_direct_pio_sample)
		{
			int changed = sample_experimental_pio(machine);
			if (machine->experimental_dispatch_input)
				machine->experimental_callback_pending |= changed;
		}
		machine->next_vblank_cycle += XAVIX2_CYCLES_PER_FRAME;
	}
	if (machine->dma_completion_cycle &&
		machine->cpu.total_cycles >= machine->dma_completion_cycle)
	{
		machine->dma_completion_cycle = 0;
		raise_interrupt(machine, IRQ_DMA);
	}
}

uint64_t xavix2_machine_execute(xavix2_machine_t *machine,
	uint64_t cycle_budget)
{
	uint64_t start;
	uint64_t target;
	if (!machine || !cycle_budget)
		return 0;
	start = machine->cpu.total_cycles;
	target = start + cycle_budget;
	while (machine->cpu.total_cycles < target)
	{
		uint64_t event_cycle;
		uint64_t remaining;
		uint32_t slice;
		uint32_t used;

		process_events(machine);
		if (machine->cpu.waiting && !machine->cpu.interrupt_line)
		{
			if (machine->experimental_dispatch_input &&
				machine->experimental_callback_pending)
			{
				machine->experimental_callback_pending = 0;
				machine->cpu.r[7] = machine->cpu.pc;
				machine->cpu.pc = machine->experimental_callback_address ?
					machine->experimental_callback_address : UINT32_C(0x00020017);
				machine->cpu.waiting = 0;
				continue;
			}
			event_cycle = next_event_cycle(machine);
			machine->cpu.total_cycles = event_cycle < target ? event_cycle : target;
			process_events(machine);
			if (machine->cpu.total_cycles == target && !machine->cpu.interrupt_line)
				break;
		}

		remaining = target - machine->cpu.total_cycles;
		event_cycle = next_event_cycle(machine);
		if (event_cycle > machine->cpu.total_cycles &&
			event_cycle - machine->cpu.total_cycles < remaining)
			remaining = event_cycle - machine->cpu.total_cycles;
		slice = remaining > 4096 ? 4096 : (uint32_t)remaining;
		if (!slice)
		{
			process_events(machine);
			continue;
		}
		used = xavix2_cpu_execute(&machine->cpu, slice);
		if (!used && !machine->cpu.waiting)
			break;
	}
	process_events(machine);
	return machine->cpu.total_cycles - start;
}

uint64_t xavix2_machine_run_video_frame(xavix2_machine_t *machine,
	const uint8_t motion_packet[XAVIX2_MOTION_PACKET_SIZE],
	uint32_t pio_input)
{
	uint64_t start;
	uint64_t target;
	const uint64_t render_margin = 64;

	if (!machine)
		return 0;
	start = machine->cpu.total_cycles;
	if (motion_packet)
		memcpy(machine->low_ram + machine->motion_packet_address,
			motion_packet, XAVIX2_MOTION_PACKET_SIZE);
	machine->pio_input = machine->pio_fixed_input | pio_input;
	/* PIO is sampled by the guest through its hardware registers.  The old
	 * experimental direct sampler mirrors edges into 0x0da4..0x0db3, but that
	 * range is ordinary game state in Take-copter (including its star-hit and
	 * scene-transition flags).  Do not enable that diagnostic path during
	 * normal video frames; probes can still opt into it explicitly. */

	/* The two wrist reflectors are latched by the level-10 handler.  Pulse the
	 * line only at the firmware's wait point, matching the observed hardware
	 * cadence and avoiding interference with another active interrupt. */
	if (motion_packet && machine->cpu.waiting && !machine->interrupt_active)
	{
		uint64_t reads_before = machine->irq_level_read_count;
		unsigned steps;
		xavix2_machine_raise_irq(machine, IRQ_MOTION);
		if (machine->interrupt_active)
		{
			for (steps = 0; steps < 256 &&
				machine->irq_level_read_count == reads_before; ++steps)
				(void)xavix2_cpu_execute(&machine->cpu, 1);
			xavix2_machine_clear_irq(machine, IRQ_MOTION);
		}
	}
	/* A save may land after the short IRQ-10 service crosses a vblank
	 * boundary. Process that already-due event before subtracting the next
	 * event cycle; otherwise the unsigned difference becomes a huge budget
	 * and the first frame after F7 fast-forwards the guest. */
	process_events(machine);

	/* Stop just before vertical blank clears the command-list framebuffer.
	 * The next call crosses that boundary and renders the following frame. */
	if (machine->cpu.total_cycles < machine->next_vblank_cycle &&
		machine->cpu.total_cycles + render_margin >=
		machine->next_vblank_cycle)
	{
		uint64_t remaining = machine->next_vblank_cycle -
			machine->cpu.total_cycles;
		(void)xavix2_machine_execute(machine, remaining + 1);
	}
	target = machine->next_vblank_cycle > render_margin ?
		machine->next_vblank_cycle - render_margin :
		machine->next_vblank_cycle;
	if (machine->cpu.total_cycles < target)
		(void)xavix2_machine_execute(machine,
			target - machine->cpu.total_cycles);
	xavix2_audio_render(&machine->audio,
		xavix2_audio_engine_rate(machine->mmio[0xa00], machine->mmio[0xa05]));
	return machine->cpu.total_cycles - start;
}

static uint32_t high_resolution_3d_bilinear(uint32_t color00,
	uint32_t color10, uint32_t color01, uint32_t color11,
	uint32_t weight_x0, uint32_t weight_y0)
{
	uint32_t weight_x1 = 4 - weight_x0;
	uint32_t weight_y1 = 4 - weight_y0;
	uint32_t weight00 = weight_x0 * weight_y0;
	uint32_t weight10 = weight_x1 * weight_y0;
	uint32_t weight01 = weight_x0 * weight_y1;
	uint32_t weight11 = weight_x1 * weight_y1;
	uint32_t red = (weight00 * ((color00 >> 16) & 0xff) +
		weight10 * ((color10 >> 16) & 0xff) +
		weight01 * ((color01 >> 16) & 0xff) +
		weight11 * ((color11 >> 16) & 0xff) + 8) >> 4;
	uint32_t green = (weight00 * ((color00 >> 8) & 0xff) +
		weight10 * ((color10 >> 8) & 0xff) +
		weight01 * ((color01 >> 8) & 0xff) +
		weight11 * ((color11 >> 8) & 0xff) + 8) >> 4;
	uint32_t blue = (weight00 * (color00 & 0xff) +
		weight10 * (color10 & 0xff) +
		weight01 * (color01 & 0xff) +
		weight11 * (color11 & 0xff) + 8) >> 4;
	return UINT32_C(0xff000000) | (red << 16) | (green << 8) | blue;
}

static const uint32_t *high_resolution_3d_upscale(
	const xavix2_machine_t *machine, const uint32_t *source,
	uint32_t native_width, uint32_t native_height, uint32_t source_stride,
	uint32_t origin_x, uint32_t origin_y, int line_doubled,
	unsigned *width, unsigned *height, unsigned *stride)
{
	uint32_t output_width = native_width * 2;
	uint32_t output_height = native_height * 2;
	uint32_t output_y;
	int visible_polygon = 0;
	(void)machine;
	/* The native 640x480 surface is the XaviX/SSD startup and menu path.  It is
	 * already the board's full output resolution; the optional 3D enhancement
	 * is intended for the lower-resolution polygon playfield, not for doubling
	 * a 2D BIOS logo to 1280x960.  Take-copter's 640x240 logical 3D scene enters
	 * here after line doubling and remains eligible for polygon enhancement. */
	if (!line_doubled && native_width == 640 && native_height == 480)
	{
		high_resolution_3d.presented_scale = 1;
		if (width) *width = native_width;
		if (height) *height = native_height;
		if (stride) *stride = source_stride;
		return source;
	}
	/* Convert the sparse internal 2048x1024 bit plane to the compact visible
	 * window once.  The output loop below can then use byte neighbours instead
	 * of repeatedly recalculating and probing packed-bit addresses. */
	for (output_y = 0; output_y < native_height; ++output_y)
	{
		uint32_t internal_y = origin_y +
			(line_doubled ? output_y / 2 : output_y);
		uint32_t x;
		uint8_t *mask_row = high_resolution_3d_visible_mask +
			output_y * native_width;
		for (x = 0; x < native_width; ++x)
		{
			mask_row[x] = (uint8_t)high_resolution_3d_is_polygon(
				origin_x + x, internal_y);
			visible_polygon |= mask_row[x];
		}
	}
	/* BIOS logos and other pure-2D 640x480 screens have no polygon pixels.
	 * Expanding those frames to 1280x960 cannot improve their artwork, but the
	 * unnecessary 1.2-million-pixel pass creates deadline spikes which become
	 * audible as startup crackle.  Keep them native and engage 2x only when the
	 * composed visible frame really contains 3D geometry. */
	if (!visible_polygon)
	{
		high_resolution_3d.presented_scale = 1;
		if (width) *width = native_width;
		if (height) *height = native_height;
		if (stride) *stride = source_stride;
		return source;
	}
	high_resolution_3d.presented_scale = 2;

	for (output_y = 0; output_y < output_height; ++output_y)
	{
		uint32_t nearest_y = output_y >> 1;
		uint32_t y0 = (output_y & 1) || !nearest_y ?
			nearest_y : nearest_y - 1;
		uint32_t y1 = (output_y & 1) && nearest_y + 1 < native_height ?
			nearest_y + 1 : nearest_y;
		uint32_t weight_y0 = (output_y & 1) ? 3 : 1;
		uint32_t output_x;
		for (output_x = 0; output_x < output_width; ++output_x)
		{
			uint32_t nearest_x = output_x >> 1;
			uint32_t x0 = (output_x & 1) || !nearest_x ?
				nearest_x : nearest_x - 1;
			uint32_t x1 = (output_x & 1) && nearest_x + 1 < native_width ?
				nearest_x + 1 : nearest_x;
			int footprint_has_polygon =
				high_resolution_3d_visible_mask[y0 * native_width + x0] ||
				high_resolution_3d_visible_mask[y0 * native_width + x1] ||
				high_resolution_3d_visible_mask[y1 * native_width + x0] ||
				high_resolution_3d_visible_mask[y1 * native_width + x1];
			uint32_t nearest = source[nearest_y * source_stride + nearest_x];
			/* The real video encoder receives the already-composited line-buffer
			 * RGB, not a polygon/sprite class bit.  Keep wholly non-polygon regions
			 * pixel-sharp, but filter both sides of a mixed footprint.  Selecting by
			 * only the nearest pixel made the polygon side bilinear and the adjacent
			 * sprite side nearest-neighbour, exposing an artificial join. */
			if (!footprint_has_polygon)
			{
				high_resolution_3d_frame[output_y * output_width + output_x] =
					nearest | UINT32_C(0xff000000);
				continue;
			}
			{
				uint32_t weight_x0 = (output_x & 1) ? 3 : 1;
				high_resolution_3d_frame[output_y * output_width + output_x] =
					high_resolution_3d_bilinear(
						source[y0 * source_stride + x0],
						source[y0 * source_stride + x1],
						source[y1 * source_stride + x0],
						source[y1 * source_stride + x1],
						weight_x0, weight_y0);
			}
		}
	}
	if (width) *width = output_width;
	if (height) *height = output_height;
	if (stride) *stride = output_width;
	return high_resolution_3d_frame;
}
const uint32_t *xavix2_machine_visible_frame(const xavix2_machine_t *machine,
	unsigned *width, unsigned *height, unsigned *stride)
{
	const uint32_t *source;
	uint16_t mode;
	uint32_t origin_x;
	uint32_t origin_y;
	uint32_t visible_width;
	uint32_t visible_height;
	uint32_t source_height;
	int line_doubled;
	unsigned y;

	if (!machine)
		return NULL;
	/* Display mode 08 exposes the 640x480 startup surface.  Its GPU lists
	 * deliberately cover that full area (for example DB2J emits two adjacent
	 * 320x480 sprites).  Cropping a fixed 320x240 window here showed exactly
	 * the upper-left quarter of every XaviX2 startup logo. */
	mode = load16(machine->mmio + 0x650);
	origin_x = load16(machine->mmio + 0x656);
	origin_y = load16(machine->mmio + 0x658);
	line_doubled = mode == 0x1608 && origin_y == 0x0110;
	if ((mode & 0x00ff) == 0x0008 && !line_doubled)
	{
		visible_width = 640;
		visible_height = 480;
		source_height = 480;
	}
	else if (line_doubled)
	{
		/* EPOCH's Take-copter switches origin Y from 0120 to 0110 when 1608
		 * changes from the native 640x480 XaviX logo surface to its 640x240
		 * logical scene surface. Treating the latter as a native 640x480 frame
		 * placed the complete image in the upper half; doubling the former
		 * stretched and clipped the startup logo. */
		visible_width = 640;
		visible_height = 480;
		source_height = 240;
	}
	else
	{
		visible_width = 320;
		visible_height = 240;
		source_height = 240;
	}
	if (width) *width = visible_width;
	if (height) *height = visible_height;
	/* E656/E658 are the guest-selected visible origin.  Title firmware uses
	 * 0x360/0x188, while the Dragon Ball gameplay setup deliberately shifts
	 * X left to 0x310.  Retain the observed title origin until the guest has
	 * programmed a complete, in-range pair. */
	if ((!origin_x && !origin_y) || origin_x + visible_width > 0x800 ||
		origin_y + source_height > 0x400)
	{
		origin_x = 0x400 - visible_width / 2;
		origin_y = 0x200 - source_height / 2;
	}
	source = machine->screen_data + origin_y * 0x800 + origin_x;
	if (!line_doubled)
	{
		if (high_resolution_3d_active(machine))
			return high_resolution_3d_upscale(machine, source,
				visible_width, visible_height, 0x800, origin_x, origin_y, 0,
				width, height, stride);
		if (stride) *stride = 0x800;
		return source;
	}
	for (y = 0; y < source_height; ++y)
	{
		memcpy(line_doubled_frame + (y * 2) * visible_width,
			source + y * 0x800, visible_width * sizeof(*source));
		memcpy(line_doubled_frame + (y * 2 + 1) * visible_width,
			source + y * 0x800, visible_width * sizeof(*source));
	}
	if (high_resolution_3d_active(machine))
		return high_resolution_3d_upscale(machine, line_doubled_frame,
			visible_width, visible_height, visible_width, origin_x, origin_y, 1,
			width, height, stride);
	if (stride) *stride = visible_width;
	return line_doubled_frame;
}

const int16_t *xavix2_machine_frame_audio(const xavix2_machine_t *machine)
{
	return machine ? xavix2_audio_frame(&machine->audio) : NULL;
}

size_t xavix2_machine_state_size(void)
{
	return XAVIX2_STATE_HEADER_SIZE + sizeof(xavix2_machine_t);
}

int xavix2_machine_state_save(const xavix2_machine_t *machine,
	void *output, size_t output_capacity, size_t *output_size)
{
	uint8_t *bytes = (uint8_t *)output;
	const size_t required = xavix2_machine_state_size();

	if (output_size)
		*output_size = required;
	if (!machine || !bytes || output_capacity < required ||
		sizeof(xavix2_machine_t) > UINT32_MAX)
		return 0;
	memcpy(bytes, XAVIX2_STATE_MAGIC, sizeof(XAVIX2_STATE_MAGIC));
	store32(bytes + 8, XAVIX2_STATE_VERSION);
	store32(bytes + 12, (uint32_t)sizeof(xavix2_machine_t));
	memcpy(bytes + XAVIX2_STATE_HEADER_SIZE, machine, sizeof(*machine));
	return 1;
}

int xavix2_machine_state_load(xavix2_machine_t *machine,
	const void *input, size_t input_size)
{
	const uint8_t *bytes = (const uint8_t *)input;
	const size_t legacy_size = offsetof(xavix2_machine_t, next_timer_cycle);
	uint32_t version;
	uint32_t payload_size;
	int legacy;
	const uint8_t *rom;
	size_t rom_size;
	xavix2_read8_fn read8;
	xavix2_read8_fn fetch8;
	xavix2_write8_fn write8;
	void *opaque;
	xavix2_interrupt_ack_fn interrupt_ack;
	void *interrupt_ack_opaque;
	xavix2_trace_fn trace;
	void *trace_opaque;
	uint64_t audio_mute_mask;
	uint8_t timer_rate_hz;
	unsigned channel;

	if (!machine || !bytes || input_size < XAVIX2_STATE_HEADER_SIZE ||
		memcmp(bytes, XAVIX2_STATE_MAGIC, sizeof(XAVIX2_STATE_MAGIC)))
		return 0;
	version = load32(bytes + 8);
	payload_size = load32(bytes + 12);
	legacy = version == XAVIX2_STATE_LEGACY_VERSION &&
		payload_size == legacy_size;
	if ((!legacy && (version != XAVIX2_STATE_VERSION ||
		payload_size != sizeof(xavix2_machine_t))) ||
		input_size != XAVIX2_STATE_HEADER_SIZE + (size_t)payload_size)
		return 0;
	if ((uint32_t)load16(bytes + XAVIX2_STATE_HEADER_SIZE +
		offsetof(xavix2_machine_t, motion_packet_address)) +
		XAVIX2_MOTION_PACKET_SIZE > XAVIX2_LOW_RAM_SIZE)
		return 0;

	/* Files capture guest hardware only.  Keep the current ROM and host bus
	 * callbacks so a state never restores stale process addresses. */
	rom = machine->rom;
	rom_size = machine->rom_size;
	read8 = machine->cpu.read8;
	fetch8 = machine->cpu.fetch8;
	write8 = machine->cpu.write8;
	opaque = machine->cpu.opaque;
	interrupt_ack = machine->cpu.interrupt_ack;
	interrupt_ack_opaque = machine->cpu.interrupt_ack_opaque;
	trace = machine->cpu.trace;
	trace_opaque = machine->cpu.trace_opaque;
	audio_mute_mask = 0;
	timer_rate_hz = machine->timer_rate_hz ? machine->timer_rate_hz : 120;
	for (channel = 0; channel < XAVIX2_AUDIO_VOICES; ++channel)
		if (machine->audio.voice[channel].host_muted)
			audio_mute_mask |= UINT64_C(1) << channel;
	if (legacy)
	{
		memcpy(machine, bytes + XAVIX2_STATE_HEADER_SIZE, legacy_size);
		/* Version 1 tied IRQ 7 to vblank.  Resume at the next half-frame
		 * boundary so an existing F5 file immediately acquires the independent
		 * 120 Hz timer without losing the saved video phase. */
		if (machine->next_vblank_cycle >= XAVIX2_TIMER_CYCLES &&
			machine->cpu.total_cycles <
			machine->next_vblank_cycle - XAVIX2_TIMER_CYCLES)
			machine->next_timer_cycle =
				machine->next_vblank_cycle - XAVIX2_TIMER_CYCLES;
		else
			machine->next_timer_cycle = machine->next_vblank_cycle;
	}
	else
		memcpy(machine, bytes + XAVIX2_STATE_HEADER_SIZE, sizeof(*machine));
	machine->timer_rate_hz = timer_rate_hz;
	if (timer_rate_hz == 60)
		machine->next_timer_cycle = machine->next_vblank_cycle;
	machine->rom = rom;
	machine->rom_size = rom_size;
	machine->audio.rom = rom;
	machine->audio.rom_size = rom_size;
	xavix2_audio_restore_descriptors(&machine->audio,
		machine->video_ram + AUDIO_DESCRIPTOR_RAM_OFFSET);
	xavix2_audio_set_mute_mask(&machine->audio, audio_mute_mask);
	machine->cpu.read8 = read8;
	machine->cpu.fetch8 = fetch8;
	machine->cpu.write8 = write8;
	machine->cpu.opaque = opaque;
	machine->cpu.interrupt_ack = interrupt_ack;
	machine->cpu.interrupt_ack_opaque = interrupt_ack_opaque;
	machine->cpu.trace = trace;
	machine->cpu.trace_opaque = trace_opaque;
	/* Experimental probe controls are host state, not guest hardware.  Older
	 * saves may contain the direct PIO sampler enabled; restoring it would write
	 * synthetic edge bits over ordinary game RAM on the next vblank. */
	machine->experimental_direct_pio_sample = 0;
	machine->experimental_dispatch_input = 0;
	machine->experimental_callback_pending = 0;
	machine->experimental_callback_address = 0;
	machine->experimental_capture_readback = 0;
	machine->experimental_capture_a = 0;
	machine->experimental_capture_b = 0;
	machine->experimental_sampled_pio = 0;
	/* The background preparation flag is an in-flight compositor cache rather
	 * than durable guest state. */
	machine->gpu_sprite_background_prepared = 0;
	if (rpu_surface_snapshot.owner == machine)
		rpu_surface_snapshot.valid = 0;
	memset(rpu_sprite_coverage_mask, 0, sizeof(rpu_sprite_coverage_mask));
	if (high_resolution_3d.owner == machine)
		memset(high_resolution_3d_polygon_mask, 0,
			sizeof(high_resolution_3d_polygon_mask));
	if (skip_render_owner == machine)
		skip_render_enabled = 0;
	return 1;
}
