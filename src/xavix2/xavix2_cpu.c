// SPDX-License-Identifier: BSD-3-Clause
// MAME-derived portions copyright-holders: Olivier Galibert, Nathan Gilbert
// XaviXEmu port and modifications:
// Copyright (c) 2026 Billy Jr. and contributors

#include "xavix2_cpu.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

enum
{
	F_Z = 1,
	F_N = 2,
	F_C = 4,
	F_V = 8,
	F_MASK = 15,
	F_I = 16
};

static const uint8_t BYTES_PER_OPCODE[8] = { 4, 3, 3, 2, 2, 2, 2, 1 };

static unsigned r1(uint32_t opcode) { return (opcode >> 22) & 7; }
static unsigned r2(uint32_t opcode) { return (opcode >> 19) & 7; }
static unsigned r3(uint32_t opcode) { return (opcode >> 16) & 7; }
static uint32_t val24u(uint32_t opcode) { return opcode & UINT32_C(0x00ffffff); }
static uint32_t val22h(uint32_t opcode) { return opcode << 10; }
static uint32_t val19s(uint32_t opcode) { return opcode & UINT32_C(0x40000) ? opcode | UINT32_C(0xfff80000) : opcode & UINT32_C(0x7ffff); }
static uint32_t val19u(uint32_t opcode) { return opcode & UINT32_C(0x0007ffff); }
static uint32_t val16s(uint32_t opcode) { return (uint32_t)(int32_t)(int16_t)(opcode >> 8); }
static uint32_t val14h(uint32_t opcode) { return (opcode << 10) & UINT32_C(0xfffc0000); }
static uint32_t val14s(uint32_t opcode) { return opcode & UINT32_C(0x200000) ? (opcode >> 8) | UINT32_C(0xffffc000) : (opcode >> 8) & UINT32_C(0x3fff); }
static uint32_t val11s(uint32_t opcode) { return opcode & UINT32_C(0x40000) ? (opcode >> 8) | UINT32_C(0xfffff800) : (opcode >> 8) & UINT32_C(0x7ff); }
static uint32_t val11u(uint32_t opcode) { return (opcode >> 8) & UINT32_C(0x000007ff); }
static uint32_t val8s(uint32_t opcode) { return (uint32_t)(int32_t)(int8_t)(opcode >> 16); }
static uint32_t val6u(uint32_t opcode) { return (opcode >> 16) & UINT32_C(0x3f); }
static uint32_t val6s(uint32_t opcode) { return opcode & UINT32_C(0x200000) ? (opcode >> 16) | UINT32_C(0xffffffc0) : (opcode >> 16) & UINT32_C(0x3f); }
static uint32_t val3u(uint32_t opcode) { return (opcode >> 16) & 7; }
static uint32_t val3s(uint32_t opcode) { return opcode & UINT32_C(0x40000) ? (opcode >> 16) | UINT32_C(0xfffffff8) : (opcode >> 16) & 7; }

static uint8_t read8(xavix2_cpu_t *cpu, uint32_t address)
{
	return cpu->read8 ? cpu->read8(cpu->opaque, address) : 0;
}

static uint8_t fetch8(xavix2_cpu_t *cpu, uint32_t address)
{
	return cpu->fetch8 ? cpu->fetch8(cpu->opaque, address) : read8(cpu, address);
}

static uint16_t read16(xavix2_cpu_t *cpu, uint32_t address)
{
	return (uint16_t)(read8(cpu, address) | ((uint16_t)read8(cpu, address + 1) << 8));
}

static uint32_t read32(xavix2_cpu_t *cpu, uint32_t address)
{
	return (uint32_t)read8(cpu, address) |
		((uint32_t)read8(cpu, address + 1) << 8) |
		((uint32_t)read8(cpu, address + 2) << 16) |
		((uint32_t)read8(cpu, address + 3) << 24);
}

static void write8(xavix2_cpu_t *cpu, uint32_t address, uint8_t data)
{
	if (cpu->write8)
		cpu->write8(cpu->opaque, address, data);
}

static void write16(xavix2_cpu_t *cpu, uint32_t address, uint16_t data)
{
	write8(cpu, address, (uint8_t)data);
	write8(cpu, address + 1, (uint8_t)(data >> 8));
}

