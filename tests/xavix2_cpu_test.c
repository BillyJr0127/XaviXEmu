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
	/* Carry clear/set, overflow clear/set, then unimplemented 0xff. */
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

	cpu.hr[4] |= 8;
	CHECK(xavix2_cpu_execute(&cpu, 1) == 1);
	CHECK((cpu.hr[4] & 8) == 0);
	CHECK(xavix2_cpu_execute(&cpu, 1) == 1);
	CHECK((cpu.hr[4] & 8) != 0);

	CHECK(xavix2_cpu_execute(&cpu, 1) == 1);
	CHECK(cpu.first_unimplemented_pc == UINT32_C(0x40000024));
	CHECK(cpu.first_unimplemented_opcode == 0xff);
	CHECK(cpu.unimplemented_count == 1);

	/* The 0x06/0x07 load-immediate form has a signed 19-bit operand.  These
	 * are the real Dragon Ball cursor-clamp constants; decoding them as a
	 * signed 22-bit value pins the tracked cursor outside every menu target. */
	memset(&bus, 0, sizeof(bus));
	bus.rom[0] = 0x06;
	bus.rom[1] = 0x47;
	bus.rom[2] = 0xee;
	bus.rom[3] = 0x80;
	bus.rom[4] = 0x06;
	bus.rom[5] = 0x47;
	bus.rom[6] = 0xf3;
	bus.rom[7] = 0x80;
	xavix2_cpu_init(&cpu, test_read, test_write, &bus);
	CHECK(xavix2_cpu_execute(&cpu, 4) == 4);
	CHECK(cpu.r[1] == UINT32_C(0xffffee80));
	CHECK(xavix2_cpu_execute(&cpu, 4) == 4);
	CHECK(cpu.r[1] == UINT32_C(0xfffff380));

	/* Moving a hardware arithmetic result to a general register updates N/Z.
	 * Naruto's angle normalizer relies on C9 43 setting N from HR3 before a
	 * branch; retaining the flags from an older compare adds a false half-turn. */
	memset(&bus, 0, sizeof(bus));
	bus.rom[0] = 0xc9;
	bus.rom[1] = 0x43; /* r5 = HR3 */
	xavix2_cpu_init(&cpu, test_read, test_write, &bus);
	cpu.hr[3] = UINT32_C(0xffffffff);
	cpu.hr[4] = 1 | 4 | 8;
	CHECK(xavix2_cpu_execute(&cpu, 2) == 2);
	CHECK(cpu.r[5] == UINT32_C(0xffffffff));
	CHECK((cpu.hr[4] & 15) == 2);

	cpu.pc = UINT32_C(0x40000000);
	cpu.hr[3] = 0;
	cpu.hr[4] = 2 | 4 | 8;
	CHECK(xavix2_cpu_execute(&cpu, 2) == 2);
	CHECK(cpu.r[5] == 0);
	CHECK((cpu.hr[4] & 15) == 1);

	/* The 64-bit B4/B5 product is signed.  Naruto's fixed-point sine helper
	 * multiplies a positive radius by a negative ROM-table entry with this
	 * exact form and consumes both HR0 and HR1. */
	memset(&bus, 0, sizeof(bus));
	bus.rom[0] = 0xb4;
	bus.rom[1] = 0xe0; /* HR1:HR0 = r3 * r4 */
	xavix2_cpu_init(&cpu, test_read, test_write, &bus);
	cpu.r[3] = UINT32_C(0x00a00000);
	cpu.r[4] = UINT32_C(0xffffeebc);
	CHECK(xavix2_cpu_execute(&cpu, 2) == 2);
	{
		uint64_t product = (uint64_t)((int64_t)(int32_t)cpu.r[3] *
			(int32_t)cpu.r[4]);
		CHECK(cpu.hr[0] == (uint32_t)product);
		CHECK(cpu.hr[1] == (uint32_t)(product >> 32));
	}

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
