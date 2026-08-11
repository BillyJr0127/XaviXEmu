// SPDX-License-Identifier: BSD-3-Clause
// MAME-derived portions copyright-holders: Olivier Galibert, David Haywood
// XaviXEmu port and modifications:
// Copyright (c) 2026 Billy Jr. and contributors
/*
 * Compact SSD 2000 / XaviX CPU interpreter.
 *
 * Instruction semantics and bus ordering follow the BSD-3-Clause MAME
 * m6502, XaviX and XaviX 2000 cores by Olivier Galibert and David Haywood.
 */

#include "xavix_cpu.h"

#include <stddef.h>
#include <string.h>

#define EXT_MASK UINT32_C(0x7fffff)
#define ADDR24_MASK UINT32_C(0xffffff)

static uint8_t host_read(xavix_cpu_t *cpu, xavix_cpu_bus_t bus, uint32_t address)
{
	return cpu->read8 ? cpu->read8(cpu->opaque, bus, address) : 0xff;
}

static void host_write(xavix_cpu_t *cpu, xavix_cpu_bus_t bus, uint32_t address, uint8_t data)
{
	if (cpu->write8)
		cpu->write8(cpu->opaque, bus, address, data);
}

static uint8_t read_low(xavix_cpu_t *cpu, uint16_t address)
{
	return host_read(cpu, XAVIX_CPU_BUS_LOW, address & 0x7fff);
}

static void write_low(xavix_cpu_t *cpu, uint16_t address, uint8_t data)
{
	host_write(cpu, XAVIX_CPU_BUS_LOW, address & 0x7fff, data);
}

static uint8_t read_external(xavix_cpu_t *cpu, uint32_t address)
{
	return host_read(cpu, XAVIX_CPU_BUS_EXTERNAL, address & EXT_MASK);
}

static void write_external(xavix_cpu_t *cpu, uint32_t address, uint8_t data)
{
	host_write(cpu, XAVIX_CPU_BUS_EXTERNAL, address & EXT_MASK, data);
}

static uint8_t read_zero_page(xavix_cpu_t *cpu, uint8_t address)
{
	if (address == 0xfe)
		return cpu->code_bank;
	if (address == 0xff)
		return cpu->data_bank;
	return read_low(cpu, address);
}

static void write_zero_page(xavix_cpu_t *cpu, uint8_t address, uint8_t data)
{
	if (address == 0xfe)
	{
		cpu->code_bank = data;
		return;
	}
	if (address == 0xff)
	{
		cpu->data_bank = data;
		return;
	}
	write_low(cpu, address, data);
}

static uint8_t read_full(xavix_cpu_t *cpu, uint32_t address)
{
	const uint8_t bank = (uint8_t)(address >> 16);
	const uint16_t offset = (uint16_t)address;

	if (bank < 0x80 && offset < 0x8000)
	{
		if (offset < 0x0100)
			return read_zero_page(cpu, (uint8_t)offset);
		return read_low(cpu, offset);
	}
	return read_external(cpu, address);
}

static void write_full(xavix_cpu_t *cpu, uint32_t address, uint8_t data)
{
	const uint8_t bank = (uint8_t)(address >> 16);
	const uint16_t offset = (uint16_t)address;

	if (bank < 0x80 && offset < 0x8000)
	{
		if (offset < 0x0100)
			write_zero_page(cpu, (uint8_t)offset, data);
		else
			write_low(cpu, offset, data);
		return;
	}
	write_external(cpu, address, data);
}

static uint8_t read_data(xavix_cpu_t *cpu, uint16_t address)
{
	return read_full(cpu, ((uint32_t)cpu->data_bank << 16) | address);
}

static void write_data(xavix_cpu_t *cpu, uint16_t address, uint8_t data)
{
	write_full(cpu, ((uint32_t)cpu->data_bank << 16) | address, data);
}

static uint8_t read_stack(xavix_cpu_t *cpu)
{
	return read_low(cpu, (uint16_t)(0x0100 | cpu->s));
}

static void push(xavix_cpu_t *cpu, uint8_t data)
{
	write_low(cpu, (uint16_t)(0x0100 | cpu->s), data);
	cpu->s--;
}

static uint8_t pull(xavix_cpu_t *cpu)
{
	cpu->s++;
	return read_low(cpu, (uint16_t)(0x0100 | cpu->s));
}

static uint8_t read_code_at(xavix_cpu_t *cpu, uint16_t pc)
{
	if (cpu->code_bank < 0x80 && pc < 0x8000)
		return read_low(cpu, pc);
	return read_external(cpu, ((uint32_t)cpu->code_bank << 16) | pc);
}

static uint8_t fetch8(xavix_cpu_t *cpu)
{
	const uint8_t result = read_code_at(cpu, cpu->pc);
	cpu->pc++;
	return result;
}

static uint16_t fetch16(xavix_cpu_t *cpu)
{
	const uint16_t lo = fetch8(cpu);
	return (uint16_t)(lo | ((uint16_t)fetch8(cpu) << 8));
}

static uint8_t read_vector(xavix_cpu_t *cpu, uint16_t address)
{
	return host_read(cpu, XAVIX_CPU_BUS_VECTOR, address);
}

static void set_nz(xavix_cpu_t *cpu, uint8_t value)
{
	cpu->p &= (uint8_t)~(XAVIX_CPU_N | XAVIX_CPU_Z);
	if (!value)
		cpu->p |= XAVIX_CPU_Z;
	else if (value & 0x80)
		cpu->p |= XAVIX_CPU_N;
}

static void op_cmp(xavix_cpu_t *cpu, uint8_t lhs, uint8_t rhs)
{
	const uint16_t result = (uint16_t)lhs - rhs;
	cpu->p &= (uint8_t)~(XAVIX_CPU_N | XAVIX_CPU_Z | XAVIX_CPU_C);
	if (!(uint8_t)result)
		cpu->p |= XAVIX_CPU_Z;
	else if (result & 0x80)
		cpu->p |= XAVIX_CPU_N;
	if (!(result & 0xff00))
		cpu->p |= XAVIX_CPU_C;
}

