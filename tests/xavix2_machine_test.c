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

static uint32_t packed_geometry_vertex(int x, int y, int z)
{
	return ((uint32_t)x & 0x3ff) | (((uint32_t)y & 0x3ff) << 10) |
		(((uint32_t)z & 0x3ff) << 20);
}
static uint32_t packed_vector10(int x, int y, int z, unsigned scale)
{
	return (((uint32_t)x & 0x3ff) << 22) |
		(((uint32_t)y & 0x3ff) << 12) |
		(((uint32_t)z & 0x3ff) << 2) | (scale & 3);
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
	/* Take-copter advances IRQ 7 once per video frame while the PCM master
	 * clock, and therefore sample pitch, remains unchanged. */
	xavix2_machine_set_timer_rate(machine, 60);
	machine->interrupt_enabled = UINT32_C(1) << 7;
	machine->cpu.waiting = 1;
	CHECK(xavix2_machine_execute(machine, XAVIX2_TIMER_CYCLES) ==
		XAVIX2_TIMER_CYCLES);
	CHECK(!(machine->interrupt_active & (UINT32_C(1) << 7)));
	CHECK(machine->frame_count == 0);
	CHECK(xavix2_machine_execute(machine, XAVIX2_TIMER_CYCLES) ==
		XAVIX2_TIMER_CYCLES);
	CHECK(machine->interrupt_active & (UINT32_C(1) << 7));
	CHECK(machine->frame_count == 1);
	xavix2_machine_set_timer_rate(machine, 120);
	/* Take-copter keeps a 60 Hz firmware timer in both display modes.  The
	 * 640-wide menu must not be mistaken for a 120 Hz timing switch. */
	store16(machine->mmio + 0x650, UINT16_C(0x1608));
	xavix2_machine_set_timer_rate(machine, 120);
	xavix2_machine_update_takecopter_timer_rate(machine);
	CHECK(machine->timer_rate_hz == 60);
	CHECK(machine->next_timer_cycle == machine->cpu.total_cycles +
		XAVIX2_CYCLES_PER_FRAME);
	{
		uint64_t next_timer_cycle = machine->next_timer_cycle;
		xavix2_machine_update_takecopter_timer_rate(machine);
		CHECK(machine->next_timer_cycle == next_timer_cycle);
	}
	store16(machine->mmio + 0x650, UINT16_C(0x1610));
	xavix2_machine_update_takecopter_timer_rate(machine);
	CHECK(machine->timer_rate_hz == 60);
	CHECK(machine->next_timer_cycle == machine->cpu.total_cycles +
		XAVIX2_CYCLES_PER_FRAME);
	store16(machine->mmio + 0x650, 0);
	xavix2_machine_set_timer_rate(machine, 120);
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
	 * depth/vertical/horizontal triples and consumes the projected Y/X pair. */
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

	/* Command 10 composes two Q16.16 affine matrices as source * registers.
	 * The source translation therefore stays outside the register scale. */
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
			-1, 0, 0, 10,
			0, -1, 0, 12,
			0, 0, 1, 14
		};
		unsigned element;
		for (element = 0; element < 12; ++element)
		{
			store32(machine->mmio + 0x800 + element * 4,
				geometry_q16_16(left[element], 1));
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

	/* Commands 01, 02, 03 and 0C use the documented Vector10 -> Vector32
	 * -> Vector16 pipeline.  Vector32 RAM order is Z/Y/X; Vector16 is
	 * clipping+Z/Y/X and its screen coordinates retain one fractional bit. */
	{
		uint32_t packed = packed_vector10(256, -128, 64, 1);
		memset(machine->mmio + 0x800, 0, 0x30);
		store32(machine->mmio + 0x800, UINT32_C(0xffff0100));
		store32(machine->mmio + 0x814, UINT32_C(0x0000ff00));
		store32(machine->mmio + 0x828, UINT32_C(0x12340100));
		store32(machine->mmio + 0x80c, UINT32_C(0x00018000));
		store32(machine->mmio + 0x81c, UINT32_C(0xfffe0000));
		store32(machine->mmio + 0x82c, UINT32_C(0x00040000));
		store32(machine->low_ram + 0x1180, packed);
		store16(machine->mmio + 0x85a, 0);
		store16(machine->mmio + 0x860, 0x1180);
		store16(machine->mmio + 0x862, 0x1190);
		machine->cpu.write8(machine->cpu.opaque,
			UINT32_C(0xffffe858), 0x01);
		CHECK(load32(machine->low_ram + 0x1190) == UINT32_C(0x00044000));
		CHECK(load32(machine->low_ram + 0x1194) == UINT32_C(0xfffe8000));
		CHECK(load32(machine->low_ram + 0x1198) == UINT32_C(0x00028000));

		store16(machine->mmio + 0x840, 128);
		store16(machine->mmio + 0x844, 1);
		store16(machine->mmio + 0x846, 10);
		store16(machine->mmio + 0x860, 0x1190);
		store16(machine->mmio + 0x862, 0x11a0);
		machine->cpu.write8(machine->cpu.opaque,
			UINT32_C(0xffffe858), 0x02);
		CHECK(load32(machine->low_ram + 0x11a0) == UINT32_C(0x00044000));
		/* Stand-alone P emits the documented one-fractional-bit coordinates. */
		CHECK((int16_t)load16(machine->low_ram + 0x11a4) == -90);
		CHECK((int16_t)load16(machine->low_ram + 0x11a6) == 150);

		store16(machine->mmio + 0x848, UINT16_C(0x0800));
		store16(machine->mmio + 0x84a, UINT16_C(0x0400));
		store16(machine->mmio + 0x860, 0x11a0);
		store16(machine->mmio + 0x862, 0x11b0);
		machine->cpu.write8(machine->cpu.opaque,
			UINT32_C(0xffffe858), 0x03);
		CHECK(load32(machine->low_ram + 0x11b0) == UINT32_C(0x00044000));
		CHECK(load16(machine->low_ram + 0x11b4) == 934);
		CHECK(load16(machine->low_ram + 0x11b6) == 2198);

		store16(machine->mmio + 0x860, 0x1180);
		store16(machine->mmio + 0x862, 0x11c0);
		machine->cpu.write8(machine->cpu.opaque,
			UINT32_C(0xffffe858), 0x0c);
		CHECK(load32(machine->low_ram + 0x11c0) == UINT32_C(0x00044000));
		/* Composite APV is bit-identical to Affine + Pproj + View: Vector16
		 * keeps its documented one fractional coordinate bit. */
		CHECK(load16(machine->low_ram + 0x11c4) == 934);
		CHECK(load16(machine->low_ram + 0x11c6) == 2198);
	}

	/* Command 07 establishes the rotated light vector. */
	{
		/* Command 07 uses the same matrix to rotate signed XYZ16 light
		 * vectors and keeps their six-byte layout. */
		memset(machine->mmio + 0x800, 0, 0x24);
		store32(machine->mmio + 0x804, UINT32_C(0x00000100));
		store32(machine->mmio + 0x80c, UINT32_C(0x00000080));
		store32(machine->mmio + 0x820, UINT32_C(0xffffff00));
		store16(machine->low_ram + 0x11c0, 1000);
		store16(machine->low_ram + 0x11c2, (uint16_t)-2000);
		store16(machine->low_ram + 0x11c4, 3000);
		store16(machine->low_ram + 0x11c6, UINT16_C(0x8000));
		store16(machine->low_ram + 0x11c8, UINT16_C(0x7fff));
		store16(machine->low_ram + 0x11ca, UINT16_C(0xffff));
		store16(machine->mmio + 0x860, 0x11c0);
		store16(machine->mmio + 0x862, 0x11d0);
		store16(machine->mmio + 0x85a, 1);
		machine->cpu.write8(machine->cpu.opaque,
			UINT32_C(0xffffe858), 0x07);
		CHECK((int16_t)load16(machine->low_ram + 0x11d0) == -2000);
		CHECK((int16_t)load16(machine->low_ram + 0x11d2) == 500);
		CHECK((int16_t)load16(machine->low_ram + 0x11d4) == -3000);
		CHECK((int16_t)load16(machine->low_ram + 0x11d6) == 32767);
		CHECK((int16_t)load16(machine->low_ram + 0x11d8) == -16384);
		CHECK((int16_t)load16(machine->low_ram + 0x11da) == 1);
	}

	/* Commands 0F/0B/0E implement the patent's TD, Ccalc and TDBP lighting
	 * chain. TD emits one Dot byte for each correctly packed Vector10 normal;
	 * Ccalc lights Type-1 vertex colors and TDBP writes the Type-0 Light field. */
	{
		uint8_t *gouraud = machine->low_ram + 0x11d0;
		uint8_t *textured = machine->low_ram + 0x1200;
		memset(machine->mmio + 0x800, 0, 0x24);
		store32(machine->mmio + 0x800, UINT32_C(0x00000100));
		store32(machine->mmio + 0x810, UINT32_C(0x00000100));
		store32(machine->mmio + 0x820, UINT32_C(0x00000100));
		store16(machine->mmio + 0x850, 0);
		store16(machine->mmio + 0x852, 0);
		store16(machine->mmio + 0x854, UINT16_C(0x7fff));
		machine->mmio[0x84c] = 0;
		store32(machine->low_ram + 0x11a0, packed_vector10(0, 0, 511, 0));
		store32(machine->low_ram + 0x11a4, packed_vector10(0, 0, -512, 0));
		memset(machine->low_ram + 0x11b0, 0xcc, 8);
		store16(machine->mmio + 0x860, 0x11a0);
		store16(machine->mmio + 0x862, 0x11b0);
		store16(machine->mmio + 0x85a, 1);
		machine->cpu.write8(machine->cpu.opaque,
			UINT32_C(0xffffe858), 0x0f);
		CHECK(machine->low_ram[0x11b0] == 0x9f);
		CHECK(machine->low_ram[0x11b1] == 0x5f);
		CHECK(machine->low_ram[0x11b2] == 0xcc);

		/* Vertex A/C use the front-lit +Z normal; B uses the unlit -Z normal. */
		store32(gouraud, UINT32_C(0x00000001));
		store32(gouraud + 4, UINT32_C(0x00000001));
		store32(gouraud + 8, UINT32_C(0x03e0001f));
		store32(gouraud + 12, UINT32_C(0x38007c00));
		store16(machine->mmio + 0x860, 0x11b0);
		store16(machine->mmio + 0x864, 0x11d0);
		store16(machine->mmio + 0x85a, 0);
		machine->cpu.write8(machine->cpu.opaque,
			UINT32_C(0xffffe858), 0x0b);
		CHECK(load32(gouraud + 8) == UINT32_C(0x0020001f));
		CHECK(load32(gouraud + 12) == UINT32_C(0x38007c00));

		memset(textured, 0, 32);
		store32(textured + 8, UINT32_C(0x12340040));
		store32(textured + 24, UINT32_C(0x56780040));
		store16(machine->mmio + 0x860, 0x11a0);
		store16(machine->mmio + 0x864, 0x1200);
		store16(machine->mmio + 0x85a, 1);
		machine->cpu.write8(machine->cpu.opaque,
			UINT32_C(0xffffe858), 0x0e);
		CHECK(load32(textured + 8) == UINT32_C(0x12347c40));
		CHECK(load32(textured + 24) == UINT32_C(0x56780440));
	}

	/* 0x4D is command 0x0D (CWD) with InvalidStructureRemove set. CWD
	 * consumes eight-byte Vector16 vertices, compacts clipped/back-facing
	 * triangles, calculates perspective weights, and writes packed GPU
	 * coordinates into the same 128-bit polygon record. */
	{
		uint8_t *vertices = machine->low_ram + 0x1200;
		uint8_t *polygons = machine->low_ram + 0x1300;
		uint32_t expected0;
		uint32_t expected1;
		uint32_t expected3;

		/* A(100,100,300), B(200,100,500), C(100,200,700). */
		store32(vertices + 0, UINT32_C(300) << 16);
		store16(vertices + 4, 100 * 2);
		store16(vertices + 6, 100 * 2);
		store32(vertices + 8, UINT32_C(500) << 16);
		store16(vertices + 12, 100 * 2);
		store16(vertices + 14, 200 * 2);
		store32(vertices + 16, UINT32_C(700) << 16);
		store16(vertices + 20, 200 * 2);
		store16(vertices + 22, 100 * 2);
		store32(vertices + 24, UINT32_C(0x80000000) |
			(UINT32_C(300) << 16));
		store16(vertices + 28, 120 * 2);
		store16(vertices + 30, 120 * 2);

		/* Textured front face A/B/C. */
		store32(polygons + 0, UINT32_C(0x00000000));
		store32(polygons + 4, UINT32_C(0x00020001));
		store32(polygons + 8, UINT32_C(0x123456aa));
		store32(polygons + 12, UINT32_C(0x00008035));
		/* Reversed, single-sided back face A/C/B. */
		store32(polygons + 16, UINT32_C(0x00000001));
		store32(polygons + 20, UINT32_C(0x00010002));
		store32(polygons + 24, UINT32_C(0x87654321));
		store32(polygons + 28, UINT32_C(0x0fedcba9));
		/* Double-sided but clipped A/B/D. */
		store32(polygons + 32, UINT32_C(0x00000002));
		store32(polygons + 36, UINT32_C(0x00030001));
		store32(polygons + 40, UINT32_C(0x13579bdf));
		store32(polygons + 44, UINT32_C(0x2468ace0));

		machine->mmio[0x856] = 0;
		store16(machine->mmio + 0x85a, 2);
		store16(machine->mmio + 0x860, 0x1200);
		store16(machine->mmio + 0x864, 0x1300);
		machine->cpu.write8(machine->cpu.opaque,
			UINT32_C(0xffffe858), 0x4d);
		CHECK(load16(machine->mmio + 0x85c) == 1);
		expected0 = (100u << 1) | (100u << 11) | (100u << 22);
		expected1 = 200u | (200u << 11) | (100u << 21);
		CHECK(load32(polygons) == expected0);
		CHECK(load32(polygons + 4) == expected1);
		CHECK(load32(polygons + 8) == UINT32_C(0x12345626));
		expected3 = (UINT32_C(0x00008035) & ~UINT32_C(0x07ffff80)) |
			(UINT32_C(27) << 7) | (UINT32_C(0x0a78) << 15);
		CHECK(load32(polygons + 12) == expected3);

		/* Without the remove flag, an invalid record stays in place with the
		 * documented off-screen sentinel coordinates. */
		store32(polygons + 0, UINT32_C(0x00000001));
		store32(polygons + 4, UINT32_C(0x00010002));
		store32(polygons + 8, UINT32_C(0x87654321));
		store32(polygons + 12, UINT32_C(0x0fedcba9));
		store16(machine->mmio + 0x85a, 0);
		machine->cpu.write8(machine->cpu.opaque,
			UINT32_C(0xffffe858), 0x0d);
		CHECK(load16(machine->mmio + 0x85c) == 1);
		CHECK((load32(polygons) & ~UINT32_C(1)) ==
			((UINT32_C(1023) << 1) | (UINT32_C(2047) << 11) |
				(UINT32_C(1023) << 22)));
		CHECK(load32(polygons + 4) ==
			(UINT32_C(2047) | (UINT32_C(1023) << 11) |
				(UINT32_C(2047) << 21)));
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

		/* A board without a verified motion receiver must not get a synthetic
		 * level-10 interrupt merely because the caller advances a video frame. */
		machine->interrupt_enabled = UINT32_C(1) << 10;
		machine->interrupt_nmi = 0;
		machine->interrupt_active = 0;
		machine->interrupt_pending = 0;
		machine->cpu.waiting = 1;
		machine->cpu.hr[4] |= 16;
		{
			const uint64_t interrupts_before = machine->cpu.interrupt_count;
			(void)xavix2_machine_run_video_frame(machine, NULL, 0);
			CHECK(machine->cpu.interrupt_count == interrupts_before);
			CHECK(!(machine->interrupt_active & (UINT32_C(1) << 10)));
		}
		machine->interrupt_enabled = 0;
		machine->cpu.waiting = 0;
	}
	CHECK(machine->mmio[0xc48] == 0x00);

	/* Epoch XaviX 2 boards configure the 24C04 bus on PIO16/17 rather than
	 * Bandai's PIO20/21 pair.  Exercise the real MMIO mode/data bridge. */
	machine->cpu.write8(machine->cpu.opaque, UINT32_C(0xffffe204), 0x0f);
	machine->cpu.write8(machine->cpu.opaque, UINT32_C(0xffffe20a), 0x03);
	CHECK(machine->eeprom.scl == 1);
	CHECK(machine->eeprom.master_sda == 1);
	machine->cpu.write8(machine->cpu.opaque, UINT32_C(0xffffe20a), 0x01);
	CHECK(machine->eeprom.scl == 0);
	CHECK(machine->eeprom.master_sda == 1);
	machine->cpu.write8(machine->cpu.opaque, UINT32_C(0xffffe204), 0x00);

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

	/* Sprite Filter is command bit 29.  At 2x, Filter=0 must interpolate the
	 * red/blue texels at destination pixel centres; Filter=1 keeps nearest. */
	store32(machine->low_ram + 0x0200, UINT32_C(0x00000001));
	store64(machine->low_ram + 0x4000, UINT64_C(2));
	store16(machine->palette_ram, UINT16_C(0x001f));
	store16(machine->palette_ram + 4, UINT16_C(0x7c00));
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	store64(machine->low_ram + 0x0500, gpu_command(0x20, 0x10));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xffff0000));
	CHECK(machine->screen_data[1] == UINT32_C(0xffbf003f));
	CHECK(machine->screen_data[2] == UINT32_C(0xff3f00bf));
	CHECK(machine->screen_data[3] == UINT32_C(0xff0000ff));

	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	store64(machine->low_ram + 0x0500,
		gpu_command(0x20, 0x10) | (UINT64_C(1) << 29));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1] == UINT32_C(0xffff0000));
	CHECK(machine->screen_data[2] == UINT32_C(0xff0000ff));
	store32(machine->low_ram + 0x0200, UINT32_C(0x0000000f));
	store64(machine->low_ram + 0x4000, UINT64_MAX);
	/* Bit 15 selects interleaved premultiplied RGB444/Nalpha; texel zero can
	 * still carry an ordinary opaque color. */
	store16(machine->palette_ram + 4, 0);
	machine->screen_data[0] = UINT32_C(0xffffffff);
	store64(machine->low_ram + 0x0500, gpu_command(0x10, 0x10));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xff000000));

	store16(machine->palette_ram + 4, UINT16_C(0x800f));
	machine->screen_data[0] = UINT32_C(0xff0000ff);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xff73001f));

	store64(machine->low_ram + 0x4000, 0);
	store16(machine->palette_ram, UINT16_C(0x8421));
	machine->screen_data[0] = UINT32_C(0xffffffff);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xffffffff));
	store16(machine->palette_ram, UINT16_C(0x7c00));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xff0000ff));
	store64(machine->low_ram + 0x4000, UINT64_MAX);

	/* The 16-byte GPU channel also carries Type-1 Gouraud triangles.  RGB555
	 * vertex colors are interpolated independently and are premultiplied by
	 * alpha; Nalpha stores the inverse destination contribution. */
	store16(machine->mmio + 0x400, 0x0600);
	store16(machine->mmio + 0x404, 1);
	store16(machine->mmio + 0x410, 0);
	store_triangle_record(machine->low_ram + 0x0600, 1,
		0, 0, 4, 0, 0, 4,
		UINT32_C(0x001f001f), UINT32_C(0x0000801f));
	machine->screen_data[1 * 0x800 + 1] = UINT32_C(0xff0000ff);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xffff0000));

	/* Adjacent translucent triangles share a half-open edge.  Pixel centres on
	 * the diagonal belong to exactly one face; blending both faces would apply
	 * Nalpha twice and reveal every diagonal in a Gouraud mesh. */
	store16(machine->mmio + 0x404, 2);
	store16(machine->mmio + 0x410, 0);
	store16(machine->mmio + 0x846, 10);
	store_triangle_record(machine->low_ram + 0x0600, 1,
		0, 0, 4, 0, 4, 4, UINT32_C(0x000f000f),
		(UINT32_C(3) << 29) | UINT32_C(0x000f));
	store_triangle_record(machine->low_ram + 0x0610, 1,
		0, 0, 4, 4, 0, 4, UINT32_C(0x000f000f),
		(UINT32_C(3) << 29) | UINT32_C(0x000f));
	machine->screen_data[1 * 0x800 + 1] = UINT32_C(0xff0000ff);
	machine->screen_data[1 * 0x800 + 2] = UINT32_C(0xff0000ff);
	machine->gpu_sprite_background_prepared = 0;
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xff7b005f));
	CHECK(machine->screen_data[1 * 0x800 + 2] == UINT32_C(0xff7b005f));
	store16(machine->mmio + 0x404, 1);

	/* E620 packs the four two-bit pixel-dither values selected by XY LSBs.
	 * EPOCH's C6 pattern rounds this shallow red gradient differently at
	 * adjacent pixels instead of exposing a solid RGB555 Mach band. */
	store_triangle_record(machine->low_ram + 0x0600, 1,
		0, 0, 8, 0, 0, 8,
		UINT32_C(0x001f0000), UINT32_C(0x00000000));
	machine->mmio[0x620] = UINT8_C(0xc6);
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xff100000));
	CHECK(machine->screen_data[1] == UINT32_C(0xff310000));
	CHECK(machine->screen_data[0x800] == UINT32_C(0xff080000));
	machine->mmio[0x620] = 0;

	/* Enhanced presentation interpolates the final composed RGB symmetrically
	 * across a polygon/non-polygon boundary.  The two output samples surrounding
	 * the edge blend the same red/blue pair from their respective sides instead
	 * of making the non-polygon side fall back to nearest-neighbour. */
	store_triangle_record(machine->low_ram + 0x0600, 1,
		1, 1, 9, 1, 1, 9,
		UINT32_C(0x001f001f), UINT32_C(0x0000801f));
	store16(machine->mmio + 0x656, 1);
	store16(machine->mmio + 0x658, 1);
	store16(machine->mmio + 0x60e, UINT16_C(0x7c00));
	{
		uint32_t pixel;
		for (pixel = 0; pixel < 0x400 * 0x800; ++pixel)
			machine->screen_data[pixel] = UINT32_C(0xff0000ff);
	}
	xavix2_machine_set_high_resolution_3d(machine, 1);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	{
		const uint32_t *enhanced;
		unsigned enhanced_width;
		unsigned enhanced_height;
		unsigned enhanced_stride;
		enhanced = xavix2_machine_visible_frame(machine, &enhanced_width,
			&enhanced_height, &enhanced_stride);
		CHECK(enhanced_width == 640);
		CHECK(enhanced_height == 480);
		CHECK(enhanced_stride == 640);
		CHECK(enhanced[12] == UINT32_C(0xffff0000));
		CHECK(enhanced[13] == UINT32_C(0xffbf0040));
		CHECK(enhanced[14] == UINT32_C(0xff4000bf));
		CHECK(enhanced[15] == UINT32_C(0xff0000ff));
	}
	/* A translucent sprite in front of a polygon retains destination RGB via
	 * Nalpha.  Enhanced presentation must therefore keep filtering that mixed
	 * pixel as composed 3D rather than switching it to nearest-neighbour at the
	 * sprite boundary.  DBZ's red energy aura is this exact arrangement. */
	store32(machine->low_ram + 0x0200, 0);
	store16(machine->low_ram + 0x0300, 0);
	machine->low_ram[0] = 1;
	store16(machine->palette_ram + 4, UINT16_C(0x800f));
	store16(machine->mmio + 0x40c, 0x0500);
	store16(machine->mmio + 0x410, 1);
	store64(machine->low_ram + 0x0500,
		gpu_command(0x10, 0x10) | UINT64_C(7) |
		(UINT64_C(1) << 11) | (UINT64_C(1) << 29));
	machine->gpu_sprite_background_prepared = 0;
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	{
		const uint32_t *enhanced;
		uint32_t composed =
			machine->screen_data[1 * 0x800 + 7];
		enhanced = xavix2_machine_visible_frame(machine, NULL, NULL, NULL);
		CHECK(enhanced[13] != composed);
	}
	xavix2_machine_set_high_resolution_3d(machine, 0);
	store16(machine->mmio + 0x410, 0);
	store16(machine->mmio + 0x656, 0);
	store16(machine->mmio + 0x658, 0);
	store16(machine->mmio + 0x60e, 0);

	store_triangle_record(machine->low_ram + 0x0600, 1,
		0, 0, 4, 0, 0, 4,
		UINT32_C(0x000f000f), UINT32_C(0x8000800f));
	machine->screen_data[1 * 0x800 + 1] = UINT32_C(0xff0000ff);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xff7b007f));

	/* Gouraud Nalpha=7 remains the documented 7/8 destination contribution;
	 * the fully transparent endpoint belongs only to indexed textures. */
	store_triangle_record(machine->low_ram + 0x0600, 1,
		0, 0, 4, 0, 0, 4, UINT32_C(0x00000000),
		(UINT32_C(7) << 29));
	machine->screen_data[1 * 0x800 + 1] = UINT32_C(0xffffffff);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xffdfdfdf));

	/* Triangle raster work is clipped to the guest-selected presentation
	 * window.  The internal target remains 2048x1024, but pixels outside the
	 * active 320x240 crop must neither be visited nor modified. */
	store_triangle_record(machine->low_ram + 0x0600, 1,
		0, 0, 8, 0, 0, 8,
		UINT32_C(0x001f001f), UINT32_C(0x0000801f));
	store16(machine->mmio + 0x410, 0);
	store16(machine->mmio + 0x656, 2);
	store16(machine->mmio + 0x658, 2);
	machine->screen_data[1 * 0x800 + 1] = UINT32_C(0xff00ff00);
	machine->screen_data[2 * 0x800 + 2] = UINT32_C(0xff0000ff);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xff00ff00));
	CHECK(machine->screen_data[2 * 0x800 + 2] == UINT32_C(0xffff0000));
	store16(machine->mmio + 0x656, 0);
	store16(machine->mmio + 0x658, 0);

	/* Quantized far-terrain batches close a short vertical seam without
	 * extending through nearer geometry or changing the final opaque alpha. */
	store16(machine->mmio + 0x400, 0x0600);
	store16(machine->mmio + 0x404, 2);
	store16(machine->mmio + 0x410, 0);
	store_triangle_record(machine->low_ram + 0x0600, 1,
		0, 0, 4, 0, 0, 2, UINT32_C(0x001f001f),
		(UINT32_C(0x0900) << 15) | UINT32_C(0x001f));
	store_triangle_record(machine->low_ram + 0x0610, 1,
		0, 4, 4, 4, 0, 6, UINT32_C(0x03e003e0),
		(UINT32_C(0x0900) << 15) | UINT32_C(0x03e0));
	memset(machine->screen_data, 0xff, sizeof(machine->screen_data));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[2 * 0x800] == UINT32_C(0xff00ff00));
	CHECK(machine->screen_data[3 * 0x800] == UINT32_C(0xff00ff00));
	store16(machine->mmio + 0x404, 1);

	/* A repeated RPU submission can redraw the same short HUD bar over the
	 * terrain seam.  Its second copy is byte-identical to the captured base,
	 * so base-color comparison alone must not let seam closure replace it with
	 * the lower terrain.  Sprite ownership persists for the whole video frame. */
	store16(machine->mmio + 0x404, 2);
	store16(machine->mmio + 0x40c, 0x0700);
	store16(machine->mmio + 0x410, 1);
	store16(machine->mmio + 0x608, 0x0200);
	store16(machine->mmio + 0x622, 0x0300);
	store32(machine->low_ram + 0x0200, 0);
	store16(machine->low_ram + 0x0300, 1);
	memset(machine->low_ram + 0x4000, 0xff, 8);
	store16(machine->palette_ram + 4, UINT16_C(0x001f));
	store64(machine->low_ram + 0x0700,
		gpu_command(0x10, 0x10) | (UINT64_C(2) << 11) |
		(UINT64_C(0x09) << 21));
	memset(machine->screen_data, 0xff, sizeof(machine->screen_data));
	machine->gpu_sprite_background_prepared = 0;
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[2 * 0x800] == UINT32_C(0xffff0000));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[2 * 0x800] == UINT32_C(0xffff0000));
	store16(machine->mmio + 0x404, 1);
	store16(machine->mmio + 0x410, 0);

	/* EPOCH submits its Type-1 sky and transition overlay in the GPU0 stream,
	 * while characters and clouds arrive through GPU1.  A depth-zero fade is
	 * held until E414, then composed after every nonzero-depth sprite. */
	store16(machine->mmio + 0x404, 2);
	store16(machine->mmio + 0x40c, 0x0500);
	store16(machine->mmio + 0x410, 1);
	store16(machine->mmio + 0x608, 0x0200);
	store16(machine->mmio + 0x622, 0x0300);
	store16(machine->mmio + 0x846, UINT16_C(0x7fff));
	store32(machine->low_ram + 0x0200, UINT32_C(0x0000000f));
	store16(machine->low_ram + 0x0300, 1);
	memset(machine->low_ram + 0x4000, 0xff, 8);
	store16(machine->palette_ram + 4, UINT16_C(0x03e0));
	store64(machine->low_ram + 0x0500,
		gpu_command(0x10, 0x10) | (UINT64_C(0x2e) << 21));
	store_triangle_record(machine->low_ram + 0x0600, 1,
		0, 0, 4, 0, 0, 4, UINT32_C(0x7c007c00),
		(UINT32_C(0x0900) << 15) | UINT32_C(0x7c00));
	store_triangle_record(machine->low_ram + 0x0610, 1,
		0, 0, 4, 0, 0, 4, UINT32_C(0x000f000f),
		(UINT32_C(4) << 29) | UINT32_C(0x000f));
	machine->gpu_sprite_background_prepared = 0;
	machine->screen_data[0] = 0;
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xff0000ff));
	CHECK(machine->gpu_sprite_background_prepared == 0xfe);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xff7b7f00));

	/* EPOCH's Type-0 and Type-1 GPU0 records share one raw 12-bit depth
	 * ordering.  The farther blue texture is earlier even though its class is
	 * listed after the nearer red Gouraud triangle in the old split renderer. */
	store16(machine->mmio + 0x404, 2);
	store16(machine->mmio + 0x410, 0);
	store32(machine->low_ram + 0x0200, 0);
	store16(machine->low_ram + 0x0300, 0);
	machine->low_ram[0] = 0;
	store16(machine->palette_ram, UINT16_C(0x7c00));
	store_triangle_record(machine->low_ram + 0x0600, 1,
		0, 0, 4, 0, 0, 4, UINT32_C(0x001f001f),
		(UINT32_C(0x0800) << 15) | UINT32_C(0x001f));
	store_triangle_record(machine->low_ram + 0x0610, 0,
		0, 0, 4, 0, 0, 4, UINT32_C(0x00007c40),
		(UINT32_C(0x0900) << 15) | UINT32_C(0x00002000));
	machine->gpu_sprite_background_prepared = 0;
	machine->screen_data[1 * 0x800 + 1] = 0;
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xffff0000));
	store16(machine->mmio + 0x404, 1);
	store16(machine->mmio + 0x40c, 0x0500);
	store16(machine->mmio + 0x410, 0);
	store16(machine->mmio + 0x846, 10);

	/* An opaque black texel must overwrite the framebuffer, including index
	 * zero when its selected palette entry is not the transparent key. */
	store16(machine->mmio + 0x608, 0x0200);
	store16(machine->mmio + 0x622, 0x0300);
	store32(machine->low_ram + 0x0200, 0);
	store16(machine->low_ram + 0x0300, 0);
	machine->low_ram[0] = 0;
	store16(machine->palette_ram, 0);
	store_triangle_record(machine->low_ram + 0x0600, 0,
		0, 0, 4, 0, 0, 4, UINT32_C(0x00007c40),
		UINT32_C(0x00022000));
	machine->screen_data[1 * 0x800 + 1] = UINT32_C(0xffffffff);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xff000000));

	store16(machine->palette_ram, UINT16_C(0x800f));
	machine->screen_data[1 * 0x800 + 1] = UINT32_C(0xff0000ff);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xff73001f));

	store16(machine->palette_ram, UINT16_C(0x8421));
	machine->screen_data[1 * 0x800 + 1] = UINT32_C(0xffffffff);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xffffffff));

	/* EPOCH's cloud-border entry stores zero red, low premultiplied cyan,
	 * and Nalpha=7.  It must preserve seven eighths of the sky instead of
	 * darkening the rectangular texture boundary as fixed half-alpha did. */
	store16(machine->palette_ram, UINT16_C(0x8c61));
	machine->screen_data[1 * 0x800 + 1] = UINT32_C(0xff31adff);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xff31adff));
	machine->low_ram[0] = 1;

	/* Type-0 flag bit 3 is packed into d3 bit 27.  Filter=0 bilinearly
	 * combines the four surrounding texels; Filter=1 preserves the existing
	 * nearest-neighbour sample.  A 9x9 screen triangle maps pixel (1,1) to the
	 * exact centre of this four-color 8-bpp checker. */
	store32(machine->low_ram + 0x0200, UINT32_C(0x07000303));
	store16(machine->low_ram + 0x0300, 1);
	memset(machine->low_ram + 0x4000, 0, 16);
	machine->low_ram[0x4000] = 0;
	machine->low_ram[0x4001] = 1;
	machine->low_ram[0x4004] = 2;
	machine->low_ram[0x4005] = 3;
	store16(machine->palette_ram, UINT16_C(0x001f));
	store16(machine->palette_ram + 4, UINT16_C(0x03e0));
	store16(machine->palette_ram + 8, UINT16_C(0x7c00));
	store16(machine->palette_ram + 12, UINT16_C(0x7fff));
	store_triangle_record(machine->low_ram + 0x0600, 0,
		0, 0, 9, 0, 0, 9, UINT32_C(0x00007c40),
		UINT32_C(0x00002040));
	machine->screen_data[1 * 0x800 + 1] = 0;
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xff7f7f7f));
	store_triangle_record(machine->low_ram + 0x0600, 0,
		0, 0, 9, 0, 0, 9, UINT32_C(0x00007c40),
		UINT32_C(0x08002040));
	machine->screen_data[1 * 0x800 + 1] = 0;
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xffff0000));

	/* Palette Nalpha=7 is the fully transparent texture endpoint.  Filtering
	 * three such texels with one opaque red texel preserves three quarters of
	 * the destination without a black halo. */
	store16(machine->palette_ram + 4, UINT16_C(0x8421));
	store16(machine->palette_ram + 8, UINT16_C(0x8421));
	store16(machine->palette_ram + 12, UINT16_C(0x8421));
	store_triangle_record(machine->low_ram + 0x0600, 0,
		0, 0, 9, 0, 0, 9, UINT32_C(0x00007c40),
		UINT32_C(0x00002040));
	machine->screen_data[1 * 0x800 + 1] = UINT32_C(0xff0000ff);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xff3f00bf));

	/* The Type-0 Light field is a multiplier from 1/32 through 32/32.  It
	 * scales the premultiplied texture RGB but not its alpha. */
	memset(machine->low_ram + 0x4000, 0, 16);
	store16(machine->palette_ram, UINT16_C(0x001f));
	store_triangle_record(machine->low_ram + 0x0600, 0,
		0, 0, 4, 0, 0, 4, UINT32_C(0x00003c40),
		UINT32_C(0x08002000));
	machine->screen_data[1 * 0x800 + 1] = 0;
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[1 * 0x800 + 1] == UINT32_C(0xff7f0000));

	/* Four-tap sampling reuses the exact storage sampler at the lower folded
	 * edge.  The two Map layouts select different top neighbours while the
	 * V=height neighbours are clamped and folded by the same address rules. */
	store16(machine->palette_ram + 4, UINT16_C(0x03e0));
	store16(machine->palette_ram + 8, UINT16_C(0x7c00));
	store16(machine->palette_ram + 12, UINT16_C(0x7fff));
	store32(machine->low_ram + 0x0200, UINT32_C(0x07000603));
	memset(machine->low_ram + 0x4000, 0, 16);
	machine->low_ram[0x4007] = 0;
	machine->low_ram[0x4006] = 1;
	machine->low_ram[0x4003] = 2;
	machine->low_ram[0x4002] = 3;
	machine->low_ram[0x400f] = 3;
	machine->low_ram[0x400e] = 2;
	store_triangle_record(machine->low_ram + 0x0600, 0,
		0, 0, 7, 0, 0, 6, UINT32_C(0x00007c40),
		UINT32_C(0x00002000));
	machine->screen_data[5 * 0x800] = 0;
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[5 * 0x800] == UINT32_C(0xffc87f7f));
	store_triangle_record(machine->low_ram + 0x0600, 0,
		0, 0, 7, 0, 0, 6, UINT32_C(0x00007c40),
		UINT32_C(0x00002040));
	machine->screen_data[5 * 0x800] = 0;
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[5 * 0x800] == UINT32_C(0xff7f7fff));

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
	store16(machine->palette_ram + 4, UINT16_C(0x7c00));
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
	CHECK(machine->screen_data[0] == 0);
	store16(machine->palette_ram + 4, UINT16_C(0x03e0));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xff00ff00));

	/* Nonzero polygon depth uses the shared sprite/polygon ordering.  The
	 * depth-D0 dragon body is behind a depth-2FF Type-1 shadow, while the
	 * depth-10 character remains in front.  This is the Blue Dragon boss
	 * connector arrangement rather than a title-specific layer override. */
	store_triangle_record(machine->low_ram + 0x0600, 1,
		0, 0, 4, 0, 0, 4,
		UINT32_C(0x001f001f), UINT32_C(0x017f801f));
	store64(machine->low_ram + 0x0700,
		gpu_command(0x10, 0x10) | (UINT64_C(0xd0) << 21));
	store64(machine->low_ram + 0x0708,
		gpu_command(0x10, 0x10) | (UINT64_C(0x10) << 21));
	store16(machine->palette_ram + 4, UINT16_C(0x7c00));
	machine->gpu_sprite_background_prepared = 0;
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	/* E408 captures the base without rasterizing an intermediate copy.  The
	 * final RPU merge waits for E414 because the guest can change the Sprite
	 * palette in between. */
	CHECK(machine->gpu_sprite_background_prepared == 0xfc);
	CHECK(machine->screen_data[0] == 0);
	store16(machine->palette_ram + 4, UINT16_C(0x03e0));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xff00ff00));
	/* Bandai's 120 Hz event handler can trigger the alternate complete command
	 * buffer before the next 60 Hz vblank.  The RPU has already latched the
	 * displayed polygon/sprite pair, so a later palette epoch must not be
	 * composited into that completed surface.  Doing so produced DB2J's
	 * one-frame brown/black reflection rectangles and doubled raster work. */
	{
		uint64_t pixel_writes = machine->gpu_pixel_write_count;
		store16(machine->palette_ram + 4, UINT16_C(0x7c00));
		machine->cpu.r[0] = 0;
		machine->cpu.r[1] = UINT32_C(0xffffe408);
		machine->cpu.pc = 4;
		CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
		machine->cpu.r[0] = 0;
		machine->cpu.r[1] = UINT32_C(0xffffe414);
		machine->cpu.pc = 4;
		CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
		CHECK(machine->screen_data[0] == UINT32_C(0xff00ff00));
		CHECK(machine->gpu_pixel_write_count == pixel_writes);
	}

	/* The patented depth comparator merges a new prefetch entry with an object
	 * recycled from the preceding scanline.  A newly appearing farther sprite
	 * is painted first and remains behind the continuing red sprite; changing
	 * it to a nearer depth must put the new green sprite in front. */
	store16(machine->mmio + 0x400, 0x0600);
	store16(machine->mmio + 0x404, 1);
	store16(machine->mmio + 0x40c, 0x0700);
	store16(machine->mmio + 0x410, 2);
	store32(machine->low_ram + 0x0200, UINT32_C(0x00000100));
	store32(machine->low_ram + 0x0204, UINT32_C(0x08000000));
	store16(machine->low_ram + 0x0300, 1);
	memset(machine->low_ram + 0x4000, 0xff, 16);
	store16(machine->palette_ram + 4, UINT16_C(0x001f));
	store16(machine->palette_ram + 12, UINT16_C(0x03e0));
	store_triangle_record(machine->low_ram + 0x0600, 1,
		100, 0, 101, 0, 100, 2, UINT32_C(0x001f001f),
		UINT32_C(0x0400001f));
	store64(machine->low_ram + 0x0700,
		gpu_command(0x10, 0x10) | (UINT64_C(0x20) << 21));
	store64(machine->low_ram + 0x0708,
		gpu_command(0x10, 0x10) | (UINT64_C(1) << 11) |
		(UINT64_C(0x30) << 21) | (UINT64_C(1) << 30));
	machine->gpu_sprite_background_prepared = 0;
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xffff0000));
	CHECK(machine->screen_data[0x800] == UINT32_C(0xffff0000));

	store64(machine->low_ram + 0x0708,
		gpu_command(0x10, 0x10) | (UINT64_C(1) << 11) |
		(UINT64_C(0x10) << 21) | (UINT64_C(1) << 30));
	machine->gpu_sprite_background_prepared = 0;
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0x800] == UINT32_C(0xff00ff00));

	/* Equal-depth records keep firmware list order across the two queues.  The
	 * later red record starts first and is recycled; a lower-index green record
	 * which appears on line one must still be painted before it. */
	store64(machine->low_ram + 0x0700,
		gpu_command(0x10, 0x10) | (UINT64_C(1) << 11) |
		(UINT64_C(0x20) << 21) | (UINT64_C(1) << 30));
	store64(machine->low_ram + 0x0708,
		gpu_command(0x10, 0x10) | (UINT64_C(0x20) << 21));
	machine->gpu_sprite_background_prepared = 0;
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0x800] == UINT32_C(0xffff0000));

	/* DB2J's GAME OVER / MISSION CLEAR banner immediately follows the long
	 * depth-zero tiled fighter run.  Its nonzero record depth is not a request
	 * to hide behind the fighter: the active-line painter keeps that following
	 * presentation object in front, including when it first appears on a later
	 * scanline and the fighter tiles arrive through the recycle queue. */
	store32(machine->low_ram + 0x0200, UINT32_C(0x00000f0f));
	store32(machine->low_ram + 0x0204, UINT32_C(0xf10021b5));
	store16(machine->low_ram + 0x0300, 1);
	store16(machine->low_ram + 0x0302, 2);
	memset(machine->low_ram + 0x4000, 0xff, 512);
	memset(machine->low_ram + 0x8000, 0xff, 1024);
	store16(machine->palette_ram + 4, UINT16_C(0x001f));
	store16(machine->palette_ram + 123 * 4, UINT16_C(0x03e0));
	store16(machine->mmio + 0x410, 9);
	for (unsigned banner_tile = 0; banner_tile < 8; ++banner_tile)
		store64(machine->low_ram + 0x0700 + banner_tile * 8,
			gpu_command(0x10, 0x10));
	store64(machine->low_ram + 0x0740,
		gpu_command(0x10, 0x10) | (UINT64_C(1) << 11) |
		(UINT64_C(0x08) << 21) | (UINT64_C(1) << 30) |
		(UINT64_C(1) << 58));
	store_triangle_record(machine->low_ram + 0x0600, 1,
		100, 100, 104, 100, 100, 104,
		UINT32_C(0x001f001f), UINT32_C(0x0400001f));
	machine->gpu_sprite_background_prepared = 0;
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0x800] == UINT32_C(0xff00ff00));

	/* Presentation-scale same-depth backings are drawn before smaller overlays
	 * regardless of their list order.  DBZ puts its logo before a later
	 * full-screen backing, whereas DB2J puts its full-screen Shenron backing
	 * before later dialogue-panel tiles. */
	store32(machine->low_ram + 0x0204, UINT32_C(0x0800efff));
	store16(machine->palette_ram + 4, UINT16_C(0x001f));
	store16(machine->palette_ram + 12, UINT16_C(0x7c00));
	store64(machine->low_ram + 0x0700,
		gpu_command(0x10, 0x10) | (UINT64_C(0xff) << 21));
	store64(machine->low_ram + 0x0708,
		gpu_command(0x10, 0x10) | (UINT64_C(0xff) << 21) |
		(UINT64_C(1) << 30));
	machine->gpu_sprite_background_prepared = 0;
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xffff0000));
	/* Reverse the two commands and use a green small overlay: the large blue
	 * backing must still be painted first, leaving the panel tile visible. */
	store16(machine->palette_ram + 4, UINT16_C(0x03e0));
	store64(machine->low_ram + 0x0700,
		gpu_command(0x10, 0x10) | (UINT64_C(0xff) << 21) |
		(UINT64_C(1) << 30));
	store64(machine->low_ram + 0x0708,
		gpu_command(0x10, 0x10) | (UINT64_C(0xff) << 21));
	machine->gpu_sprite_background_prepared = 0;
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xff00ff00));
	/* Ordinary equal-depth objects instead retain list order.  DB2J submits an
	 * attack effect after its tiled enemy, so the later effect must stay in
	 * front even when its covered area is larger than one character tile. */
	store32(machine->low_ram + 0x0204, UINT32_C(0x08000f0f));
	store16(machine->palette_ram + 4, UINT16_C(0x7c00));
	store64(machine->low_ram + 0x0700,
		gpu_command(0x10, 0x10) | (UINT64_C(0xff) << 21));
	store64(machine->low_ram + 0x0708,
		gpu_command(0x10, 0x10) | (UINT64_C(0xff) << 21) |
		(UINT64_C(1) << 30));
	machine->gpu_sprite_background_prepared = 0;
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xff0000ff));

	/* DBZ submits a fighter as a long run of small depth-00 tiles followed by
	 * one large depth-71 energy sphere.  The active-line merger leaves that
	 * later effect over the assembled fighter, while a global depth sort would
	 * redraw all of the tiles over its opaque core. */
	unsigned tile_index;
	store32(machine->low_ram + 0x0200, UINT32_C(0x00000f0f));
	store32(machine->low_ram + 0x0204, UINT32_C(0x00005f5f));
	store16(machine->low_ram + 0x0300, 1);
	store16(machine->low_ram + 0x0302, 2);
	store64(machine->low_ram + 0x4000, 0);
	store64(machine->low_ram + 0x8000, UINT64_MAX);
	store16(machine->palette_ram, UINT16_C(0x001f));
	store16(machine->palette_ram + 4, UINT16_C(0x03e0));
	store16(machine->mmio + 0x410, 9);
	for (tile_index = 0; tile_index < 8; ++tile_index)
		store64(machine->low_ram + 0x0700 + tile_index * 8,
			gpu_command(0x10, 0x10));
	store64(machine->low_ram + 0x0740,
		gpu_command(0x20, 0x20) | (UINT64_C(0x71) << 21) |
		(UINT64_C(1) << 30) | (UINT64_C(1) << 58));
	machine->gpu_sprite_background_prepared = 0x80;
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xff00ff00));
	/* The weak sphere stays at depth zero.  Once its scaled area exceeds the
	 * large-backing threshold it must still remain after the tiled fighter,
	 * rather than suddenly flipping behind the enemy partway through growth. */
	store64(machine->low_ram + 0x0740,
		gpu_command(0x20, 0x20) | (UINT64_C(1) << 30) |
		(UINT64_C(1) << 58));
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xff00ff00));
	/* A result-screen-like run at nonzero depth is not the tiled-fighter case.
	 * It keeps the ordinary depth ordering instead of replaying the whole list
	 * in firmware order and exposing repeated logos above its dialogue panel. */
	for (tile_index = 0; tile_index < 8; ++tile_index)
		store64(machine->low_ram + 0x0700 + tile_index * 8,
			gpu_command(0x10, 0x10) | (UINT64_C(0x14) << 21));
	store64(machine->low_ram + 0x0740,
		gpu_command(0x20, 0x20) | (UINT64_C(0xfe) << 21) |
		(UINT64_C(1) << 30) | (UINT64_C(1) << 58));
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xffff0000));
	store32(machine->low_ram + 0x0200, 0);
	store32(machine->low_ram + 0x0204, UINT32_C(0x08000f0f));
	store16(machine->low_ram + 0x0300, 0);
	store16(machine->palette_ram + 4, UINT16_C(0x7c00));
	store64(machine->low_ram + 0x0700,
		gpu_command(0x10, 0x10) | (UINT64_C(0xff) << 21));
	store64(machine->low_ram + 0x0708,
		gpu_command(0x10, 0x10) | (UINT64_C(0x09) << 21));

	/* A sprite-only frame has no E408 opportunity to prepaint depth FF.
	 * E414 must supply that band once, while repeated submissions in the same
	 * frame must not paint it again. */
	store16(machine->mmio + 0x410, 1);
	store16(machine->palette_ram + 4, UINT16_C(0x7c00));
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
	/* Startup can trigger E408 while the new sprite list is still empty.  That
	 * must not suppress the depth-FF logo submitted moments later by E414. */
	store16(machine->mmio + 0x404, 0);
	store16(machine->mmio + 0x410, 0);
	machine->gpu_sprite_background_prepared = 0;
	memset(machine->screen_data, 0, sizeof(machine->screen_data));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(!machine->gpu_sprite_background_prepared);
	store16(machine->mmio + 0x410, 1);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xff0000ff));
	CHECK(machine->gpu_sprite_background_prepared);
	/* Startup submits another empty E408/list pair in the same video frame;
	 * that opens a new opportunity for its second depth-FF logo buffer. */
	store16(machine->mmio + 0x410, 0);
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe408);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(!machine->gpu_sprite_background_prepared);
	store64(machine->low_ram + 0x0710,
		gpu_command(0x10, 0x10) | (UINT64_C(0xff) << 21));
	store16(machine->mmio + 0x40c, 0x0710);
	store16(machine->mmio + 0x410, 1);
	store16(machine->palette_ram + 4, UINT16_C(0x03e0));
	machine->cpu.r[0] = 0;
	machine->cpu.r[1] = UINT32_C(0xffffe414);
	machine->cpu.pc = 4;
	CHECK(xavix2_cpu_execute(&machine->cpu, 4) == 4);
	CHECK(machine->screen_data[0] == UINT32_C(0xff00ff00));
	store16(machine->mmio + 0x40c, 0x0700);
	/* Repeated GPU0 submissions likewise must not put the distant band back
	 * over triangles already produced earlier in the frame. */
	store16(machine->mmio + 0x404, 0);
	store16(machine->palette_ram + 4, UINT16_C(0x7c00));
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
		/* A pure-2D frame remains native even when 3D enhancement is enabled;
		 * there is no polygon edge to improve and expanding it only wastes the
		 * startup frame budget. */
		machine->screen_data[0x0188 * 0x800 + 0x0310] =
			UINT32_C(0xff13579b);
		xavix2_machine_set_high_resolution_3d(machine, 1);
		visible = xavix2_machine_visible_frame(machine, &visible_width,
			&visible_height, &visible_stride);
		CHECK(xavix2_machine_frame_scale(machine) == 1);
		CHECK(visible_width == 320);
		CHECK(visible_height == 240);
		CHECK(visible_stride == 0x800);
		CHECK(visible[0] == UINT32_C(0xff13579b));
		xavix2_machine_set_high_resolution_3d(machine, 0);
		CHECK(xavix2_machine_frame_scale(machine) == 1);
		/* Startup mode 08 exposes a 640x480 surface rather than a 320x240
		 * crop of its upper-left quarter. */
		machine->mmio[0x650] = 0x08;
		store16(machine->mmio + 0x656, 0x02c0);
		store16(machine->mmio + 0x658, 0x0106);
		visible = xavix2_machine_visible_frame(machine, &visible_width,
			&visible_height, &visible_stride);
		CHECK(visible == machine->screen_data + 0x0106 * 0x800 + 0x02c0);
		CHECK(visible_width == 640);
		CHECK(visible_height == 480);
		CHECK(visible_stride == 0x800);
		/* Naruto and Blue Dragon set additional high-byte display flags during
		 * their XaviX logo.  The low-byte mode remains 08 and must still expose
		 * the complete 640x480 surface. */
		machine->mmio[0x651] = 0x20;
		store16(machine->mmio + 0x658, 0x0120);
		visible = xavix2_machine_visible_frame(machine, &visible_width,
			&visible_height, &visible_stride);
		CHECK(visible == machine->screen_data + 0x0120 * 0x800 + 0x02c0);
		CHECK(visible_width == 640);
		CHECK(visible_height == 480);
		CHECK(visible_stride == 0x800);
		/* EPOCH mode 1608 stores 640 logical pixels by 240 logical rows and
		 * repeats each row on the 480-line television output. */
		machine->mmio[0x651] = 0x16;
		store16(machine->mmio + 0x656, 0x02c0);
		store16(machine->mmio + 0x658, 0x0110);
		machine->screen_data[0x0110 * 0x800 + 0x02c0] =
			UINT32_C(0xff123456);
		machine->screen_data[0x0111 * 0x800 + 0x02c0] =
			UINT32_C(0xffabcdef);
		visible = xavix2_machine_visible_frame(machine, &visible_width,
			&visible_height, &visible_stride);
		CHECK(visible != machine->screen_data + 0x0110 * 0x800 + 0x02c0);
		CHECK(visible_width == 640);
		CHECK(visible_height == 480);
		CHECK(visible_stride == 640);
		CHECK(visible[0] == UINT32_C(0xff123456));
		CHECK(visible[640] == UINT32_C(0xff123456));
		CHECK(visible[1280] == UINT32_C(0xffabcdef));
		/* The same 1608 value with startup origin 0120 is a native 480-line
		 * surface; this keeps the XaviX logo at its original height. */
		store16(machine->mmio + 0x658, 0x0120);
		machine->screen_data[(0x0120 + 300) * 0x800 + 0x02c0] =
			UINT32_C(0xff654321);
		visible = xavix2_machine_visible_frame(machine, &visible_width,
			&visible_height, &visible_stride);
		CHECK(visible == machine->screen_data + 0x0120 * 0x800 + 0x02c0);
		CHECK(visible_width == 640);
		CHECK(visible_height == 480);
		CHECK(visible_stride == 0x800);
		CHECK(visible[300 * 0x800] == UINT32_C(0xff654321));
		machine->mmio[0x651] = 0;
		store16(machine->mmio + 0x656, 0x07ff);
		store16(machine->mmio + 0x658, 0x03ff);
		visible = xavix2_machine_visible_frame(machine, NULL, NULL, NULL);
		CHECK(visible == machine->screen_data +
			(0x200 - 240) * 0x800 + (0x400 - 320));
		machine->mmio[0x650] = 0x10;
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
	/* The MMIO command bridge must preserve EA1B's key-off flag rather than
	 * treating every live update as a pitch slide. */
	memcpy(machine->video_ram + 0xf800, descriptors, sizeof(descriptors));
	xavix2_audio_init(&machine->audio, rom, UINT32_C(0x10000));
	machine->cpu.write8(machine->cpu.opaque, UINT32_C(0xffffea0a), 0x40);
	machine->cpu.write8(machine->cpu.opaque, UINT32_C(0xffffea0b), 0x00);
	CHECK(machine->audio.voice[0].active);
	machine->cpu.write8(machine->cpu.opaque, UINT32_C(0xffffea18), 0x00);
	machine->cpu.write8(machine->cpu.opaque, UINT32_C(0xffffea19), 0x10);
	machine->cpu.write8(machine->cpu.opaque, UINT32_C(0xffffea1a), 0x00);
	machine->cpu.write8(machine->cpu.opaque, UINT32_C(0xffffea1b), 0x01);
	machine->cpu.write8(machine->cpu.opaque, UINT32_C(0xffffea1c), 0x40);
	machine->cpu.write8(machine->cpu.opaque, UINT32_C(0xffffea1d), 0x40);
	machine->cpu.write8(machine->cpu.opaque, UINT32_C(0xffffea0a), 0xc0);
	machine->cpu.write8(machine->cpu.opaque, UINT32_C(0xffffea0b), 0x00);
	CHECK(machine->audio.voice[0].active);
	CHECK(machine->audio.voice[0].release_phase == 1);
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
		xavix2_audio_command(&machine->audio, 0x40, descriptors, 0, 0, 0, 0);
		CHECK(xavix2_machine_run_video_frame(machine, NULL, 0) != 0);
		CHECK(machine->experimental_direct_pio_sample == 0);
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
		machine->experimental_direct_pio_sample = 1;
		machine->experimental_dispatch_input = 1;
		machine->experimental_callback_pending = 1;
		machine->experimental_callback_address = UINT32_C(0x12345678);
		machine->experimental_capture_readback = 1;
		machine->experimental_capture_a = UINT16_C(0x1234);
		machine->experimental_capture_b = UINT16_C(0x5678);
		machine->experimental_sampled_pio = UINT32_C(0x87654321);
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
		CHECK(machine->experimental_direct_pio_sample == 0);
		CHECK(machine->experimental_dispatch_input == 0);
		CHECK(machine->experimental_callback_pending == 0);
		CHECK(machine->experimental_callback_address == 0);
		CHECK(machine->experimental_capture_readback == 0);
		CHECK(machine->experimental_capture_a == 0);
		CHECK(machine->experimental_capture_b == 0);
		CHECK(machine->experimental_sampled_pio == 0);
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

	/* A host catch-up frame advances guest time and audio without clearing the
	 * last completed surface.  The following normal frame resumes ordinary
	 * vblank clearing, so automatic frame skipping cannot become sticky. */
	xavix2_machine_reset(machine);
	machine->screen_data[123] = UINT32_C(0xff123456);
	machine->cpu.waiting = 1;
	xavix2_machine_set_skip_render(machine, 1);
	CHECK(xavix2_machine_run_video_frame(machine, NULL, 0) != 0);
	CHECK(machine->screen_data[123] == UINT32_C(0xff123456));
	xavix2_machine_set_skip_render(machine, 0);
	CHECK(xavix2_machine_run_video_frame(machine, NULL, 0) != 0);
	CHECK(machine->screen_data[123] == UINT32_C(0xff000000));

	free(machine);
	free(rom);
	puts("xavix2 machine tests passed");
	return 0;
}