static void write32(xavix2_cpu_t *cpu, uint32_t address, uint32_t data)
{
	write8(cpu, address, (uint8_t)data);
	write8(cpu, address + 1, (uint8_t)(data >> 8));
	write8(cpu, address + 2, (uint8_t)(data >> 16));
	write8(cpu, address + 3, (uint8_t)(data >> 24));
}

static uint32_t do_add(xavix2_cpu_t *cpu, uint32_t v1, uint32_t v2)
{
	uint32_t r = v1 + v2;
	uint32_t f = 0;
	if (!r) f |= F_Z;
	if (r & UINT32_C(0x80000000)) f |= F_N;
	if (((v1 & v2) | ((~r) & (v1 | v2))) & UINT32_C(0x80000000)) f |= F_C;
	if (((v1 ^ r) & (v2 ^ r)) & UINT32_C(0x80000000)) f |= F_V;
	cpu->hr[4] = (cpu->hr[4] & ~F_MASK) | f;
	return r;
}

static uint32_t do_sub(xavix2_cpu_t *cpu, uint32_t v1, uint32_t v2)
{
	uint32_t r = v1 - v2;
	uint32_t f = 0;
	if (!r) f |= F_Z;
	if (r & UINT32_C(0x80000000)) f |= F_N;
	if (((v2 & r) | ((~v1) & (v2 | r))) & UINT32_C(0x80000000)) f |= F_C;
	if (((v1 ^ v2) & (v1 ^ r)) & UINT32_C(0x80000000)) f |= F_V;
	cpu->hr[4] = (cpu->hr[4] & ~F_MASK) | f;
	return r;
}

static uint32_t snz(xavix2_cpu_t *cpu, uint32_t r)
{
	uint32_t f = !r ? F_Z : (r & UINT32_C(0x80000000) ? F_N : 0);
	cpu->hr[4] = (cpu->hr[4] & ~F_MASK) | f;
	return r;
}

static uint32_t do_lsl(xavix2_cpu_t *cpu, uint32_t v1, uint32_t shift)
{
	uint32_t r;
	uint32_t f;
	if (!shift)
	{
		f = v1 ? (v1 & UINT32_C(0x80000000) ? F_N : 0) : F_Z;
		cpu->hr[4] = (cpu->hr[4] & ~F_MASK) | f;
		return v1;
	}
	if (shift < 32)
	{
		r = v1 << shift;
		f = r ? (r & UINT32_C(0x80000000) ? F_N : 0) : F_Z;
		if (v1 & (UINT32_C(1) << (32 - shift))) f |= F_C;
		cpu->hr[4] = (cpu->hr[4] & ~F_MASK) | f;
		return r;
	}
	cpu->hr[4] = (cpu->hr[4] & ~F_MASK) |
		(shift == 32 && (v1 & 1) ? F_C | F_Z : F_Z);
	return 0;
}

static uint32_t do_lsr(xavix2_cpu_t *cpu, uint32_t v1, uint32_t shift)
{
	uint32_t r;
	uint32_t f;
	if (!shift)
	{
		f = v1 ? (v1 & UINT32_C(0x80000000) ? F_N : 0) : F_Z;
		cpu->hr[4] = (cpu->hr[4] & ~F_MASK) | f;
		return v1;
	}
	if (shift < 32)
	{
		r = v1 >> shift;
		f = r ? 0 : F_Z;
		if (v1 & (UINT32_C(1) << (shift - 1))) f |= F_C;
		cpu->hr[4] = (cpu->hr[4] & ~F_MASK) | f;
		return r;
	}
	cpu->hr[4] = (cpu->hr[4] & ~F_MASK) |
		(shift == 32 && (v1 & UINT32_C(0x80000000)) ? F_C | F_Z : F_Z);
	return 0;
}

static uint32_t do_asr(xavix2_cpu_t *cpu, uint32_t v1, uint32_t shift)
{
	uint32_t r;
	uint32_t f;
	if (!shift)
	{
		f = v1 ? (v1 & UINT32_C(0x80000000) ? F_N : 0) : F_Z;
		cpu->hr[4] = (cpu->hr[4] & ~F_MASK) | f;
		return v1;
	}
	if (shift < 32)
	{
		r = (uint32_t)((int32_t)v1 >> shift);
		f = r ? (r & UINT32_C(0x80000000) ? F_N : 0) : F_Z;
		if (v1 & (UINT32_C(1) << (shift - 1))) f |= F_C;
		cpu->hr[4] = (cpu->hr[4] & ~F_MASK) | f;
		return r;
	}
	if (v1 & UINT32_C(0x80000000))
	{
		cpu->hr[4] = (cpu->hr[4] & ~F_MASK) | F_C;
		return UINT32_MAX;
	}
	cpu->hr[4] = (cpu->hr[4] & ~F_MASK) | F_Z;
	return 0;
}