static void op_bit(xavix_cpu_t *cpu, uint8_t value)
{
	cpu->p &= (uint8_t)~(XAVIX_CPU_N | XAVIX_CPU_V | XAVIX_CPU_Z);
	if (!(cpu->a & value))
		cpu->p |= XAVIX_CPU_Z;
	cpu->p |= value & (XAVIX_CPU_N | XAVIX_CPU_V);
}

static void op_adc(xavix_cpu_t *cpu, uint8_t value)
{
	const uint8_t old_a = cpu->a;
	const uint8_t carry = (cpu->p & XAVIX_CPU_C) ? 1 : 0;

	cpu->p &= (uint8_t)~(XAVIX_CPU_N | XAVIX_CPU_V | XAVIX_CPU_Z | XAVIX_CPU_C);
	if (cpu->p & XAVIX_CPU_D)
	{
		uint8_t al = (uint8_t)((old_a & 0x0f) + (value & 0x0f) + carry);
		uint8_t ah;
		if (al > 9)
			al = (uint8_t)(al + 6);
		ah = (uint8_t)((old_a >> 4) + (value >> 4) + (al > 15));
		if (!(uint8_t)(old_a + value + carry))
			cpu->p |= XAVIX_CPU_Z;
		else if (ah & 8)
			cpu->p |= XAVIX_CPU_N;
		if ((uint8_t)(~(old_a ^ value) & (old_a ^ (uint8_t)(ah << 4)) & 0x80))
			cpu->p |= XAVIX_CPU_V;
		if (ah > 9)
			ah = (uint8_t)(ah + 6);
		if (ah > 15)
			cpu->p |= XAVIX_CPU_C;
		cpu->a = (uint8_t)((ah << 4) | (al & 0x0f));
	}
	else
	{
		const uint16_t sum = (uint16_t)old_a + value + carry;
		cpu->a = (uint8_t)sum;
		set_nz(cpu, cpu->a);
		if ((uint8_t)(~(old_a ^ value) & (old_a ^ cpu->a) & 0x80))
			cpu->p |= XAVIX_CPU_V;
		if (sum & 0xff00)
			cpu->p |= XAVIX_CPU_C;
	}
}

static void op_sbc(xavix_cpu_t *cpu, uint8_t value)
{
	const uint8_t old_a = cpu->a;
	const uint8_t borrow = (cpu->p & XAVIX_CPU_C) ? 0 : 1;
	const uint16_t diff = (uint16_t)old_a - value - borrow;

	cpu->p &= (uint8_t)~(XAVIX_CPU_N | XAVIX_CPU_V | XAVIX_CPU_Z | XAVIX_CPU_C);
	if (!(uint8_t)diff)
		cpu->p |= XAVIX_CPU_Z;
	else if (diff & 0x80)
		cpu->p |= XAVIX_CPU_N;
	if ((old_a ^ value) & (old_a ^ (uint8_t)diff) & 0x80)
		cpu->p |= XAVIX_CPU_V;
	if (!(diff & 0xff00))
		cpu->p |= XAVIX_CPU_C;

	if (cpu->p & XAVIX_CPU_D)
	{
		uint8_t al = (uint8_t)((old_a & 0x0f) - (value & 0x0f) - borrow);
		uint8_t ah = (uint8_t)((old_a >> 4) - (value >> 4) - ((int8_t)al < 0));
		if ((int8_t)al < 0)
			al = (uint8_t)(al - 6);
		if ((int8_t)ah < 0)
			ah = (uint8_t)(ah - 6);
		cpu->a = (uint8_t)((ah << 4) | (al & 0x0f));
	}
	else
	{
		cpu->a = (uint8_t)diff;
	}
}

static uint8_t op_asl(xavix_cpu_t *cpu, uint8_t value)
{
	cpu->p &= (uint8_t)~XAVIX_CPU_C;
	if (value & 0x80)
		cpu->p |= XAVIX_CPU_C;
	value <<= 1;
	set_nz(cpu, value);
	return value;
}

static uint8_t op_lsr(xavix_cpu_t *cpu, uint8_t value)
{
	cpu->p &= (uint8_t)~XAVIX_CPU_C;
	if (value & 1)
		cpu->p |= XAVIX_CPU_C;
	value >>= 1;
	set_nz(cpu, value);
	return value;
}

static uint8_t op_rol(xavix_cpu_t *cpu, uint8_t value)
{
	const uint8_t carry = (cpu->p & XAVIX_CPU_C) ? 1 : 0;
	cpu->p &= (uint8_t)~XAVIX_CPU_C;
	if (value & 0x80)
		cpu->p |= XAVIX_CPU_C;
	value = (uint8_t)((value << 1) | carry);
	set_nz(cpu, value);
	return value;
}

static uint8_t op_ror(xavix_cpu_t *cpu, uint8_t value)
{
	const uint8_t carry = (cpu->p & XAVIX_CPU_C) ? 0x80 : 0;
	cpu->p &= (uint8_t)~XAVIX_CPU_C;
	if (value & 1)
		cpu->p |= XAVIX_CPU_C;
	value = (uint8_t)((value >> 1) | carry);
	set_nz(cpu, value);
	return value;
}

static uint8_t op_asr(xavix_cpu_t *cpu, uint8_t value)
{
	const uint8_t sign = value & 0x40;
	cpu->p &= (uint8_t)~XAVIX_CPU_C;
	if (value & 1)
		cpu->p |= XAVIX_CPU_C;
	value >>= 1;
	if (sign)
		value |= 0x80;
	set_nz(cpu, value);
	return value;
}

