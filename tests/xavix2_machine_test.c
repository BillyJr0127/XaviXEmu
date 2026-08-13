// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "xavix2_machine.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			fprintf(stderr, "check failed at %s:%d: %s\n", \
				__FILE__, __LINE__, #condition); \
			free(machine); \
			free(rom); \
			return 1; \
		} \
	} while (0)

static void store16(uint8_t *destination, uint16_t value)
{
	destination[0] = (uint8_t)value;
	destination[1] = (uint8_t)(value >> 8);
}

static uint16_t load16(const uint8_t *source)
{
	return (uint16_t)(source[0] | ((uint16_t)source[1] << 8));
}

static void store_address(uint8_t *descriptor, unsigned high_offset,
	unsigned low_offset, uint32_t address)
{
	store16(descriptor + high_offset, (uint16_t)(address >> 16));
	store16(descriptor + low_offset, (uint16_t)address);
}

static void store32(uint8_t *destination, uint32_t value)
{
	destination[0] = (uint8_t)value;
	destination[1] = (uint8_t)(value >> 8);
	destination[2] = (uint8_t)(value >> 16);
	destination[3] = (uint8_t)(value >> 24);
}

static uint32_t load32(const uint8_t *source)
{
	return (uint32_t)source[0] | ((uint32_t)source[1] << 8) |
		((uint32_t)source[2] << 16) | ((uint32_t)source[3] << 24);
}

static void store64(uint8_t *destination, uint64_t value)
{
	unsigned index;
	for (index = 0; index < 8; ++index)
		destination[index] = (uint8_t)(value >> (index * 8));
}

static uint64_t gpu_command(unsigned scale_x, unsigned scale_y)
{
	return ((uint64_t)(scale_x & 0x3f) << 36) |
		((uint64_t)(scale_y & 0x3f) << 42);
}

static void store_triangle_record(uint8_t *record, int type,
	unsigned ax, unsigned ay, unsigned bx, unsigned by,
	unsigned cx, unsigned cy, uint32_t d2, uint32_t d3)
{
	store32(record, (type ? 1u : 0u) |
		((ay & 0x3ff) << 1) | ((ax & 0x7ff) << 11) |
		((by & 0x3ff) << 22));
	store32(record + 4, (bx & 0x7ff) | ((cy & 0x3ff) << 11) |
		((cx & 0x7ff) << 21));
	store32(record + 8, d2);
	store32(record + 12, d3);
}

static uint32_t projector_input(int value)
{
	return (uint32_t)((int64_t)value * 65536);
}

static uint32_t geometry_q16_16(int numerator, int denominator)
{
	return (uint32_t)(((int64_t)numerator << 16) / denominator);
}

static uint32_t geometry_q8_24(int numerator, int denominator)
{
	return (uint32_t)(((int64_t)numerator << 24) / denominator);
}

static uint32_t packed_geometry_vertex(int x, int y, int z)
{
	return ((uint32_t)x & 0x3ff) | (((uint32_t)y & 0x3ff) << 10) |
		(((uint32_t)z & 0x3ff) << 20);
}