static uint32_t check_interrupt(xavix2_cpu_t *cpu, uint32_t current_pc)
{
	if (cpu->interrupt_line && ((cpu->hr[4] & F_I) || cpu->waiting))
	{
		cpu->ilr1 = cpu->waiting ? current_pc + 1 : current_pc;
		cpu->waiting = 0;
		cpu->enable_interrupt_delay = 0;
		cpu->if1 = (uint8_t)cpu->hr[4];
		cpu->hr[4] &= ~F_I;
		cpu->interrupt_count++;
		if (cpu->interrupt_ack)
			cpu->interrupt_ack(cpu->interrupt_ack_opaque);
		return UINT32_C(0x40000010);
	}
	return current_pc;
}

void xavix2_cpu_init(xavix2_cpu_t *cpu, xavix2_read8_fn host_read8,
	xavix2_write8_fn host_write8, void *opaque)
{
	memset(cpu, 0, sizeof(*cpu));
	cpu->read8 = host_read8;
	cpu->write8 = host_write8;
	cpu->opaque = opaque;
	xavix2_cpu_reset(cpu);
}

void xavix2_cpu_reset(xavix2_cpu_t *cpu)
{
	xavix2_read8_fn host_read8 = cpu->read8;
	xavix2_read8_fn host_fetch8 = cpu->fetch8;
	xavix2_write8_fn host_write8 = cpu->write8;
	void *opaque = cpu->opaque;
	xavix2_interrupt_ack_fn interrupt_ack = cpu->interrupt_ack;
	void *interrupt_ack_opaque = cpu->interrupt_ack_opaque;
	xavix2_trace_fn trace = cpu->trace;
	void *trace_opaque = cpu->trace_opaque;
	memset(cpu, 0, sizeof(*cpu));
	cpu->read8 = host_read8;
	cpu->fetch8 = host_fetch8;
	cpu->write8 = host_write8;
	cpu->opaque = opaque;
	cpu->interrupt_ack = interrupt_ack;
	cpu->interrupt_ack_opaque = interrupt_ack_opaque;
	cpu->trace = trace;
	cpu->trace_opaque = trace_opaque;
	cpu->pc = UINT32_C(0x40000000);
}

void xavix2_cpu_set_fetch(xavix2_cpu_t *cpu, xavix2_read8_fn host_fetch8)
{
	cpu->fetch8 = host_fetch8;
}

void xavix2_cpu_set_interrupt(xavix2_cpu_t *cpu, int asserted)
{
	cpu->interrupt_line = asserted != 0;
}

void xavix2_cpu_set_interrupt_ack(xavix2_cpu_t *cpu,
	xavix2_interrupt_ack_fn acknowledge, void *opaque)
{
	cpu->interrupt_ack = acknowledge;
	cpu->interrupt_ack_opaque = opaque;
}

static void record_unimplemented(xavix2_cpu_t *cpu, uint32_t pc, uint8_t opcode)
{
	if (!cpu->unimplemented_count)
	{
		cpu->first_unimplemented_pc = pc;
		cpu->first_unimplemented_opcode = opcode;
	}
	cpu->unimplemented_count++;
}