static uint16_t addr_zpx(xavix_cpu_t *cpu, uint8_t index)
{
	const uint8_t base = fetch8(cpu);
	(void)read_zero_page(cpu, base);
	return (uint8_t)(base + index);
}

static uint16_t addr_idx(xavix_cpu_t *cpu)
{
	uint8_t pointer = fetch8(cpu);
	(void)read_zero_page(cpu, pointer);
	pointer = (uint8_t)(pointer + cpu->x);
	return (uint16_t)(read_zero_page(cpu, pointer) |
		((uint16_t)read_zero_page(cpu, (uint8_t)(pointer + 1)) << 8));
}

static uint16_t addr_idy_base(xavix_cpu_t *cpu)
{
	const uint8_t pointer = fetch8(cpu);
	return (uint16_t)(read_zero_page(cpu, pointer) |
		((uint16_t)read_zero_page(cpu, (uint8_t)(pointer + 1)) << 8));
}

static int page_crossed(uint16_t base, uint8_t index)
{
	return ((base ^ (uint16_t)(base + index)) & 0xff00) != 0;
}

static void dummy_indexed_read(xavix_cpu_t *cpu, uint16_t base, uint8_t index)
{
	(void)read_data(cpu, (uint16_t)((base & 0xff00) | (uint8_t)(base + index)));
}

enum alu_kind
{
	ALU_ORA = 0,
	ALU_AND = 1,
	ALU_EOR = 2,
	ALU_ADC = 3,
	ALU_STA = 4,
	ALU_LDA = 5,
	ALU_CMP = 6,
	ALU_SBC = 7
};

static void apply_alu(xavix_cpu_t *cpu, unsigned kind, uint8_t value)
{
	switch (kind)
	{
	case ALU_ORA: cpu->a |= value; set_nz(cpu, cpu->a); break;
	case ALU_AND: cpu->a &= value; set_nz(cpu, cpu->a); break;
	case ALU_EOR: cpu->a ^= value; set_nz(cpu, cpu->a); break;
	case ALU_ADC: op_adc(cpu, value); break;
	case ALU_LDA: cpu->a = value; set_nz(cpu, cpu->a); break;
	case ALU_CMP: op_cmp(cpu, cpu->a, value); break;
	case ALU_SBC: op_sbc(cpu, value); break;
	default: break;
	}
}

static int execute_alu_group(xavix_cpu_t *cpu, uint8_t opcode)
{
	const unsigned kind = opcode >> 5;
	const uint8_t mode = opcode & 0x1f;
	uint16_t address = 0;
	uint16_t base;
	uint8_t value = 0;
	int zero_page = 0;
	int cycles;

	switch (mode)
	{
	case 0x01:
		address = addr_idx(cpu);
		cycles = 6;
		break;
	case 0x05:
		address = fetch8(cpu);
		zero_page = 1;
		cycles = 3;
		break;
	case 0x09:
		value = fetch8(cpu);
		cycles = 2;
		if (kind != ALU_STA)
		{
			apply_alu(cpu, kind, value);
			return cycles;
		}
		return cycles;
	case 0x0d:
		address = fetch16(cpu);
		cycles = 4;
		break;
	case 0x11:
		base = addr_idy_base(cpu);
		address = (uint16_t)(base + cpu->y);
		if (kind == ALU_STA)
		{
			/* Matches the XaviX core's zero-page dummy access. */
			(void)read_zero_page(cpu, (uint8_t)address);
			cycles = 6;
		}
		else
		{
			cycles = 5;
			if (page_crossed(base, cpu->y))
			{
				dummy_indexed_read(cpu, base, cpu->y);
				cycles++;
			}
		}
		break;
	case 0x15:
		address = addr_zpx(cpu, cpu->x);
		zero_page = 1;
		cycles = 4;
		break;
	case 0x19:
		base = fetch16(cpu);
		address = (uint16_t)(base + cpu->y);
		if (kind == ALU_STA)
		{
			dummy_indexed_read(cpu, base, cpu->y);
			cycles = 5;
		}
		else
		{
			cycles = 4;
			if (page_crossed(base, cpu->y))
			{
				dummy_indexed_read(cpu, base, cpu->y);
				cycles++;
			}
		}
		break;
	case 0x1d:
		base = fetch16(cpu);
		address = (uint16_t)(base + cpu->x);
		if (kind == ALU_STA)
		{
			dummy_indexed_read(cpu, base, cpu->x);
			cycles = 5;
		}
		else
		{
			cycles = 4;
			if (page_crossed(base, cpu->x))
			{
				dummy_indexed_read(cpu, base, cpu->x);
				cycles++;
			}
		}
		break;
	default:
		return 2;
	}

	if (kind == ALU_STA)
	{
		if (zero_page)
			write_zero_page(cpu, (uint8_t)address, cpu->a);
		else
			write_data(cpu, address, cpu->a);
	}
	else
	{
		value = zero_page ? read_zero_page(cpu, (uint8_t)address) : read_data(cpu, address);
		apply_alu(cpu, kind, value);
	}
	return cycles;
}

static uint8_t apply_shift(xavix_cpu_t *cpu, unsigned kind, uint8_t value)
{
	switch (kind)
	{
	case 0: return op_asl(cpu, value);
	case 1: return op_rol(cpu, value);
	case 2: return op_lsr(cpu, value);
	default: return op_ror(cpu, value);
	}
}

