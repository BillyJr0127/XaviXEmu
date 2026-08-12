/*
 * SPDX-License-Identifier: BSD-3-Clause
 * MAME-derived portions copyright-holders: David Haywood, Angelo Salese
 * XaviXEmu adaptation and modifications:
 * Copyright (c) 2026 Billy Jr. and contributors
 *
 * Minimal SSD 2000/XaviX machine bus for the drgqst standalone player.
 * Behaviour is derived from MAME's BSD-3-Clause XaviX driver by David
 * Haywood.  This file contains no MAME framework code.
 */
#ifndef DRGQST_PLAYER_XAVIX_MACHINE_H
#define DRGQST_PLAYER_XAVIX_MACHINE_H

#include "xavix_peripherals.h"

#include <stddef.h>
#include <stdint.h>

enum
{
	XAVIX_CLOCK_HZ = 21477272,
	XAVIX_FRAME_RATE = 60,
	XAVIX_FRAME_WIDTH = 256,
	XAVIX_FRAME_HEIGHT = 224,
	XAVIX_MAIN_RAM_SIZE = 0x4000,
	XAVIX_PARALLEL_NVRAM_SIZE = 0x1000,
	XAVIX_PARALLEL_NVRAM_BASE = XAVIX_MAIN_RAM_SIZE - XAVIX_PARALLEL_NVRAM_SIZE,
	XAVIX_FRAGMENT_RAM_SIZE = 0x0800,
	XAVIX_SOUND_RAM_SIZE = 0x0180
};

typedef uint8_t (*xavix_io_read_fn)(void *context, uint8_t direction);
typedef void (*xavix_io_write_fn)(void *context, uint8_t data, uint8_t direction);
typedef uint8_t (*xavix_adc_read_fn)(void *context, unsigned channel);
typedef uint8_t (*xavix_anport_read_fn)(void *context, unsigned channel);
typedef uint8_t (*xavix_sound_read_fn)(void *context, unsigned offset);
typedef void (*xavix_sound_write_fn)(void *context, unsigned offset, uint8_t data);
typedef uint8_t (*xavix_external_read_fn)(void *context, uint32_t address,
	int *handled);

typedef struct xavix_machine_hooks
{
	void *context;
	xavix_io_read_fn read_io1;
	xavix_io_write_fn write_io1;
	xavix_adc_read_fn read_adc;
	xavix_anport_read_fn read_anport;
	xavix_sound_read_fn read_sound;
	xavix_sound_write_fn write_sound;
	xavix_external_read_fn read_external;
} xavix_machine_hooks;

/* Pointer-free emulated state.  This is the payload used by save states. */
typedef struct xavix_machine_state
{
	uint8_t main_ram[XAVIX_MAIN_RAM_SIZE];
	uint8_t txarray[3];
	uint8_t fragment_ram[XAVIX_FRAGMENT_RAM_SIZE];
	uint8_t palette_sh[0x100];
	uint8_t palette_l[0x100];
	uint8_t segment_regs[0x20];
	uint8_t tile_regs[2][8];
	uint8_t sprite_reg;
	uint8_t sprite_dma_param1[2];
	uint8_t sprite_dma_param2[2];
	uint8_t arena_start;
	uint8_t arena_end;
	uint8_t arena_control;
	uint8_t colmix_sh;
	uint8_t colmix_l;
	uint8_t colmix_control;
	uint8_t video_control;
	uint8_t position_irq_x;
	uint8_t position_irq_y;

	uint8_t sound_ram[XAVIX_SOUND_RAM_SIZE];
	uint8_t sound_regs[0x10];
	uint8_t sound_regbase;
	uint8_t sound_irq_status;

	uint8_t extbus_control[3];

	uint8_t io_data[2];
	uint8_t io_direction[2];
	uint8_t ioevent_enable;
	uint8_t ioevent_active;
	uint8_t input0;
	uint8_t input1;

	uint8_t anport_regs[4];
	uint8_t adc_control;
	uint8_t adc_latch;

	xavix_peripherals peripherals;

	uint8_t vector_enable;
	uint8_t nmi_vector[2];
	uint8_t irq_vector[2];
	uint8_t irq_source;
	uint8_t irq_asserted;
	uint8_t nmi_asserted;

	uint64_t total_cycles;
	uint32_t frame_cycles;
} xavix_machine_state;

typedef struct xavix_machine
{
	const uint8_t *rom;
	size_t rom_size;
	xavix_machine_hooks hooks;
	xavix_machine_state state;
	/* Host persistence bookkeeping; not visible through guest registers. */
	uint32_t nvram_write_generation;
} xavix_machine;

void xavix_machine_init(xavix_machine *machine, const uint8_t *rom, size_t rom_size);
void xavix_machine_reset(xavix_machine *machine);
void xavix_machine_set_hooks(xavix_machine *machine, const xavix_machine_hooks *hooks);
void xavix_machine_set_sword_input(xavix_machine *machine, uint8_t x, uint8_t y,
	enum xavix_sensor_mode mode);
void xavix_machine_trigger_ioevent(xavix_machine *machine, uint8_t bits);

uint8_t xavix_machine_read_low(void *context, uint16_t address);
void xavix_machine_write_low(void *context, uint16_t address, uint8_t data);
uint8_t xavix_machine_read_external(void *context, uint32_t address);
void xavix_machine_write_external(void *context, uint32_t address, uint8_t data);
uint8_t xavix_machine_read_vector(void *context, uint16_t address);

/* Video/DMA view: low bus below 0x8000, ROM elsewhere. */
uint8_t xavix_machine_read_full(const xavix_machine *machine, uint32_t address);

void xavix_machine_advance(xavix_machine *machine, unsigned cycles);

#endif
