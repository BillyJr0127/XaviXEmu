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
	CHECK(xavix2_machine_init(machine, rom, UINT32_C(0x10000)));

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

	/* Different XaviX 2 firmware revisions place the IRQ-10 producer packet
	 * at different low-RAM addresses.  The board profile selects the address;
	 * the frame API must not silently keep writing Naruto's 0x000d buffer. */
	{
		const uint8_t packet[XAVIX2_MOTION_PACKET_SIZE] =
			{ 1, 2, 3, 4, 5, 6, 7 };
		xavix2_machine_set_motion_packet_address(machine, 0x014d);
		xavix2_machine_set_motion_packet_address(machine, 0xffff);
		CHECK(machine->motion_packet_address == 0x014d);
		machine->cpu.waiting = 1;
		machine->interrupt_enabled = 0;
		(void)xavix2_machine_run_video_frame(machine, packet, 0);
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

	free(machine);
	free(rom);
	puts("xavix2 machine tests passed");
	return 0;
}