static int execute_shift_group(xavix_cpu_t *cpu, uint8_t opcode)
{
	const unsigned kind = (opcode >> 5) & 3;
	const uint8_t mode = opcode & 0x1f;
	uint16_t address;
	uint16_t base;
	uint8_t value;
	int zero_page = 0;
	int cycles;

	if (mode == 0x0a)
	{
		cpu->a = apply_shift(cpu, kind, cpu->a);
		return 2;
	}
	if (mode == 0x06)
	{
		address = fetch8(cpu);
		zero_page = 1;
		cycles = 5;
	}
	else if (mode == 0x16)
	{
		address = addr_zpx(cpu, cpu->x);
		zero_page = 1;
		cycles = 6;
	}
	else if (mode == 0x0e)
	{
		address = fetch16(cpu);
		cycles = 6;
	}
	else
	{
		base = fetch16(cpu);
		dummy_indexed_read(cpu, base, cpu->x);
		address = (uint16_t)(base + cpu->x);
		cycles = 7;
	}

	value = zero_page ? read_zero_page(cpu, (uint8_t)address) : read_data(cpu, address);
	if (zero_page)
		write_zero_page(cpu, (uint8_t)address, value);
	else
		write_data(cpu, address, value);
	value = apply_shift(cpu, kind, value);
	if (zero_page)
		write_zero_page(cpu, (uint8_t)address, value);
	else
		write_data(cpu, address, value);
	return cycles;
}

static int execute_reg_alu(xavix_cpu_t *cpu, uint8_t opcode)
{
	const unsigned kind = opcode >> 5;
	uint8_t *const regs[4] = { &cpu->j, &cpu->k, &cpu->l, &cpu->m };
	uint8_t *const reg = regs[(opcode >> 2) & 3];

	(void)read_code_at(cpu, cpu->pc);
	if (kind == ALU_STA)
		*reg = cpu->a;
	else
		apply_alu(cpu, kind, *reg);
	return 2;
}

static int execute_pointer_alu(xavix_cpu_t *cpu, uint8_t opcode)
{
	const unsigned kind = opcode >> 5;
	uint32_t *const pointer = (opcode & 0x04) ? &cpu->pb : &cpu->pa;

	if (kind <= ALU_EOR)
		(void)read_code_at(cpu, cpu->pc);
	if (kind == ALU_STA)
		write_full(cpu, *pointer, cpu->a);
	else
		apply_alu(cpu, kind, read_full(cpu, *pointer));
	return kind <= ALU_EOR ? 3 : 2;
}

static int execute_branch(xavix_cpu_t *cpu, int condition)
{
	const int8_t displacement = (int8_t)fetch8(cpu);
	int cycles = 2;
	if (condition)
	{
		const uint16_t old_pc = cpu->pc;
		(void)read_code_at(cpu, old_pc);
		cpu->pc = (uint16_t)(old_pc + displacement);
		cycles++;
		if ((old_pc ^ cpu->pc) & 0xff00)
		{
			(void)read_code_at(cpu, (uint16_t)((old_pc & 0xff00) | (cpu->pc & 0x00ff)));
			cycles++;
		}
	}
	return cycles;
}

static int execute_incdec_memory(xavix_cpu_t *cpu, uint8_t opcode)
{
	const int increment = (opcode & 0x20) != 0;
	const uint8_t mode = opcode & 0x1f;
	uint16_t address;
	uint16_t base;
	uint8_t value;
	int zero_page = 0;
	int cycles;

	if (mode == 0x06)
	{
		address = fetch8(cpu);
		zero_page = 1;
		cycles = 5;
	}
	else if (mode == 0x16)
	{
		address = addr_zpx(cpu, cpu->x);
		zero_page = 1;
		cycles = 6;
	}
	else if (mode == 0x0e)
	{
		address = fetch16(cpu);
		cycles = 6;
	}
	else
	{
		base = fetch16(cpu);
		dummy_indexed_read(cpu, base, cpu->x);
		address = (uint16_t)(base + cpu->x);
		cycles = 7;
	}
	value = zero_page ? read_zero_page(cpu, (uint8_t)address) : read_data(cpu, address);
	if (zero_page)
		write_zero_page(cpu, (uint8_t)address, value);
	else
		write_data(cpu, address, value);
	value = increment ? (uint8_t)(value + 1) : (uint8_t)(value - 1);
	set_nz(cpu, value);
	if (zero_page)
		write_zero_page(cpu, (uint8_t)address, value);
	else
		write_data(cpu, address, value);
	return cycles;
}

static int service_interrupt(xavix_cpu_t *cpu, int nmi)
{
	(void)read_code_at(cpu, cpu->pc);
	push(cpu, cpu->code_bank);
	cpu->code_bank = 0;
	push(cpu, (uint8_t)(cpu->pc >> 8));
	push(cpu, (uint8_t)cpu->pc);
	push(cpu, (uint8_t)((cpu->p | XAVIX_CPU_U) & ~XAVIX_CPU_B));
	cpu->p |= XAVIX_CPU_I | XAVIX_CPU_U | XAVIX_CPU_B;
	if (nmi)
	{
		cpu->pc = (uint16_t)(read_vector(cpu, 0xfffa) |
			((uint16_t)read_vector(cpu, 0xfffb) << 8));
		cpu->nmi_pending = 0;
	}
	else
	{
		cpu->pc = (uint16_t)(read_vector(cpu, 0xfffe) |
			((uint16_t)read_vector(cpu, 0xffff) << 8));
	}
	return 8;
}

