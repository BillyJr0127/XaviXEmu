// SPDX-License-Identifier: BSD-3-Clause
// MAME-derived portions copyright-holders: Olivier Galibert, David Haywood
// XaviXEmu port and modifications:
// Copyright (c) 2026 Billy Jr. and contributors
/*
 * Compact SSD 2000 / XaviX CPU interpreter.
 *
 * The instruction behaviour is derived from MAME's BSD-3-Clause m6502,
 * XaviX and XaviX 2000 cores.  This interface intentionally has no dependency
 * on the MAME device framework.
 */

#ifndef DRGQST_XAVIX_CPU_H
#define DRGQST_XAVIX_CPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum xavix_cpu_flag
{
	XAVIX_CPU_C = 0x01,
	XAVIX_CPU_Z = 0x02,
	XAVIX_CPU_I = 0x04,
	XAVIX_CPU_D = 0x08,
	XAVIX_CPU_B = 0x10,
	XAVIX_CPU_U = 0x20,
	XAVIX_CPU_V = 0x40,
	XAVIX_CPU_N = 0x80
};

/*
 * LOW addresses are the fixed 0x0000-0x7fff internal bus.  EXTERNAL
 * addresses have already been folded to the SSD 2000's 23-bit external bus.
 * VECTOR is an external vector fetch; a machine may substitute the XaviX
 * programmable vectors and otherwise treat it exactly like EXTERNAL.
 */
typedef enum xavix_cpu_bus
{
	XAVIX_CPU_BUS_LOW = 0,
	XAVIX_CPU_BUS_EXTERNAL = 1,
	XAVIX_CPU_BUS_VECTOR = 2
} xavix_cpu_bus_t;

typedef uint8_t (*xavix_cpu_read8_fn)(void *opaque, xavix_cpu_bus_t bus, uint32_t address);
typedef void (*xavix_cpu_write8_fn)(void *opaque, xavix_cpu_bus_t bus, uint32_t address, uint8_t data);

typedef struct xavix_cpu
{
	/* Architecturally visible registers. */
	uint8_t a;
	uint8_t x;
	uint8_t y;
	uint8_t s;
	uint8_t p;
	uint16_t pc;
	uint8_t code_bank;
	uint8_t data_bank;
	uint8_t j;
	uint8_t k;
	uint8_t l;
	uint8_t m;
	uint32_t pa;
	uint32_t pb;

	/* Line and scheduler state; include these fields in save states. */
	uint8_t irq_line;
	uint8_t nmi_line;
	uint8_t nmi_pending;
	uint8_t irq_inhibit;
	uint8_t stopped;
	uint64_t total_cycles;

	/* CPU-family configuration is host-owned, not serialized. */
	uint8_t decimal_arithmetic;

	/* Host bindings are not part of a serialized save state. */
	xavix_cpu_read8_fn read8;
	xavix_cpu_write8_fn write8;
	void *opaque;
} xavix_cpu_t;

void xavix_cpu_init(xavix_cpu_t *cpu, xavix_cpu_read8_fn read8,
	xavix_cpu_write8_fn write8, void *opaque);
void xavix_cpu_reset(xavix_cpu_t *cpu);
void xavix_cpu_set_decimal_arithmetic(xavix_cpu_t *cpu, int enabled);
void xavix_cpu_set_irq(xavix_cpu_t *cpu, int asserted);
void xavix_cpu_set_nmi(xavix_cpu_t *cpu, int asserted);

/*
 * Runs whole instructions until at least cycle_budget cycles have elapsed.
 * The return value can exceed the budget by one instruction.  A non-positive
 * budget performs no work.  The accumulated count is also in total_cycles.
 */
int xavix_cpu_execute(xavix_cpu_t *cpu, int cycle_budget);

/* Current 24-bit logical fetch address, useful for traces and diagnostics. */
uint32_t xavix_cpu_linear_pc(const xavix_cpu_t *cpu);

#ifdef __cplusplus
}
#endif

#endif /* DRGQST_XAVIX_CPU_H */
