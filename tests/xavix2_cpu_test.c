// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "xavix2_cpu.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			fprintf(stderr, "check failed at %s:%d: %s\n", \
				__FILE__, __LINE__, #condition); \
			return 1; \
		} \
	} while (0)

typedef struct test_bus
{
	uint8_t rom[256];
	uint8_t fetch_rom[256];
	unsigned interrupt_ack_count;
} test_bus;

static uint8_t test_read(void *opaque, uint32_t address)
{
	test_bus *bus = (test_bus *)opaque;
	if (address >= UINT32_C(0x40000000) && address < UINT32_C(0x40000100))
		return bus->rom[address - UINT32_C(0x40000000)];
	return 0;
}

static uint8_t test_fetch(void *opaque, uint32_t address)
{
	test_bus *bus = (test_bus *)opaque;
	if (address >= UINT32_C(0x40000000) && address < UINT32_C(0x40000100))
		return bus->fetch_rom[address - UINT32_C(0x40000000)];
	return 0;
}

static void test_interrupt_ack(void *opaque)
{
	test_bus *bus = (test_bus *)opaque;
	bus->interrupt_ack_count++;
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
	CHECK(cpu.pc == UINT32_C(0x40000000));
	CHECK(xavix2_cpu_execute(&cpu, 4) == 4);
	CHECK(cpu.pc == UINT32_C(0x40000020));
	CHECK(cpu.total_instructions == 1);

	CHECK(xavix2_cpu_execute(&cpu, 2) == 2);
	CHECK(cpu.pc == UINT32_C(0x40000022));
	CHECK((cpu.hr[4] & 4) != 0);

	cpu.hr[4] |= 1;
	CHECK(xavix2_cpu_execute(&cpu, 1) == 1);
	CHECK((cpu.hr[4] & 1) == 0);
	CHECK(xavix2_cpu_execute(&cpu, 1) == 1);
	CHECK((cpu.hr[4] & 1) != 0);

	CHECK(xavix2_cpu_execute(&cpu, 1) == 1);
	CHECK(cpu.first_unimplemented_pc == UINT32_C(0x40000024));
	CHECK(cpu.first_unimplemented_opcode == 0xff);
	CHECK(cpu.unimplemented_count == 1);

	/* Instruction fetch can observe program memory independently of data reads. */
	memset(&bus, 0, sizeof(bus));
	bus.rom[0] = 0xff;
	bus.fetch_rom[0] = 0xfc;
	xavix2_cpu_init(&cpu, test_read, test_write, &bus);
	xavix2_cpu_set_fetch(&cpu, test_fetch);
	CHECK(xavix2_cpu_execute(&cpu, 1) == 1);
	CHECK(cpu.pc == UINT32_C(0x40000001));
	CHECK(cpu.unimplemented_count == 0);

	/* Accepting an interrupt notifies the machine before vector execution. */
	xavix2_cpu_set_interrupt_ack(&cpu, test_interrupt_ack, &bus);
	cpu.hr[4] |= 16;
	xavix2_cpu_set_interrupt(&cpu, 1);
	CHECK(xavix2_cpu_execute(&cpu, 1) == 4);
	CHECK(bus.interrupt_ack_count == 1);
	CHECK(cpu.interrupt_count == 1);

	puts("xavix2 CPU tests passed");
	return 0;
}