static int execute_one(xavix_cpu_t *cpu)
{
	const uint8_t opcode = fetch8(cpu);
	uint16_t address;
	uint16_t base;
	uint8_t value;
	uint8_t old_i;
	uint8_t bank;
	uint8_t lo;
	uint8_t hi;

	/* NMOS group-one ALU/addressing pattern, using XaviX zero-page rules. */
	switch (opcode)
	{
	case 0x01: case 0x05: case 0x09: case 0x0d: case 0x11: case 0x15: case 0x19: case 0x1d:
	case 0x21: case 0x25: case 0x29: case 0x2d: case 0x31: case 0x35: case 0x39: case 0x3d:
	case 0x41: case 0x45: case 0x49: case 0x4d: case 0x51: case 0x55: case 0x59: case 0x5d:
	case 0x61: case 0x65: case 0x69: case 0x6d: case 0x71: case 0x75: case 0x79: case 0x7d:
	case 0x81: case 0x85: case 0x8d: case 0x91: case 0x95: case 0x99: case 0x9d:
	case 0xa1: case 0xa5: case 0xa9: case 0xad: case 0xb1: case 0xb5: case 0xb9: case 0xbd:
	case 0xc1: case 0xc5: case 0xc9: case 0xcd: case 0xd1: case 0xd5: case 0xd9: case 0xdd:
	case 0xe1: case 0xe5: case 0xe9: case 0xed: case 0xf1: case 0xf5: case 0xf9: case 0xfd:
		return execute_alu_group(cpu, opcode);
	default:
		break;
	}

	/* Register and PA/PB variants occupy the former NMOS illegal slots. */
	switch (opcode)
	{
	case 0x03: case 0x07: case 0x0b: case 0x0f:
	case 0x23: case 0x27: case 0x2b: case 0x2f:
	case 0x43: case 0x47: case 0x4b: case 0x4f:
	case 0x63: case 0x67: case 0x6b: case 0x6f:
	case 0x83: case 0x87: case 0x8b: case 0x8f:
	case 0xa3: case 0xa7: case 0xab: case 0xaf:
	case 0xc3: case 0xc7: case 0xcb: case 0xcf:
	case 0xe3: case 0xe7: case 0xeb: case 0xef:
		return execute_reg_alu(cpu, opcode);
	case 0x13: case 0x17: case 0x33: case 0x37:
	case 0x53: case 0x57: case 0x73: case 0x77:
	case 0x93: case 0x97: case 0xb3: case 0xb7:
	case 0xd3: case 0xd7: case 0xf3: case 0xf7:
		return execute_pointer_alu(cpu, opcode);
	default:
		break;
	}

	/* ASL/ROL/LSR/ROR families. */
	switch (opcode)
	{
	case 0x06: case 0x0a: case 0x0e: case 0x16: case 0x1e:
	case 0x26: case 0x2a: case 0x2e: case 0x36: case 0x3e:
	case 0x46: case 0x4a: case 0x4e: case 0x56: case 0x5e:
	case 0x66: case 0x6a: case 0x6e: case 0x76: case 0x7e:
		return execute_shift_group(cpu, opcode);
	case 0xc6: case 0xce: case 0xd6: case 0xde:
	case 0xe6: case 0xee: case 0xf6: case 0xfe:
		return execute_incdec_memory(cpu, opcode);
	default:
		break;
	}

	switch (opcode)
	{
	case 0x00: /* BRK */
		(void)fetch8(cpu);
		push(cpu, cpu->code_bank);
		cpu->code_bank = 0;
		push(cpu, (uint8_t)(cpu->pc >> 8));
		push(cpu, (uint8_t)cpu->pc);
		push(cpu, (uint8_t)(cpu->p | XAVIX_CPU_B | XAVIX_CPU_U));
		cpu->p |= XAVIX_CPU_I | XAVIX_CPU_B | XAVIX_CPU_U;
		cpu->pc = (uint16_t)(read_vector(cpu, 0xfffe) |
			((uint16_t)read_vector(cpu, 0xffff) << 8));
		return 8;

	case 0x02: /* CMC */
		(void)read_code_at(cpu, cpu->pc);
		cpu->p ^= XAVIX_CPU_C;
		return 2;

	case 0x04: /* ASR zp */
		address = fetch8(cpu);
		value = read_zero_page(cpu, (uint8_t)address);
		/* Match the SSD 2000 reference core: this form updates flags but
		 * writes the original byte back.  The hardware behaviour remains
		 * marked for verification in MAME. */
		(void)op_asr(cpu, value);
		write_zero_page(cpu, (uint8_t)address, value);
		return 4;
	case 0x0c: /* ASR abs */
		address = fetch16(cpu);
		value = read_data(cpu, address);
		write_data(cpu, address, value);
		value = op_asr(cpu, value);
		write_data(cpu, address, value);
		return 6;
	case 0x14: /* ASR zp,X */
		value = read_code_at(cpu, cpu->pc);
		(void)read_code_at(cpu, cpu->pc);
		cpu->pc++;
		address = (uint8_t)(value + cpu->x);
		value = read_zero_page(cpu, (uint8_t)address);
		(void)read_zero_page(cpu, (uint8_t)(address + 1));
		value = op_asr(cpu, value);
		write_zero_page(cpu, (uint8_t)address, value);
		return 6;
	case 0x1a: /* ASR A */
		cpu->a = op_asr(cpu, cpu->a);
		return 1;
	case 0x1c: /* ASR abs,X */
		base = fetch16(cpu);
		dummy_indexed_read(cpu, base, cpu->x);
		address = (uint16_t)(base + cpu->x);
		value = read_data(cpu, address);
		(void)read_data(cpu, address);
		value = op_asr(cpu, value);
		write_data(cpu, address, value);
		return 7;

	case 0x08: /* PHP */
		(void)read_code_at(cpu, cpu->pc);
		push(cpu, (uint8_t)(cpu->p | XAVIX_CPU_B | XAVIX_CPU_U));
		return 3;
	case 0x12: /* PHX */
		(void)read_code_at(cpu, cpu->pc);
		push(cpu, cpu->x);
		return 3;
	case 0x28: /* PLP */
		(void)read_code_at(cpu, cpu->pc);
		(void)read_stack(cpu);
		old_i = cpu->p & XAVIX_CPU_I;
		cpu->p = (uint8_t)(pull(cpu) | XAVIX_CPU_B | XAVIX_CPU_U);
		if (old_i && !(cpu->p & XAVIX_CPU_I))
			cpu->irq_inhibit = 1;
		return 4;
	case 0x32: /* PLX */
		(void)read_code_at(cpu, cpu->pc);
		(void)read_stack(cpu);
		cpu->x = pull(cpu);
		set_nz(cpu, cpu->x);
		return 4;
	case 0x48: /* PHA */
		(void)read_code_at(cpu, cpu->pc);
		push(cpu, cpu->a);
		return 3;
	case 0x52: /* PHY */
		(void)read_code_at(cpu, cpu->pc);
		push(cpu, cpu->y);
		return 3;
	case 0x68: /* PLA */
		(void)read_code_at(cpu, cpu->pc);
		(void)read_stack(cpu);
		cpu->a = pull(cpu);
		set_nz(cpu, cpu->a);
		return 4;
	case 0x72: /* PLY */
		(void)read_code_at(cpu, cpu->pc);
		(void)read_stack(cpu);
		cpu->y = pull(cpu);
		set_nz(cpu, cpu->y);
		return 4;

	case 0x10: return execute_branch(cpu, !(cpu->p & XAVIX_CPU_N));
	case 0x30: return execute_branch(cpu,  (cpu->p & XAVIX_CPU_N));
	case 0x50: return execute_branch(cpu, !(cpu->p & XAVIX_CPU_V));
	case 0x70: return execute_branch(cpu,  (cpu->p & XAVIX_CPU_V));
	case 0x90: return execute_branch(cpu, !(cpu->p & XAVIX_CPU_C));
	case 0xb0: return execute_branch(cpu,  (cpu->p & XAVIX_CPU_C));
	case 0xd0: return execute_branch(cpu, !(cpu->p & XAVIX_CPU_Z));
	case 0xf0: return execute_branch(cpu,  (cpu->p & XAVIX_CPU_Z));

	case 0x18: cpu->p &= (uint8_t)~XAVIX_CPU_C; return 2;
	case 0x38: cpu->p |= XAVIX_CPU_C; return 2;
	case 0x58:
		old_i = cpu->p & XAVIX_CPU_I;
		cpu->p &= (uint8_t)~XAVIX_CPU_I;
		if (old_i)
			cpu->irq_inhibit = 1;
		return 2;
	case 0x78: cpu->p |= XAVIX_CPU_I; return 2;
	case 0x89: cpu->p |= XAVIX_CPU_V; return 2; /* SEV */
	case 0xb8: cpu->p &= (uint8_t)~XAVIX_CPU_V; return 2;
	case 0xd8: cpu->p &= (uint8_t)~XAVIX_CPU_D; return 2;
	case 0xf8: cpu->p |= XAVIX_CPU_D; return 2;

	case 0x20: /* JSR abs */
		lo = fetch8(cpu);
		(void)read_stack(cpu);
		push(cpu, (uint8_t)(cpu->pc >> 8));
		push(cpu, (uint8_t)cpu->pc);
		hi = fetch8(cpu);
		cpu->pc = (uint16_t)(lo | ((uint16_t)hi << 8));
		return 6;
	case 0x22: /* CALLF bank,abs */
		(void)read_stack(cpu);
		push(cpu, cpu->code_bank);
		bank = fetch8(cpu);
		lo = fetch8(cpu);
		push(cpu, (uint8_t)(cpu->pc >> 8));
		push(cpu, (uint8_t)cpu->pc);
		hi = fetch8(cpu);
		cpu->code_bank = bank;
		cpu->pc = (uint16_t)(lo | ((uint16_t)hi << 8));
		return 9;
	case 0x40: /* RTI with far bank */
		(void)read_code_at(cpu, cpu->pc);
		(void)read_stack(cpu);
		old_i = cpu->p & XAVIX_CPU_I;
		cpu->p = (uint8_t)(pull(cpu) | XAVIX_CPU_B | XAVIX_CPU_U);
		lo = pull(cpu);
		hi = pull(cpu);
		cpu->code_bank = pull(cpu);
		cpu->pc = (uint16_t)(lo | ((uint16_t)hi << 8));
		if (old_i && !(cpu->p & XAVIX_CPU_I))
			cpu->irq_inhibit = 1;
		return 7;
	case 0x4c: cpu->pc = fetch16(cpu); return 3;
	case 0x5c: /* JMPF bank,abs */
		bank = fetch8(cpu);
		lo = fetch8(cpu);
		hi = fetch8(cpu);
		cpu->code_bank = bank;
		cpu->pc = (uint16_t)(lo | ((uint16_t)hi << 8));
		return 4;
	case 0x60: /* RTS */
		(void)read_code_at(cpu, cpu->pc);
		(void)read_stack(cpu);
		lo = pull(cpu);
		hi = pull(cpu);
		cpu->pc = (uint16_t)(lo | ((uint16_t)hi << 8));
		(void)read_code_at(cpu, cpu->pc);
		cpu->pc++;
		return 6;
	case 0x6c: /* NMOS indirect JMP page-wrap behaviour */
		base = fetch16(cpu);
		lo = read_data(cpu, base);
		hi = read_data(cpu, (uint16_t)((base & 0xff00) | (uint8_t)(base + 1)));
		cpu->pc = (uint16_t)(lo | ((uint16_t)hi << 8));
		return 5;
	case 0x7c: /* JMPF (abs): low/high wrap, bank at pointer+2 */
		base = fetch16(cpu);
		lo = read_data(cpu, base);
		hi = read_data(cpu, (uint16_t)((base & 0xff00) | (uint8_t)(base + 1)));
		bank = read_data(cpu, (uint16_t)(base + 2));
		cpu->code_bank = bank;
		cpu->pc = (uint16_t)(lo | ((uint16_t)hi << 8));
		return 6;
	case 0x80: /* RETF */
		(void)read_code_at(cpu, cpu->pc);
		(void)read_stack(cpu);
		lo = pull(cpu);
		hi = pull(cpu);
		cpu->code_bank = pull(cpu);
		cpu->pc = (uint16_t)(lo | ((uint16_t)hi << 8));
		(void)read_code_at(cpu, cpu->pc);
		cpu->pc++;
		return 7;

	case 0x24:
		op_bit(cpu, read_zero_page(cpu, fetch8(cpu)));
		return 3;
	case 0x2c:
		op_bit(cpu, read_data(cpu, fetch16(cpu)));
		return 4;
	case 0x34:
		address = addr_zpx(cpu, cpu->x);
		op_bit(cpu, read_zero_page(cpu, (uint8_t)address));
		return 4;
	case 0x3a:
		op_bit(cpu, fetch8(cpu));
		return 2;
	case 0x3c:
		address = (uint16_t)(fetch16(cpu) + cpu->x);
		op_bit(cpu, read_data(cpu, address));
		return 4;

	case 0x82: write_zero_page(cpu, fetch8(cpu), 0); return 3;
	case 0x92: write_data(cpu, fetch16(cpu), 0); return 4;

	case 0x84: write_zero_page(cpu, fetch8(cpu), cpu->y); return 3;
	case 0x86: write_zero_page(cpu, fetch8(cpu), cpu->x); return 3;
	case 0x8c: write_data(cpu, fetch16(cpu), cpu->y); return 4;
	case 0x8e: write_data(cpu, fetch16(cpu), cpu->x); return 4;
	case 0x94: write_zero_page(cpu, (uint8_t)addr_zpx(cpu, cpu->x), cpu->y); return 4;
	case 0x96: write_zero_page(cpu, (uint8_t)addr_zpx(cpu, cpu->y), cpu->x); return 4;
	case 0x9c: write_data(cpu, (uint16_t)(fetch16(cpu) + cpu->x), cpu->y); return 4;
	case 0x9e: write_data(cpu, (uint16_t)(fetch16(cpu) + cpu->y), cpu->x); return 4;

	case 0xa0: cpu->y = fetch8(cpu); set_nz(cpu, cpu->y); return 2;
	case 0xa2: cpu->x = fetch8(cpu); set_nz(cpu, cpu->x); return 2;
	case 0xa4: cpu->y = read_zero_page(cpu, fetch8(cpu)); set_nz(cpu, cpu->y); return 3;
	case 0xa6: cpu->x = read_zero_page(cpu, fetch8(cpu)); set_nz(cpu, cpu->x); return 3;
	case 0xac: cpu->y = read_data(cpu, fetch16(cpu)); set_nz(cpu, cpu->y); return 4;
	case 0xae: cpu->x = read_data(cpu, fetch16(cpu)); set_nz(cpu, cpu->x); return 4;
	case 0xb4: cpu->y = read_zero_page(cpu, (uint8_t)addr_zpx(cpu, cpu->x)); set_nz(cpu, cpu->y); return 4;
	case 0xb6: cpu->x = read_zero_page(cpu, (uint8_t)addr_zpx(cpu, cpu->y)); set_nz(cpu, cpu->x); return 4;
	case 0xbc:
		base = fetch16(cpu); address = (uint16_t)(base + cpu->x);
		if (page_crossed(base, cpu->x)) dummy_indexed_read(cpu, base, cpu->x);
		cpu->y = read_data(cpu, address); set_nz(cpu, cpu->y);
		return 4 + page_crossed(base, cpu->x);
	case 0xbe:
		base = fetch16(cpu); address = (uint16_t)(base + cpu->y);
		if (page_crossed(base, cpu->y)) dummy_indexed_read(cpu, base, cpu->y);
		cpu->x = read_data(cpu, address); set_nz(cpu, cpu->x);
		return 4 + page_crossed(base, cpu->y);

	case 0xc0: op_cmp(cpu, cpu->y, fetch8(cpu)); return 2;
	case 0xc4: op_cmp(cpu, cpu->y, read_zero_page(cpu, fetch8(cpu))); return 3;
	case 0xcc: op_cmp(cpu, cpu->y, read_data(cpu, fetch16(cpu))); return 4;
	case 0xe0: op_cmp(cpu, cpu->x, fetch8(cpu)); return 2;
	case 0xe4: op_cmp(cpu, cpu->x, read_zero_page(cpu, fetch8(cpu))); return 3;
	case 0xec: op_cmp(cpu, cpu->x, read_data(cpu, fetch16(cpu))); return 4;

	case 0x88: cpu->y--; set_nz(cpu, cpu->y); return 2;
	case 0x98: cpu->a = cpu->y; set_nz(cpu, cpu->a); return 2;
	case 0xa8: cpu->y = cpu->a; set_nz(cpu, cpu->y); return 2;
	case 0xb2: cpu->a = 0; set_nz(cpu, cpu->a); return 2;
	case 0xc2: cpu->a--; set_nz(cpu, cpu->a); return 2;
	case 0xc8: cpu->y++; set_nz(cpu, cpu->y); return 2;
	case 0xca: cpu->x--; set_nz(cpu, cpu->x); return 2;
	case 0xd2: cpu->a ^= 0xff; set_nz(cpu, cpu->a); return 2;
	case 0xe2: cpu->a++; set_nz(cpu, cpu->a); return 2;
	case 0xe8: cpu->x++; set_nz(cpu, cpu->x); return 2;
	case 0xf2: cpu->a = (uint8_t)(0 - cpu->a); set_nz(cpu, cpu->a); return 2;

	case 0x8a: cpu->a = cpu->x; set_nz(cpu, cpu->a); return 2;
	case 0x9a: cpu->s = cpu->x; return 2;
	case 0xaa: cpu->x = cpu->a; set_nz(cpu, cpu->x); return 2;
	case 0xba: cpu->x = cpu->s; set_nz(cpu, cpu->x); return 2;

	case 0x1b: cpu->pa = (cpu->pa & UINT32_C(0xffff00)) | cpu->a; return 2;
	case 0x3b: cpu->a = (uint8_t)cpu->pa; set_nz(cpu, cpu->a); return 2;
	case 0x5b: cpu->pa = (cpu->pa & UINT32_C(0xff00ff)) | ((uint32_t)cpu->a << 8); return 2;
	case 0x7b: cpu->a = (uint8_t)(cpu->pa >> 8); set_nz(cpu, cpu->a); return 2;
	case 0x9b: cpu->pa = (cpu->pa & UINT32_C(0x00ffff)) | ((uint32_t)cpu->a << 16); return 2;
	case 0xbb: cpu->a = (uint8_t)(cpu->pa >> 16); set_nz(cpu, cpu->a); return 2;
	case 0x1f: cpu->pb = (cpu->pb & UINT32_C(0xffff00)) | cpu->a; return 2;
	case 0x3f: cpu->a = (uint8_t)cpu->pb; set_nz(cpu, cpu->a); return 2;
	case 0x5f: cpu->pb = (cpu->pb & UINT32_C(0xff00ff)) | ((uint32_t)cpu->a << 8); return 2;
	case 0x7f: cpu->a = (uint8_t)(cpu->pb >> 8); set_nz(cpu, cpu->a); return 2;
	case 0x9f: cpu->pb = (cpu->pb & UINT32_C(0x00ffff)) | ((uint32_t)cpu->a << 16); return 2;
	case 0xbf: cpu->a = (uint8_t)(cpu->pb >> 16); set_nz(cpu, cpu->a); return 2;

	case 0xdb: cpu->pa = (cpu->pa & ~UINT32_C(0xff)) | ((cpu->pa - 1) & 0xff); set_nz(cpu, (uint8_t)cpu->pa); return 2;
	case 0xdf: cpu->pb = (cpu->pb & ~UINT32_C(0xff)) | ((cpu->pb - 1) & 0xff); set_nz(cpu, (uint8_t)cpu->pb); return 2;
	case 0xfb: cpu->pa = (cpu->pa & ~UINT32_C(0xff)) | ((cpu->pa + 1) & 0xff); set_nz(cpu, (uint8_t)cpu->pa); return 2;
	case 0xff: cpu->pb = (cpu->pb & ~UINT32_C(0xff)) | ((cpu->pb + 1) & 0xff); set_nz(cpu, (uint8_t)cpu->pb); return 2;

	/* SSD 2000 one-byte NOP slots. */
	case 0x42: case 0x44: case 0x54: case 0x5a:
	case 0x62: case 0x64: case 0x74: case 0x7a:
	case 0xd4: case 0xda: case 0xdc:
	case 0xea: case 0xf4: case 0xfa: case 0xfc:
		(void)read_code_at(cpu, cpu->pc);
		return 2;

	default:
		/* The SSD 2000 table defines every byte.  Stop only on a programming
		 * error so an unknown instruction cannot silently corrupt state. */
		cpu->stopped = 1;
		return 2;
	}
}

