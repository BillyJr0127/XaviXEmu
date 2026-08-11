// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "xavix2_cpu.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct test_bus
{
	uint8_t rom[256];
} test_bus;

static uint8_t test_read(void *opaque, uint32_t address)
{
	test_bus *bus = (test_bus *)opaque;
	if (address >= UINT32_C(0x40000000) && address < UINT32_C(0x40000100))
		return bus->rom[address - UINT32_C(0x40000000)];
	return 0;
}

static void test_write(void *opaque, uint32_t address, uint8_t data)
{
	(void)opaque;
	(void)address;
	(void)data;
}

int main(void)
{
	test_bus bus;
	xavix2_cpu_t cpu;

	memset(&bus, 0, sizeof(bus));
	/* jmp 0x40000020 */
	bus.rom[0] = 0x08;
	bus.rom[1] = 0x00;
	bus.rom[2] = 0x00;
	bus.rom[3] = 0x20;
	/* Carry clear/set, zero clear/set, then unimplemented 0xff. */
	bus.rom[0x20] = 0xf0;
	bus.rom[0x21] = 0xf1;
	bus.rom[0x22] = 0xf6;
	bus.rom[0x23] = 0xf7;
	bus.rom[0x24] = 0xff;

	xavix2_cpu_init(&cpu, test_read, test_write, &bus);
	assert(cpu.pc == UINT32_C(0x40000000));
	assert(xavix2_cpu_execute(&cpu, 4) == 4);
	assert(cpu.pc == UINT32_C(0x40000020));
	assert(cpu.total_instructions == 1);

	assert(xavix2_cpu_execute(&cpu, 2) == 2);
	assert(cpu.pc == UINT32_C(0x40000022));
	assert((cpu.hr[4] & 4) != 0);

	cpu.hr[4] |= 1;
	assert(xavix2_cpu_execute(&cpu, 1) == 1);
	assert((cpu.hr[4] & 1) == 0);
	assert(xavix2_cpu_execute(&cpu, 1) == 1);
	assert((cpu.hr[4] & 1) != 0);

	assert(xavix2_cpu_execute(&cpu, 1) == 1);
	assert(cpu.first_unimplemented_pc == UINT32_C(0x40000024));
	assert(cpu.first_unimplemented_opcode == 0xff);
	assert(cpu.unimplemented_count == 1);

	puts("xavix2 CPU tests passed");
	return 0;
}