uint32_t xavix2_cpu_execute(xavix2_cpu_t *cpu, uint32_t cycle_budget)
{
	uint32_t consumed = 0;
	if (!cycle_budget || cpu->waiting)
	{
		if (cpu->waiting)
			cpu->pc = check_interrupt(cpu, cpu->pc);
		if (!cycle_budget || cpu->waiting)
			return 0;
	}
	if (!cpu->enable_interrupt_delay)
		cpu->pc = check_interrupt(cpu, cpu->pc);

	while (consumed < cycle_budget && !cpu->waiting)
	{
		uint32_t opcode;
		uint32_t instruction_pc;
		uint32_t npc;
		uint8_t first;
		uint8_t bytes;
		uint8_t i;

		if (cpu->enable_interrupt_delay)
		{
			cpu->enable_interrupt_delay--;
			if (!cpu->enable_interrupt_delay)
				cpu->pc = check_interrupt(cpu, cpu->pc);
		}

		instruction_pc = cpu->pc;
		first = fetch8(cpu, cpu->pc);
		opcode = (uint32_t)first << 24;
		bytes = BYTES_PER_OPCODE[first >> 5];
		npc = cpu->pc + bytes;
		for (i = 1; i < bytes; ++i)
			opcode |= (uint32_t)fetch8(cpu, cpu->pc + i) << (24 - 8 * i);
		consumed += bytes;
		cpu->total_cycles += bytes;
		cpu->total_instructions++;
		if (cpu->trace)
			cpu->trace(cpu->trace_opaque, cpu, instruction_pc, opcode, bytes);

		switch (first)
		{
		case 0x00: case 0x01: cpu->r[r1(opcode)] = do_add(cpu, cpu->r[r2(opcode)], val19s(opcode)); break;
		case 0x02: case 0x03: cpu->r[r1(opcode)] = val22h(opcode); break;
		case 0x04: case 0x05: cpu->r[r1(opcode)] = do_sub(cpu, cpu->r[r2(opcode)], val19s(opcode)); break;
		/* Unlike the add/subtract forms, load-immediate leaves the r2 field
		 * outside the value.  Dragon Ball uses 06 47 EE 80 and 06 47 F3 80
		 * for -0x1180 and -0x0c80 cursor bounds; treating all 22 low bits as
		 * the immediate turns both into large positive values. */
		case 0x06: case 0x07: cpu->r[r1(opcode)] = val19s(opcode); break;
		case 0x08: npc = val24u(opcode) | (cpu->pc & UINT32_C(0xff000000)); break;
		case 0x09: cpu->r[7] = npc; npc = val24u(opcode) | (cpu->pc & UINT32_C(0xff000000)); break;
		case 0x0a: case 0x0b: cpu->r[r1(opcode)] = snz(cpu, cpu->r[r2(opcode)] & val19u(opcode)); break;
		case 0x0c: case 0x0d: cpu->r[r1(opcode)] = snz(cpu, cpu->r[r2(opcode)] | val19u(opcode)); break;
		case 0x0e: case 0x0f: cpu->r[r1(opcode)] = snz(cpu, cpu->r[r2(opcode)] ^ val19u(opcode)); break;

		case 0x10: case 0x11: cpu->r[r1(opcode)] = (uint32_t)(int32_t)(int8_t)read8(cpu, cpu->r[r2(opcode)] + val19s(opcode)); break;
		case 0x12: case 0x13: cpu->r[r1(opcode)] = read8(cpu, cpu->r[r2(opcode)] + val19s(opcode)); break;
		case 0x14: case 0x15: cpu->r[r1(opcode)] = (uint32_t)(int32_t)(int16_t)read16(cpu, cpu->r[r2(opcode)] + val19s(opcode)); break;
		case 0x16: case 0x17: cpu->r[r1(opcode)] = read16(cpu, cpu->r[r2(opcode)] + val19s(opcode)); break;
		case 0x18: case 0x19: cpu->r[r1(opcode)] = read32(cpu, cpu->r[r2(opcode)] + val19s(opcode)); break;
		case 0x1a: case 0x1b: write8(cpu, cpu->r[r2(opcode)] + val19s(opcode), (uint8_t)cpu->r[r1(opcode)]); break;
		case 0x1c: case 0x1d: write16(cpu, cpu->r[r2(opcode)] + val19s(opcode), (uint16_t)cpu->r[r1(opcode)]); break;
		case 0x1e: case 0x1f: write32(cpu, cpu->r[r2(opcode)] + val19s(opcode), cpu->r[r1(opcode)]); break;

		case 0x20: case 0x21: cpu->r[r1(opcode)] = do_add(cpu, cpu->r[r2(opcode)], val11s(opcode)); break;
		case 0x22: case 0x23: cpu->r[r1(opcode)] = val14h(opcode); break;
		case 0x24: case 0x25: cpu->r[r1(opcode)] = do_sub(cpu, cpu->r[r2(opcode)], val11s(opcode)); break;
		case 0x26: case 0x27: (void)do_sub(cpu, cpu->r[r1(opcode)], val14s(opcode)); break;
		case 0x28: npc = cpu->pc + val16s(opcode); break;
		case 0x29: cpu->r[7] = npc; npc = cpu->pc + val16s(opcode); break;
		case 0x2a: case 0x2b: cpu->r[r1(opcode)] = snz(cpu, cpu->r[r2(opcode)] & val11u(opcode)); break;
		case 0x2c: case 0x2d: cpu->r[r1(opcode)] = snz(cpu, cpu->r[r2(opcode)] | val11u(opcode)); break;
		case 0x2e: case 0x2f: cpu->r[r1(opcode)] = snz(cpu, cpu->r[r2(opcode)] ^ val11u(opcode)); break;

		case 0x30: case 0x31: cpu->r[r1(opcode)] = (uint32_t)(int32_t)(int8_t)read8(cpu, cpu->r[6] + val14s(opcode)); break;
		case 0x32: case 0x33: cpu->r[r1(opcode)] = read8(cpu, cpu->r[6] + val14s(opcode)); break;
		case 0x34: case 0x35: cpu->r[r1(opcode)] = (uint32_t)(int32_t)(int16_t)read16(cpu, cpu->r[6] + val14s(opcode)); break;
		case 0x36: case 0x37: cpu->r[r1(opcode)] = read16(cpu, cpu->r[6] + val14s(opcode)); break;
		case 0x38: case 0x39: cpu->r[r1(opcode)] = read32(cpu, cpu->r[6] + val14s(opcode)); break;
		case 0x3a: case 0x3b: write8(cpu, cpu->r[6] + val14s(opcode), (uint8_t)cpu->r[r1(opcode)]); break;
		case 0x3c: case 0x3d: write16(cpu, cpu->r[6] + val14s(opcode), (uint16_t)cpu->r[r1(opcode)]); break;
		case 0x3e: case 0x3f: write32(cpu, cpu->r[6] + val14s(opcode), cpu->r[r1(opcode)]); break;

		case 0x40: case 0x41: cpu->r[r1(opcode)] = (uint32_t)(int32_t)(int8_t)read8(cpu, cpu->r[r2(opcode)] + val11s(opcode)); break;
		case 0x42: case 0x43: cpu->r[r1(opcode)] = read8(cpu, cpu->r[r2(opcode)] + val11s(opcode)); break;
		case 0x44: case 0x45: cpu->r[r1(opcode)] = (uint32_t)(int32_t)(int16_t)read16(cpu, cpu->r[r2(opcode)] + val11s(opcode)); break;
		case 0x46: case 0x47: cpu->r[r1(opcode)] = read16(cpu, cpu->r[r2(opcode)] + val11s(opcode)); break;
		case 0x48: case 0x49: cpu->r[r1(opcode)] = read32(cpu, cpu->r[r2(opcode)] + val11s(opcode)); break;
		case 0x4a: case 0x4b: write8(cpu, cpu->r[r2(opcode)] + val11s(opcode), (uint8_t)cpu->r[r1(opcode)]); break;
		case 0x4c: case 0x4d: write16(cpu, cpu->r[r2(opcode)] + val11s(opcode), (uint16_t)cpu->r[r1(opcode)]); break;
		case 0x4e: case 0x4f: write32(cpu, cpu->r[r2(opcode)] + val11s(opcode), cpu->r[r1(opcode)]); break;

		case 0x50: case 0x51: cpu->r[r1(opcode)] = (uint32_t)(int32_t)(int8_t)read8(cpu, val14s(opcode)); break;
		case 0x52: case 0x53: cpu->r[r1(opcode)] = read8(cpu, val14s(opcode)); break;
		case 0x54: case 0x55: cpu->r[r1(opcode)] = (uint32_t)(int32_t)(int16_t)read16(cpu, val14s(opcode)); break;
		case 0x56: case 0x57: cpu->r[r1(opcode)] = read16(cpu, val14s(opcode)); break;
		case 0x58: case 0x59: cpu->r[r1(opcode)] = read32(cpu, val14s(opcode)); break;
		case 0x5a: case 0x5b: write8(cpu, val14s(opcode), (uint8_t)cpu->r[r1(opcode)]); break;
		case 0x5c: case 0x5d: write16(cpu, val14s(opcode), (uint16_t)cpu->r[r1(opcode)]); break;
		case 0x5e: case 0x5f: write32(cpu, val14s(opcode), cpu->r[r1(opcode)]); break;

		case 0x60: case 0x61: cpu->r[r1(opcode)] = do_add(cpu, cpu->r[r1(opcode)], val6s(opcode)); break;
		case 0x62: case 0x63: cpu->r[r1(opcode)] = val6s(opcode); break;
		case 0x64: case 0x65: cpu->r[r1(opcode)] = do_sub(cpu, cpu->r[r1(opcode)], val6s(opcode)); break;
		case 0x66: case 0x67: (void)do_sub(cpu, cpu->r[r1(opcode)], val6s(opcode)); break;
		case 0x68: case 0x69: record_unimplemented(cpu, instruction_pc, first); break;
		case 0x6a: case 0x6b: cpu->r[r1(opcode)] = do_asr(cpu, cpu->r[r2(opcode)], val3u(opcode)); break;
		case 0x6c: case 0x6d: cpu->r[r1(opcode)] = do_lsr(cpu, cpu->r[r2(opcode)], val3u(opcode)); break;
		case 0x6e: case 0x6f: cpu->r[r1(opcode)] = do_lsl(cpu, cpu->r[r2(opcode)], val3u(opcode)); break;

		case 0x70: case 0x71: cpu->r[r1(opcode)] = (uint32_t)(int32_t)(int8_t)read8(cpu, cpu->r[6] + val6s(opcode)); break;
		case 0x72: case 0x73: cpu->r[r1(opcode)] = read8(cpu, cpu->r[6] + val6s(opcode)); break;
		case 0x74: case 0x75: cpu->r[r1(opcode)] = (uint32_t)(int32_t)(int16_t)read16(cpu, cpu->r[6] + val6s(opcode)); break;
		case 0x76: case 0x77: cpu->r[r1(opcode)] = read16(cpu, cpu->r[6] + val6s(opcode)); break;
		case 0x78: case 0x79: cpu->r[r1(opcode)] = read32(cpu, cpu->r[6] + val6s(opcode)); break;
		case 0x7a: case 0x7b: write8(cpu, cpu->r[6] + val6s(opcode), (uint8_t)cpu->r[r1(opcode)]); break;
		case 0x7c: case 0x7d: write16(cpu, cpu->r[6] + val6s(opcode), (uint16_t)cpu->r[r1(opcode)]); break;
		case 0x7e: case 0x7f: write32(cpu, cpu->r[6] + val6s(opcode), cpu->r[r1(opcode)]); break;

		case 0x80: case 0x81: cpu->r[r1(opcode)] = do_add(cpu, cpu->r[r2(opcode)], cpu->r[r3(opcode)]); break;
		case 0x82: case 0x83: record_unimplemented(cpu, instruction_pc, first); break;
		case 0x84: case 0x85: cpu->r[r1(opcode)] = do_sub(cpu, cpu->r[r2(opcode)], cpu->r[r3(opcode)]); break;
		case 0x86: case 0x87: record_unimplemented(cpu, instruction_pc, first); break;
		case 0x88: npc = cpu->r[r2(opcode)]; break;
		case 0x89: record_unimplemented(cpu, instruction_pc, first); break;
		case 0x8a: case 0x8b: cpu->r[r1(opcode)] = snz(cpu, cpu->r[r2(opcode)] & cpu->r[r3(opcode)]); break;
		case 0x8c: case 0x8d: cpu->r[r1(opcode)] = snz(cpu, cpu->r[r2(opcode)] | cpu->r[r3(opcode)]); break;
		case 0x8e: case 0x8f: cpu->r[r1(opcode)] = snz(cpu, cpu->r[r2(opcode)] ^ cpu->r[r3(opcode)]); break;

		case 0x90: case 0x91: cpu->r[r1(opcode)] = (uint32_t)(int32_t)(int8_t)read8(cpu, cpu->r[r2(opcode)] + val3s(opcode)); break;
		case 0x92: case 0x93: cpu->r[r1(opcode)] = read8(cpu, cpu->r[r2(opcode)] + val3s(opcode)); break;
		case 0x94: case 0x95: cpu->r[r1(opcode)] = (uint32_t)(int32_t)(int16_t)read16(cpu, cpu->r[r2(opcode)] + val3s(opcode)); break;
		case 0x96: case 0x97: cpu->r[r1(opcode)] = read16(cpu, cpu->r[r2(opcode)] + val3s(opcode)); break;
		case 0x98: case 0x99: cpu->r[r1(opcode)] = read32(cpu, cpu->r[r2(opcode)] + val3s(opcode)); break;
		case 0x9a: case 0x9b: write8(cpu, cpu->r[r2(opcode)] + val3s(opcode), (uint8_t)cpu->r[r1(opcode)]); break;
		case 0x9c: case 0x9d: write16(cpu, cpu->r[r2(opcode)] + val3s(opcode), (uint16_t)cpu->r[r1(opcode)]); break;
		case 0x9e: case 0x9f: write32(cpu, cpu->r[r2(opcode)] + val3s(opcode), cpu->r[r1(opcode)]); break;

		case 0xa0: case 0xa1: cpu->r[r1(opcode)] = ~cpu->r[r2(opcode)]; break;
		case 0xa2: case 0xa3: cpu->r[r1(opcode)] = cpu->r[r2(opcode)]; break;
		case 0xa4: case 0xa5: cpu->r[r1(opcode)] = 0U - cpu->r[r2(opcode)]; break;
		case 0xa6: case 0xa7: (void)do_sub(cpu, cpu->r[r1(opcode)], cpu->r[r2(opcode)]); break;
		case 0xa8: cpu->r[7] = npc; npc = cpu->r[r2(opcode)]; break;
		case 0xa9: record_unimplemented(cpu, instruction_pc, first); break;
		case 0xaa: case 0xab: cpu->r[r1(opcode)] = do_asr(cpu, cpu->r[r2(opcode)], cpu->r[r3(opcode)]); break;
		case 0xac: case 0xad: cpu->r[r1(opcode)] = do_lsr(cpu, cpu->r[r2(opcode)], cpu->r[r3(opcode)]); break;
		case 0xae: case 0xaf: cpu->r[r1(opcode)] = do_lsl(cpu, cpu->r[r2(opcode)], cpu->r[r3(opcode)]); break;

		case 0xb0: case 0xb1: cpu->hr[0] = cpu->r[r1(opcode)] * cpu->r[r2(opcode)]; break;
		case 0xb2: case 0xb3: cpu->hr[0] = (uint32_t)((int64_t)(int32_t)cpu->r[r1(opcode)] * (int32_t)cpu->r[r2(opcode)]); break;
		case 0xb4: case 0xb5:
		{
			/* The firmware fixed-point helper at $54560 feeds this form a
			 * signed sine-table value, then validates that HR1 is the sign
			 * extension of HR0 before extracting the Q16.16 product.  Treating
			 * the operands as unsigned turns every negative half-cycle into an
			 * overflow and breaks object trajectories. */
			uint64_t result = (uint64_t)((int64_t)(int32_t)cpu->r[r1(opcode)] *
				(int32_t)cpu->r[r2(opcode)]);
			cpu->hr[0] = (uint32_t)result;
			cpu->hr[1] = (uint32_t)(result >> 32);
			break;
		}
		case 0xb6: case 0xb7:
		{
			uint64_t result = (uint64_t)((int64_t)(int32_t)cpu->r[r1(opcode)] *
				(int32_t)cpu->r[r2(opcode)]);
			cpu->hr[0] = (uint32_t)result;
			cpu->hr[1] = (uint32_t)(result >> 32);
			break;
		}
		case 0xb8: case 0xb9: case 0xba: case 0xbb: record_unimplemented(cpu, instruction_pc, first); break;
		case 0xbc: case 0xbd:
		{
			int32_t dividend = (int32_t)cpu->r[r1(opcode)];
			int32_t divisor = (int32_t)cpu->r[r2(opcode)];
			if (divisor && !(dividend == INT32_MIN && divisor == -1))
			{
				cpu->hr[2] = (uint32_t)(dividend / divisor);
				cpu->hr[3] = (uint32_t)(dividend % divisor);
			}
			break;
		}
		case 0xbe: case 0xbf:
		{
			uint32_t divisor = cpu->r[r2(opcode)];
			if (divisor)
			{
				cpu->hr[2] = cpu->r[r1(opcode)] / divisor;
				cpu->hr[3] = cpu->r[r1(opcode)] % divisor;
			}
			break;
		}

		case 0xc0: case 0xc1: case 0xc2: case 0xc3: case 0xc4: case 0xc5: case 0xc6: case 0xc7:
			/* MAME currently treats these encodings as no-ops. */ break;
		/* Firmware branches directly on values moved out of the multiply/divide
		 * result registers.  In particular, the angle normalizer at 0x54468
		 * tests the sign of HR3 immediately after C9 43.  Updating N/Z here is
		 * therefore part of the instruction, not a side effect of the preceding
		 * divide. */
		case 0xc8: case 0xc9:
			cpu->r[r1(opcode)] = snz(cpu, cpu->hr[val6u(opcode)]);
			break;
		case 0xca: case 0xcb: cpu->hr[val6u(opcode)] = cpu->r[r1(opcode)]; break;
		case 0xcc: case 0xcd: case 0xce: case 0xcf:
			/* MAME currently treats these encodings as no-ops. */ break;

		case 0xd0: if (cpu->hr[4] & F_V) npc = cpu->pc + val8s(opcode); break;
		case 0xd1: if (cpu->hr[4] & F_C) npc = cpu->pc + val8s(opcode); break;
		case 0xd2: if (cpu->hr[4] & F_Z) npc = cpu->pc + val8s(opcode); break;
		case 0xd3: if ((cpu->hr[4] & F_Z) || (cpu->hr[4] & F_C)) npc = cpu->pc + val8s(opcode); break;
		case 0xd4: if (cpu->hr[4] & F_N) npc = cpu->pc + val8s(opcode); break;
		case 0xd5: npc = cpu->pc + val8s(opcode); break;
		case 0xd6: if (!!(cpu->hr[4] & F_N) != !!(cpu->hr[4] & F_V)) npc = cpu->pc + val8s(opcode); break;
		case 0xd7: if ((cpu->hr[4] & F_Z) || (!!(cpu->hr[4] & F_N) != !!(cpu->hr[4] & F_V))) npc = cpu->pc + val8s(opcode); break;
		case 0xd8: if (!(cpu->hr[4] & F_V)) npc = cpu->pc + val8s(opcode); break;
		case 0xd9: if (!(cpu->hr[4] & F_C)) npc = cpu->pc + val8s(opcode); break;
		case 0xda: if (!(cpu->hr[4] & F_Z)) npc = cpu->pc + val8s(opcode); break;
		case 0xdb: if (!(cpu->hr[4] & F_Z) && !(cpu->hr[4] & F_C)) npc = cpu->pc + val8s(opcode); break;
		case 0xdc: if (!(cpu->hr[4] & F_N)) npc = cpu->pc + val8s(opcode); break;
		case 0xdd: break;
		case 0xde: if (!!(cpu->hr[4] & F_N) == !!(cpu->hr[4] & F_V)) npc = cpu->pc + val8s(opcode); break;
		case 0xdf: if (!(cpu->hr[4] & F_Z) && (!!(cpu->hr[4] & F_N) == !!(cpu->hr[4] & F_V))) npc = cpu->pc + val8s(opcode); break;

		case 0xe0: npc = cpu->r[7]; break;
		case 0xe1: cpu->hr[4] = cpu->if1; npc = cpu->ilr1; break;
		case 0xe2: record_unimplemented(cpu, instruction_pc, first); break;
		case 0xe3: /* rti2 is still unknown in MAME. */ break;
		case 0xe4: case 0xe5: case 0xe6: case 0xe7: case 0xe8: case 0xe9:
		case 0xea: case 0xeb: case 0xec: case 0xed: case 0xee: case 0xef:
			record_unimplemented(cpu, instruction_pc, first); break;

		case 0xf0: cpu->hr[4] &= ~F_C; break;
		case 0xf1: cpu->hr[4] |= F_C; break;
		case 0xf2: cpu->hr[4] &= ~F_Z; break;
		case 0xf3: cpu->hr[4] |= F_Z; break;
		case 0xf4: cpu->hr[4] &= ~F_N; break;
		case 0xf5: cpu->hr[4] |= F_N; break;
		case 0xf6: cpu->hr[4] &= ~F_V; break;
		case 0xf7: cpu->hr[4] |= F_V; break;
		case 0xf8: cpu->hr[4] &= ~F_I; break;
		case 0xf9: cpu->hr[4] |= F_I; cpu->enable_interrupt_delay = 2; break;
		case 0xfa: case 0xfb: record_unimplemented(cpu, instruction_pc, first); break;
		case 0xfc: break;
		case 0xfd: record_unimplemented(cpu, instruction_pc, first); break;
		case 0xfe:
			cpu->waiting = 1;
			npc = check_interrupt(cpu, npc - 1);
			break;
		case 0xff: record_unimplemented(cpu, instruction_pc, first); break;
		}

		cpu->pc = npc;
	}
	return consumed;
}