void xavix_cpu_init(xavix_cpu_t *cpu, xavix_cpu_read8_fn read8,
	xavix_cpu_write8_fn write8, void *opaque)
{
	if (!cpu)
		return;
	memset(cpu, 0, sizeof(*cpu));
	cpu->read8 = read8;
	cpu->write8 = write8;
	cpu->opaque = opaque;
	xavix_cpu_reset(cpu);
}

void xavix_cpu_reset(xavix_cpu_t *cpu)
{
	xavix_cpu_read8_fn read8;
	xavix_cpu_write8_fn write8;
	void *opaque;

	if (!cpu)
		return;
	read8 = cpu->read8;
	write8 = cpu->write8;
	opaque = cpu->opaque;
	memset(cpu, 0, sizeof(*cpu));
	cpu->read8 = read8;
	cpu->write8 = write8;
	cpu->opaque = opaque;

	/* MAME's deterministic power-on values followed by the three reset
	 * stack cycles: A=00, X=80, Y=00, P=36, SP=fd. */
	cpu->x = 0x80;
	cpu->s = 0xfd;
	cpu->p = XAVIX_CPU_U | XAVIX_CPU_B | XAVIX_CPU_I | XAVIX_CPU_Z;
	cpu->pc = (uint16_t)(read_vector(cpu, 0xfffc) |
		((uint16_t)read_vector(cpu, 0xfffd) << 8));
}

