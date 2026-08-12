// SPDX-License-Identifier: BSD-3-Clause
// MAME-derived portions copyright-holders: Olivier Galibert, Nathan Gilbert
// XaviXEmu port and modifications:
// Copyright (c) 2026 Billy Jr. and contributors
/*
 * Standalone interpreter for the SSD 2002-2004 "XaviX 2" RISC CPU.
 *
 * Instruction semantics are derived from MAME's BSD-3-Clause XaviX 2 CPU
 * core.  The host supplies a flat little-endian 32-bit memory bus.
 */

#ifndef XAVIXEMU_XAVIX2_CPU_H
#define XAVIXEMU_XAVIX2_CPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t (*xavix2_read8_fn)(void *opaque, uint32_t address);
typedef void (*xavix2_write8_fn)(void *opaque, uint32_t address, uint8_t data);
typedef void (*xavix2_interrupt_ack_fn)(void *opaque);

struct xavix2_cpu;
typedef void (*xavix2_trace_fn)(void *opaque, const struct xavix2_cpu *cpu,
	uint32_t pc, uint32_t opcode, uint8_t bytes);

typedef struct xavix2_cpu
{
	uint32_t pc;
	uint32_t r[8];
	uint32_t hr[64];
	uint32_t ilr1;
	uint8_t if1;
	uint8_t interrupt_line;
	uint8_t waiting;
	uint8_t enable_interrupt_delay;
	uint64_t total_cycles;
	uint64_t total_instructions;
	uint64_t interrupt_count;
	uint64_t unimplemented_count;
	uint32_t first_unimplemented_pc;
	uint8_t first_unimplemented_opcode;
	xavix2_read8_fn read8;
	xavix2_read8_fn fetch8;
	xavix2_write8_fn write8;
	void *opaque;
	xavix2_interrupt_ack_fn interrupt_ack;
	void *interrupt_ack_opaque;
	xavix2_trace_fn trace;
	void *trace_opaque;
} xavix2_cpu_t;

void xavix2_cpu_init(xavix2_cpu_t *cpu, xavix2_read8_fn read8,
	xavix2_write8_fn write8, void *opaque);
void xavix2_cpu_reset(xavix2_cpu_t *cpu);
void xavix2_cpu_set_fetch(xavix2_cpu_t *cpu, xavix2_read8_fn fetch8);
void xavix2_cpu_set_interrupt(xavix2_cpu_t *cpu, int asserted);
void xavix2_cpu_set_interrupt_ack(xavix2_cpu_t *cpu,
	xavix2_interrupt_ack_fn acknowledge, void *opaque);

/* Execute at most cycle_budget fetched bytes. Returns fetched byte cycles. */
uint32_t xavix2_cpu_execute(xavix2_cpu_t *cpu, uint32_t cycle_budget);

#ifdef __cplusplus
}
#endif

#endif /* XAVIXEMU_XAVIX2_CPU_H */