int main(void)
{
	uint8_t *rom = (uint8_t *)calloc(1, UINT32_C(0x10000));
	xavix2_machine_t *machine = (xavix2_machine_t *)calloc(1, sizeof(*machine));
	uint8_t descriptors[XAVIX2_AUDIO_DESCRIPTOR_BYTES] = { 0 };
	const int16_t *audio_frame;
	const uint32_t private_ram_rates[] = { 0, 257 };
	const uint8_t engine_divider_b[] = { 0x0d, 0x0f };
	unsigned rate_index;
	CHECK(rom && machine);
	CHECK(XAVIX2_CPU_CLOCK == XAVIX2_AUDIO_MASTER_CLOCK);
	CHECK(XAVIX2_TIMER_CYCLES * 2 == XAVIX2_CYCLES_PER_FRAME);
	CHECK(xavix2_machine_init(machine, rom, UINT32_C(0x10000)));
	/* IRQ 7 is an independent 120 Hz timer, not the 60 Hz vblank event.
	 * Reaching the half-frame boundary must raise it without clearing or
	 * advancing the framebuffer. */
	machine->interrupt_enabled = UINT32_C(1) << 7;
	machine->cpu.waiting = 1;
	CHECK(xavix2_machine_execute(machine, XAVIX2_TIMER_CYCLES) ==
		XAVIX2_TIMER_CYCLES);
	CHECK(machine->frame_count == 0);
	CHECK(machine->next_timer_cycle == 2 * XAVIX2_TIMER_CYCLES);
	CHECK(machine->interrupt_active & (UINT32_C(1) << 7));
	xavix2_machine_reset(machine);
	/* Board inputs default low.  A ROM profile explicitly enables the
	 * receiver-present input only on hardware where firmware requires it. */
	CHECK((machine->cpu.read8(machine->cpu.opaque,
		UINT32_C(0xffffe20a)) & 0x80) == 0);

	/* Instruction fetch uses the low program image, not low data RAM. */
	machine->program_ram[0] = 0xfc;
	machine->low_ram[0] = 0xff;
	machine->cpu.pc = 0;
	CHECK(xavix2_cpu_execute(&machine->cpu, 1) == 1);
	CHECK(machine->cpu.pc == 1);
	CHECK(machine->cpu.unimplemented_count == 0);

	/* EC48 combines a firmware-owned two-bit debounce counter with a
	 * read-only controller/power-good input.  Reset starts with an empty
	 * counter and the normal status line asserted. */
	machine->program_ram[4] = 0x12;
	machine->program_ram[5] = 0x08;
	machine->program_ram[6] = 0x00;
	machine->program_ram[7] = 0x00;
	machine->cpu.r[1] = UINT32_C(0xffffec48);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->cpu.r[0] == 0x04);

	/* The second GPU channel and its projector are independent hardware,
	 * not another eight-byte sprite list.  Blue Dragon feeds signed Q16.16
	 * depth/vertical/horizontal triples and consumes doubled projected Y/X. */
	CHECK(machine->cpu.read8(machine->cpu.opaque, UINT32_C(0xffffe60a)) == 0x40);
	CHECK(machine->cpu.read8(machine->cpu.opaque, UINT32_C(0xffffe60b)) == 0x03);
	store16(machine->mmio + 0x840, 0x0080);
	store16(machine->mmio + 0x860, 0x1008);
	store16(machine->mmio + 0x862, 0x1000);
	machine->mmio[0x85a] = 0;
	store32(machine->low_ram + 0x1008, projector_input(1760));
	store32(machine->low_ram + 0x100c, projector_input(768));
	store32(machine->low_ram + 0x1010, projector_input(-10147));
	machine->cpu.write8(machine->cpu.opaque, UINT32_C(0xffffe858), 2);
	CHECK((int16_t)(machine->low_ram[0x1004] |
		((uint16_t)machine->low_ram[0x1005] << 8)) == 111);
	CHECK((int16_t)(machine->low_ram[0x1006] |
		((uint16_t)machine->low_ram[0x1007] << 8)) == -1475);

	/* Command 11 returns floor(sqrt(E800.l)) through E804.w.  Naruto calls
	 * this path for cursor/target distance tests during gameplay. */
	{
		static const uint32_t input[] =
		{
			0, 1, 2, 9, UINT32_C(0xffffffff)
		};
		static const uint16_t expected[] = { 0, 1, 1, 3, 65535 };
		unsigned value;
		for (value = 0; value < sizeof(input) / sizeof(input[0]); ++value)
		{
			store32(machine->mmio + 0x800, input[value]);
			store16(machine->mmio + 0x804, UINT16_C(0xa55a));
			machine->cpu.write8(machine->cpu.opaque,
				UINT32_C(0xffffe858), 0x11);
			CHECK(load16(machine->mmio + 0x804) == expected[value]);
		}
	}

	/* Command 10 uses a Q8.24 left basis with a Q16.16 right affine matrix;
	 * the composed matrix remains Q16.16. Translation comes from the left
	 * matrix plus its basis applied to the right one. */
	{
		static const int left[12] = {
			-1, 0, 0, 3,
			0, -1, 0, 4,
			0, 0, 1, 5
		};
		static const int right[12] = {
			1, 0, 0, 7,
			0, 1, 0, 8,
			0, 0, 1, 9
		};
		static const int expected[12] = {
			-1, 0, 0, -4,
			0, -1, 0, -4,
			0, 0, 1, 14
		};
		unsigned element;
		for (element = 0; element < 12; ++element)
		{
			store32(machine->mmio + 0x800 + element * 4,
				element % 4 == 3 ? geometry_q16_16(left[element], 1) :
				geometry_q8_24(left[element], 1));
			store32(machine->low_ram + 0x1100 + element * 4,
				geometry_q16_16(right[element], 1));
		}
		store16(machine->mmio + 0x860, 0x1100);
		store16(machine->mmio + 0x862, 0x1140);
		machine->cpu.write8(machine->cpu.opaque,
			UINT32_C(0xffffe858), 0x10);
		for (element = 0; element < 12; ++element)
			CHECK((int32_t)load32(machine->low_ram + 0x1140 +
				element * 4) == (int32_t)geometry_q16_16(expected[element], 1));
	}

	/* Command 0C consumes the complete retained 32-bit coefficient registers
	 * plus Q16.16 translations. This is DBZ's first battle transform and
	 * expected hardware output, not a synthetic matrix. */
	{
		uint32_t packed = packed_geometry_vertex(412, 3, -496);
		memset(machine->mmio + 0x800, 0, 0x30);
		store32(machine->mmio + 0x808, UINT32_C(0xffff0000));
		store32(machine->mmio + 0x814, UINT32_C(0xffff0000));
		store32(machine->mmio + 0x820, UINT32_C(0xffff0000));
		store32(machine->low_ram + 0x1180, packed);
		store16(machine->mmio + 0x860, 0x1180);
		store16(machine->mmio + 0x862, 0x1190);
		store16(machine->mmio + 0x85a, 0);
		machine->cpu.write8(machine->cpu.opaque,
			UINT32_C(0xffffe858), 0x0c);
		CHECK((int32_t)load32(machine->low_ram + 0x1190) ==
			(int32_t)UINT32_C(0x01f00000));
		CHECK((int32_t)load32(machine->low_ram + 0x1194) ==
			(int32_t)UINT32_C(0xfffd0000));
		CHECK((int32_t)load32(machine->low_ram + 0x1198) ==
			(int32_t)UINT32_C(0xfe640000));
		/* Translation occupies the fourth full Q16.16 dword in each row. */
		store32(machine->mmio + 0x80c, UINT32_C(0x00018000));
		machine->cpu.write8(machine->cpu.opaque,
			UINT32_C(0xffffe858), 0x0c);
		CHECK((int32_t)load32(machine->low_ram + 0x1190) ==
			(int32_t)UINT32_C(0x01f18000));
	}

	/* Command 0F uses the firmware's contiguous signed Q8.8 3x3 normal
	 * matrix.  It expands packed signed XYZ10 normals to signed XYZ32 without
	 * applying the translation dwords that command 0C consumes. */
	{
		uint32_t packed = packed_geometry_vertex(256, -128, 64);
		memset(machine->mmio + 0x800, 0, 0x24);
		/* [ Y, 0.5X, -Z ], in signed Q8.8. */
		store32(machine->mmio + 0x804, UINT32_C(0x00000100));
		store32(machine->mmio + 0x80c, UINT32_C(0x00000080));
		store32(machine->mmio + 0x820, UINT32_C(0xffffff00));
		store32(machine->low_ram + 0x11a0, packed);
		store16(machine->mmio + 0x860, 0x11a0);
		store16(machine->mmio + 0x862, 0x11b0);
		store16(machine->mmio + 0x85a, 0);
		machine->cpu.write8(machine->cpu.opaque,
			UINT32_C(0xffffe858), 0x0f);
		CHECK((int32_t)load32(machine->low_ram + 0x11b0) == -128);
		CHECK((int32_t)load32(machine->low_ram + 0x11b4) == 128);
		CHECK((int32_t)load32(machine->low_ram + 0x11b8) == -64);
	}

	/* Command 4D replaces indexed Q16.16 vertices with packed screen
	 * coordinates, removes back faces, compacts the list, and reports the
	 * actual polygon count through E85C. */
	{
		uint8_t *vertices = machine->low_ram + 0x1200;
		uint8_t *polygons = machine->low_ram + 0x1300;
		uint32_t expected0;
		uint32_t expected1;
		/* Command 4D consumes conventional X, Y, Z triples. */
		store32(vertices + 0, projector_input(-64));
		store32(vertices + 4, projector_input(-64));
		store32(vertices + 8, projector_input(256));
		store32(vertices + 12, projector_input(64));
		store32(vertices + 16, projector_input(-64));
		store32(vertices + 20, projector_input(256));
		store32(vertices + 24, projector_input(0));
		store32(vertices + 28, projector_input(64));
		store32(vertices + 32, projector_input(256));
		/* A fourth vertex lies in front of the configured near plane.  The
		 * otherwise front-facing triangle using it must not expand across the
		 * viewport. */
		store32(vertices + 36, projector_input(0));
		store32(vertices + 40, projector_input(64));
		store32(vertices + 44, projector_input(64));
		/* These vertices form a front face before packing, but B crosses the
		 * 11-bit X boundary.  Without viewport clipping its wrapped coordinate
		 * reverses the winding and would cover most of the screen. */
		store32(vertices + 48, projector_input(1952));
		store32(vertices + 52, projector_input(-224));
		store32(vertices + 56, projector_input(256));
		store32(vertices + 60, projector_input(2352));
		store32(vertices + 64, projector_input(-224));
		store32(vertices + 68, projector_input(256));
		store32(vertices + 72, projector_input(1952));
		store32(vertices + 76, projector_input(176));
		store32(vertices + 80, projector_input(256));
		/* Front: A=0,B=1,C=2. Back: A=0,B=2,C=1. */
		store32(polygons + 0, UINT32_C(0x00000001));
		store32(polygons + 4, UINT32_C(0x00020001));
		store32(polygons + 8, UINT32_C(0x12345678));
		store32(polygons + 12, UINT32_C(0x9abcdef0));
		store32(polygons + 16, UINT32_C(0x00000001));
		store32(polygons + 20, UINT32_C(0x00010002));
		store32(polygons + 24, UINT32_C(0x87654321));
		store32(polygons + 28, UINT32_C(0x0fedcba9));
		/* Near-plane crossing: A=0,B=1,C=3. */
		store32(polygons + 32, UINT32_C(0x00000001));
		store32(polygons + 36, UINT32_C(0x00030001));
		store32(polygons + 40, UINT32_C(0x13579bdf));
		store32(polygons + 44, UINT32_C(0x2468ace0));
		/* Wrapped-front triangle: A=4,B=5,C=6. */
		store32(polygons + 48, UINT32_C(0x00040001));
		store32(polygons + 52, UINT32_C(0x00060005));
		store32(polygons + 56, UINT32_C(0x55aa55aa));
		store32(polygons + 60, UINT32_C(0xaa55aa55));
		store16(machine->mmio + 0x840, 0x0080);
		store16(machine->mmio + 0x846, 0x0050);
		store16(machine->mmio + 0x848, 0x0800);
		store16(machine->mmio + 0x84a, 0x0400);
		store16(machine->mmio + 0x85a, 3);
		store16(machine->mmio + 0x860, 0x1200);
		store16(machine->mmio + 0x864, 0x1300);
		machine->cpu.write8(machine->cpu.opaque,
			UINT32_C(0xffffe858), 0x4d);
		CHECK((machine->mmio[0x85c] |
			((uint16_t)machine->mmio[0x85d] << 8)) == 1);
		expected0 = 1 | (480u << 1) | (992u << 11) | (480u << 22);
		expected1 = 1056u | (544u << 11) | (1024u << 21);
		CHECK(load32(polygons) == expected0);
		CHECK(load32(polygons + 4) == expected1);
		CHECK(load32(polygons + 8) == UINT32_C(0x12345678));
		CHECK(load32(polygons + 12) == UINT32_C(0x9abcdef0));
	}

	/* Different XaviX 2 firmware revisions place the IRQ-10 producer packet
	 * at different low-RAM addresses.  The board profile selects the address;
	 * the frame API must not silently keep writing Naruto's 0x000d buffer. */
	{
		const uint8_t packet[XAVIX2_MOTION_PACKET_SIZE] =
			{ 1, 2, 3, 4, 5, 6, 7 };
		xavix2_machine_set_motion_packet_address(machine, 0x014d);
		xavix2_machine_set_motion_packet_address(machine, 0xffff);
		CHECK(machine->motion_packet_address == 0x014d);
		CHECK((machine->cpu.read8(machine->cpu.opaque,
			UINT32_C(0xffffe20a)) & 0x80) == 0);
		xavix2_machine_set_fixed_pio_input(machine, UINT32_C(1) << 23);
		machine->cpu.waiting = 1;
		machine->interrupt_enabled = 0;
		(void)xavix2_machine_run_video_frame(machine, packet, 0);
		CHECK((machine->cpu.read8(machine->cpu.opaque,
			UINT32_C(0xffffe20a)) & 0x80) != 0);
		CHECK(memcmp(machine->low_ram + 0x014d, packet, sizeof(packet)) == 0);
		CHECK(memcmp(machine->low_ram + XAVIX2_MOTION_PACKET_FIRST,
			packet, sizeof(packet)) != 0);
		machine->cpu.waiting = 0;
	}
	CHECK(machine->mmio[0xc48] == 0x00);

	/* Writes retain only the firmware counter; reads restore status bit 2. */
	machine->program_ram[4] = 0x1a;
	machine->cpu.r[0] = 0xff;
	machine->cpu.r[1] = UINT32_C(0xffffec48);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->mmio[0xc48] == 0x03);
	machine->program_ram[4] = 0x12;
	machine->cpu.r[1] = UINT32_C(0xffffec48);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->cpu.r[0] == 0x07);
	machine->program_ram[4] = 0x1a;
	machine->cpu.r[0] = 0x00;
	machine->cpu.r[1] = UINT32_C(0xffffec48);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->mmio[0xc48] == 0x00);
	machine->program_ram[4] = 0x12;
	machine->cpu.r[1] = UINT32_C(0xffffec48);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->cpu.r[0] == 0x04);

	xavix2_machine_reset(machine);
	CHECK(machine->motion_packet_address == 0x014d);
	CHECK(machine->pio_fixed_input == (UINT32_C(1) << 23));
	CHECK((machine->cpu.read8(machine->cpu.opaque,
		UINT32_C(0xffffe20a)) & 0x80) != 0);
	CHECK(machine->mmio[0xc48] == 0x00);
	machine->program_ram[4] = 0x12;
	machine->program_ram[5] = 0x08;
	machine->program_ram[6] = 0x00;
	machine->program_ram[7] = 0x00;
	machine->cpu.r[1] = UINT32_C(0xffffec48);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->cpu.r[0] == 0x04);

	/* GPU W/H are Q2.4 scale factors.  Blue Dragon uses 0x11 to join
	 * adjacent 16-pixel menu tiles on 17-pixel boundaries; truncating the
	 * fractional nibble leaves visible seams.  Exercise magnification and
	 * reduction through the real command-list trigger. */
	store16(machine->mmio + 0x608, 0x0200);
	store16(machine->mmio + 0x622, 0x0300);
	store16(machine->mmio + 0x40c, 0x0500);
	store16(machine->mmio + 0x410, 1);
	store32(machine->low_ram + 0x0200, UINT32_C(0x0000000f));
	store16(machine->low_ram + 0x0300, 1);
	memset(machine->low_ram + 0x4000, 0xff, 8);
	store16(machine->palette_ram + 4, UINT16_C(0x001f));
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	store64(machine->low_ram + 0x0500, gpu_command(0x11, 0x10));
	machine->program_ram[4] = 0x1a;
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] != 0);
	CHECK(machine->screen_data[16] != 0);
	CHECK(machine->screen_data[17] == 0);

	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	store64(machine->low_ram + 0x0500, gpu_command(0x0a, 0x10));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[9] != 0);
	CHECK(machine->screen_data[10] == 0);

	/* Palette bit 15, not RGB value zero, marks transparency.  Opaque black
	 * sprites must cover an existing pixel just like polygon texels do. */
	store16(machine->palette_ram + 4, 0);
	machine->screen_data[0] = UINT32_C(0xffffffff);
	store64(machine->low_ram + 0x0500, gpu_command(0x10, 0x10));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xff000000));

	/* The 16-byte GPU channel also carries Type-1 Gouraud triangles.  RGB555
	 * vertex colors are interpolated independently and Nalpha stores inverse
	 * alpha (zero is opaque). */
	store16(machine->mmio + 0x400, 0x0600);
	store16(machine->mmio + 0x404, 1);
	store_triangle_record(machine->low_ram + 0x0600, 1,
		0, 0, 4, 0, 0, 4,
		UINT32_C(0x001f001f), UINT32_C(0x0000801f));
	machine->screen_data[1 * 0x800 + 1] = UINT32_C(0xff0000ff);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xffff0000));

	store_triangle_record(machine->low_ram + 0x0600, 1,
		0, 0, 4, 0, 0, 4,
		UINT32_C(0x001f001f), UINT32_C(0x8000801f));
	machine->screen_data[1 * 0x800 + 1] = UINT32_C(0xff0000ff);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xff7f007f));

	/* A transparent flag is separate from RGB555 value zero.  An opaque
	 * black texel must therefore overwrite an existing framebuffer pixel. */
	store16(machine->mmio + 0x608, 0x0200);
	store16(machine->mmio + 0x622, 0x0300);
	store32(machine->low_ram + 0x0200, 0);
	store16(machine->low_ram + 0x0300, 0);
	rom[0] = 0;
	store16(machine->palette_ram, 0);
	store_triangle_record(machine->low_ram + 0x0600, 0,
		0, 0, 4, 0, 0, 4, UINT32_C(0x00000040),
		UINT32_C(0x00022000));
	machine->screen_data[1 * 0x800 + 1] = UINT32_C(0xffffffff);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xff000000));

	/* The RPU merges the independently submitted streams by depth.  A
	 * distant depth-FF sky sprite is drawn before a zero-depth polygon, while
	 * a depth-09 HUD sprite remains in front. */
	store16(machine->mmio + 0x400, 0x0600);
	store16(machine->mmio + 0x404, 1);
	store16(machine->mmio + 0x40c, 0x0700);
	store16(machine->mmio + 0x410, 2);
	store_triangle_record(machine->low_ram + 0x0600, 1,
		0, 0, 4, 0, 0, 4,
		UINT32_C(0x001f001f), UINT32_C(0x0000001f));
	/* Two one-pixel sprites at the same location: blue sky at depth FF and
	 * green HUD at depth 09. */
	store32(machine->low_ram + 0x0200, 0);
	store16(machine->low_ram + 0x0300, 0);
	rom[0] = 0;
	store16(machine->palette_ram, UINT16_C(0x7c00));
	store64(machine->low_ram + 0x0700,
		gpu_command(0x10, 0x10) | (UINT64_C(0xff) << 21));
	store64(machine->low_ram + 0x0708,
		gpu_command(0x10, 0x10) | (UINT64_C(0x09) << 21));
	machine->gpu_sprite_background_prepared = 0;
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xffff0000));
	store16(machine->palette_ram, UINT16_C(0x03e0));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xff00ff00));

	/* A sprite-only frame has no E408 opportunity to prepaint depth FF.
	 * E414 must supply that band once, while repeated submissions in the same
	 * frame must not paint it again. */
	store16(machine->mmio + 0x410, 1);
	store16(machine->palette_ram, UINT16_C(0x7c00));
	machine->gpu_sprite_background_prepared = 0;
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	{
		uint64_t pixel_writes = machine->gpu_pixel_write_count;
		machine->cpu.r[0] = 0;
		machine->cpu.r[1] = UINT32_C(0xffffe414);
		machine->cpu.pc = 4;
		CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
		CHECK(machine->screen_data[0] == UINT32_C(0xff0000ff));
		CHECK(machine->gpu_sprite_background_prepared);
		CHECK(machine->gpu_pixel_write_count == pixel_writes + 1);
		machine->cpu.r[0] = 0;
		machine->cpu.r[1] = UINT32_C(0xffffe414);
		machine->cpu.pc = 4;
		CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
		CHECK(machine->gpu_pixel_write_count == pixel_writes + 1);
	}
	/* Repeated GPU0 submissions likewise must not put the distant band back
	 * over triangles already produced earlier in the frame. */
	store16(machine->mmio + 0x404, 0);
	machine->gpu_sprite_background_prepared = 0;
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	{
		uint64_t pixel_writes = machine->gpu_pixel_write_count;
		machine->cpu.r[0] = 0;
		machine->cpu.r[1] = UINT32_C(0xffffe408);
		machine->cpu.pc = 4;
		CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
		CHECK(machine->screen_data[0] == UINT32_C(0xff0000ff));
		CHECK(machine->gpu_pixel_write_count == pixel_writes + 1);
		machine->cpu.r[0] = 0;
		machine->cpu.r[1] = UINT32_C(0xffffe408);
		machine->cpu.pc = 4;
		CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
		CHECK(machine->gpu_pixel_write_count == pixel_writes + 1);
	}
	/* The preparation state belongs to one emulated video frame. */
	{
		uint64_t cycles_to_vblank = machine->next_vblank_cycle -
			machine->cpu.total_cycles;
		machine->cpu.waiting = 1;
		machine->cpu.interrupt_line = 0;
		machine->interrupt_enabled = 0;
		machine->interrupt_nmi = 0;
		CHECK(xavix2_machine_execute(machine, cycles_to_vblank) ==
			cycles_to_vblank);
		CHECK(!machine->gpu_sprite_background_prepared);
		machine->cpu.waiting = 0;
	}

	/* The guest changes the visible origin between title and gameplay. */
	{
		const uint32_t *visible;
		unsigned visible_width;
		unsigned visible_height;
		unsigned visible_stride;
		store16(machine->mmio + 0x656, 0x0310);
		store16(machine->mmio + 0x658, 0x0188);
		visible = xavix2_machine_visible_frame(machine, &visible_width,
			&visible_height, &visible_stride);
		CHECK(visible == machine->screen_data + 0x0188 * 0x800 + 0x0310);
		CHECK(visible_width == 320);
		CHECK(visible_height == 240);
		CHECK(visible_stride == 0x800);
		store16(machine->mmio + 0x656, 0x07ff);
		store16(machine->mmio + 0x658, 0x03ff);
		visible = xavix2_machine_visible_frame(machine, NULL, NULL, NULL);
		CHECK(visible == machine->screen_data +
			(0x200 - 120) * 0x800 + (0x400 - 160));
	}

	/* Low-address DMA seeds both views; later CPU stores change only data. */
	rom[0x20] = 0xa5;
	rom[0x21] = 0x5a;
	machine->mmio[0x000] = 0x20;
	machine->mmio[0x004] = 0x00;
	machine->mmio[0x005] = 0x01;
	machine->mmio[0x008] = 0x02;
	machine->program_ram[4] = 0x1a;
	machine->program_ram[5] = 0x08;
	machine->program_ram[6] = 0x00;
	machine->program_ram[7] = 0x00;
	machine->cpu.r[0] = 7;
	machine->cpu.r[1] = UINT32_C(0xffffe00c);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->dma_transfer_count == 1);
	CHECK(machine->program_ram[0x100] == 0xa5);
	CHECK(machine->program_ram[0x101] == 0x5a);
	CHECK(machine->low_ram[0x100] == 0xa5);
	CHECK(machine->low_ram[0x101] == 0x5a);

	machine->cpu.r[0] = 0x33;
	machine->cpu.r[1] = 0x100;
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->low_ram[0x100] == 0x33);
	CHECK(machine->program_ram[0x100] == 0xa5);

	/* The accepted source remains visible as status, but is not redelivered. */
	machine->interrupt_enabled = (UINT32_C(1) << 7) | (UINT32_C(1) << 12);
	machine->cpu.pc = UINT32_C(0x40000020);
	machine->cpu.hr[4] = 16;
	xavix2_machine_raise_irq(machine, 7);
	CHECK(machine->interrupt_active == (UINT32_C(1) << 7));
	CHECK(machine->interrupt_pending == (UINT32_C(1) << 7));
	CHECK(xavix2_cpu_execute(&machine->cpu, 1) == 4);
	CHECK(machine->cpu.interrupt_count == 1);
	CHECK(machine->interrupt_active == (UINT32_C(1) << 7));
	CHECK(machine->interrupt_pending == 0);
	CHECK(!machine->cpu.interrupt_line);
	CHECK(machine->interrupt_latched_valid);
	CHECK(machine->interrupt_latched_level == 7);

	/* A new source is still deliverable while the first source awaits clear. */
	machine->cpu.hr[4] |= 16;
	xavix2_machine_raise_irq(machine, 12);
	CHECK(machine->interrupt_pending == (UINT32_C(1) << 12));
	CHECK(xavix2_cpu_execute(&machine->cpu, 1) == 4);
	CHECK(machine->cpu.interrupt_count == 2);
	CHECK(machine->interrupt_active ==
		((UINT32_C(1) << 7) | (UINT32_C(1) << 12)));
	CHECK(machine->interrupt_pending == 0);
	CHECK(machine->interrupt_latched_valid);
	CHECK(machine->interrupt_latched_level == 12);

	xavix2_machine_clear_irq(machine, 7);
	CHECK(machine->interrupt_latched_valid);
	xavix2_machine_clear_irq(machine, 12);
	CHECK(machine->interrupt_active == 0);
	CHECK(machine->interrupt_pending == 0);
	CHECK(!machine->interrupt_latched_valid);

	/* Audio engine timing belongs to the SoC model, not a title's private
	 * low-RAM layout.  Some firmware stores its derived rate at 0x0158 while
	 * other titles leave 0x0150 zero or use it for unrelated state. */
	memset(rom + 0x8000, 64, 0x1000);
	rom[0x9000] = 0x80;
	store_address(descriptors, 0x02, 0x06, 0x8000);
	store16(descriptors + 0x16, 0x1000);
	descriptors[0x32] = 0x40;
	descriptors[0x33] = 0x40;
	machine->interrupt_enabled = 0;
	machine->interrupt_nmi = 0;
	for (rate_index = 0; rate_index < 2; ++rate_index)
	{
		const uint32_t private_rate = private_ram_rates[rate_index];
		const uint32_t engine_rate = xavix2_audio_engine_rate(0x20,
			engine_divider_b[rate_index]);
		const uint64_t step =
			((UINT64_C(0x1000) * engine_rate) << 16) /
			XAVIX2_AUDIO_OUTPUT_RATE;
		const uint64_t expected_position = (UINT64_C(0x8000) << 32) +
			step * XAVIX2_AUDIO_FRAMES_PER_VIDEO_FRAME;
		xavix2_audio_init(&machine->audio, rom, UINT32_C(0x10000));
		machine->low_ram[0x150] = (uint8_t)private_rate;
		machine->low_ram[0x151] = (uint8_t)(private_rate >> 8);
		machine->low_ram[0x152] = (uint8_t)(private_rate >> 16);
		machine->low_ram[0x153] = (uint8_t)(private_rate >> 24);
		machine->mmio[0xa00] = 0x20;
		machine->mmio[0xa05] = engine_divider_b[rate_index];
		machine->cpu.waiting = 1;
		machine->cpu.interrupt_line = 0;
		xavix2_audio_command(&machine->audio, 0x40, descriptors, 0, 0, 0);
		CHECK(xavix2_machine_run_video_frame(machine, NULL, 0) != 0);
		audio_frame = xavix2_machine_frame_audio(machine);
		CHECK(audio_frame != NULL);
		CHECK(audio_frame[0] == 64 * 64 / 2);
		CHECK(audio_frame[1] == 64 * 64 / 2);
		CHECK(machine->audio.voice[0].position == expected_position);
	}

	/* Save states preserve the complete guest machine while rebinding ROM and
	 * bus callbacks to the current process on load. */
	{
		const size_t state_size = xavix2_machine_state_size();
		uint8_t *state = (uint8_t *)malloc(state_size);
		size_t written = 0;
		const xavix2_read8_fn read8 = machine->cpu.read8;
		const xavix2_write8_fn write8 = machine->cpu.write8;
		CHECK(state != NULL);
		machine->low_ram[0x4321] = 0x9a;
		machine->cpu.pc = UINT32_C(0x40012345);
		machine->audio.voice[3].position = UINT64_C(0x123456789a);
		CHECK(xavix2_machine_state_save(machine, state, state_size, &written));
		CHECK(written == state_size);
		memset(state + 16 + offsetof(xavix2_machine_t, rom), 0,
			sizeof(machine->rom));
		memset(state + 16 + offsetof(xavix2_machine_t, cpu) +
			offsetof(xavix2_cpu_t, read8), 0, sizeof(machine->cpu.read8));
		machine->low_ram[0x4321] = 0;
		machine->cpu.pc = 0;
		machine->audio.voice[3].position = 0;
		xavix2_audio_set_mute_mask(&machine->audio, UINT64_C(1) << 3);
		CHECK(xavix2_machine_state_load(machine, state, state_size));
		CHECK(machine->low_ram[0x4321] == 0x9a);
		CHECK(machine->cpu.pc == UINT32_C(0x40012345));
		CHECK(machine->audio.voice[3].position == UINT64_C(0x123456789a));
		CHECK(machine->audio.voice[3].host_muted == 1);
		CHECK(machine->rom == rom);
		CHECK(machine->audio.rom == rom);
		CHECK(machine->cpu.read8 == read8);
		CHECK(machine->cpu.write8 == write8);
		CHECK(machine->cpu.opaque == machine);
		state[8] ^= 1;
		CHECK(!xavix2_machine_state_load(machine, state, state_size));
		state[8] ^= 1;
		CHECK(!xavix2_machine_state_load(machine, state, state_size - 1));
		/* Version-1 files ended immediately before next_timer_cycle.  They
		 * remain loadable and acquire the next 120 Hz phase from their saved
		 * vblank position. */
		store32(state + 8, 1);
		store32(state + 12,
			(uint32_t)offsetof(xavix2_machine_t, next_timer_cycle));
		store64(state + 16 + offsetof(xavix2_machine_t, cpu) +
			offsetof(xavix2_cpu_t, total_cycles), 1000);
		store64(state + 16 + offsetof(xavix2_machine_t, next_vblank_cycle),
			XAVIX2_CYCLES_PER_FRAME);
		CHECK(xavix2_machine_state_load(machine, state,
			16 + offsetof(xavix2_machine_t, next_timer_cycle)));
		CHECK(machine->next_timer_cycle == XAVIX2_TIMER_CYCLES);
		free(state);
	}

	/* A state captured just after an IRQ handler crossed vblank must resume
	 * one normal frame ahead, not underflow into a near-UINT64_MAX run. */
	xavix2_machine_reset(machine);
	machine->cpu.waiting = 1;
	machine->cpu.total_cycles = machine->next_vblank_cycle + 32;
	{
		const uint64_t before_cycles = machine->cpu.total_cycles;
		const uint64_t before_frames = machine->frame_count;
		const uint64_t ran = xavix2_machine_run_video_frame(machine, NULL, 0);
		CHECK(ran < XAVIX2_CYCLES_PER_FRAME * 2U);
		CHECK(machine->frame_count == before_frames + 1);
		CHECK(machine->cpu.total_cycles > before_cycles);
	}

	free(machine);
	free(rom);
	puts("xavix2 machine tests passed");
	return 0;
}