void xavix_cpu_set_irq(xavix_cpu_t *cpu, int asserted)
{
	if (cpu)
		cpu->irq_line = asserted ? 1 : 0;
}

void xavix_cpu_set_nmi(xavix_cpu_t *cpu, int asserted)
{
	if (!cpu)
		return;
	if (asserted && !cpu->nmi_line)
		cpu->nmi_pending = 1;
	cpu->nmi_line = asserted ? 1 : 0;
}

int xavix_cpu_execute(xavix_cpu_t *cpu, int cycle_budget)
{
	int elapsed = 0;

	if (!cpu || cycle_budget <= 0)
		return 0;
	while (elapsed < cycle_budget && !cpu->stopped)
	{
		int cycles;
		if (cpu->nmi_pending)
			cycles = service_interrupt(cpu, 1);
		else if (!cpu->irq_inhibit && cpu->irq_line && !(cpu->p & XAVIX_CPU_I))
			cycles = service_interrupt(cpu, 0);
		else
		{
			if (cpu->irq_inhibit)
				cpu->irq_inhibit--;
			cycles = execute_one(cpu);
		}
		elapsed += cycles;
		cpu->total_cycles += (uint64_t)cycles;
	}
	return elapsed;
}

uint32_t xavix_cpu_linear_pc(const xavix_cpu_t *cpu)
{
	return cpu ? ((((uint32_t)cpu->code_bank << 16) | cpu->pc) & ADDR24_MASK) : 0;
}
