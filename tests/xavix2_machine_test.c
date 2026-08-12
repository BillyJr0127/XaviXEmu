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

int main(void)
{
	uint8_t *rom = (uint8_t *)calloc(1, UINT32_C(0x10000));
	xavix2_machine_t *machine = (xavix2_machine_t *)calloc(1, sizeof(*machine));
	CHECK(rom && machine);
	CHECK(xavix2_machine_init(machine, rom, UINT32_C(0x10000)));

	/* Instruction fetch uses the low program image, not low data RAM. */
	machine->program_ram[0] = 0xfc;
	machine->low_ram[0] = 0xff;
	machine->cpu.pc = 0;
	CHECK(xavix2_cpu_execute(&machine->cpu, 1) == 1);
	CHECK(machine->cpu.pc == 1);
	CHECK(machine->cpu.unimplemented_count == 0);

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

	free(machine);
	free(rom);
	puts("xavix2 machine tests passed");
	return 0;
}
