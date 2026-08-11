/*
 * SPDX-License-Identifier: BSD-3-Clause
 * MAME-derived portions copyright-holders: David Haywood, Angelo Salese
 * XaviXEmu adaptation and modifications:
 * Copyright (c) 2026 Billy Jr. and contributors
 *
 * Game-focused SSD 2000/XaviX bus implementation.  Register behaviour is
 * derived from MAME's BSD-3-Clause XaviX driver by David Haywood.
 */
#include "xavix_machine.h"

#include <string.h>

static uint8_t txarray_read(const xavix_machine_state *state, uint16_t offset)
{
	if (offset < 0x100)
		return (uint8_t)((offset >> 4) | (offset << 4));
	if (offset < 0x200)
	{
		offset &= 0xff;
		return (uint8_t)((offset >> 4) | ((uint8_t)~offset << 4));
	}
	if (offset < 0x300)
	{
		offset &= 0xff;
		return (uint8_t)((((uint8_t)~offset >> 4) & 0x0f) | (offset << 4));
	}
	if (offset < 0x400)
	{
		offset &= 0xff;
		return (uint8_t)((((uint8_t)~offset >> 4) & 0x0f) | ((uint8_t)~offset << 4));
	}
	if (offset < 0x800)
		return state->txarray[0];
	if (offset < 0xc00)
		return state->txarray[1];
	return state->txarray[2];
}

static void txarray_write(xavix_machine_state *state, uint16_t offset, uint8_t data)
{
	if (offset >= 0x400 && offset < 0x800)
		state->txarray[0] = data;
	else if (offset >= 0x800 && offset < 0xc00)
		state->txarray[1] = data;
	else if (offset >= 0xc00)
		state->txarray[2] = data;
}

static void update_irq(xavix_machine_state *state)
{
	state->irq_asserted = state->irq_source != 0;
}

static void sprite_ram_write(xavix_machine_state *state, uint16_t offset, uint8_t data)
{
	unsigned index = offset & 0xff;
	offset &= 0x7ff;
	if (offset < 0x100)
	{
		state->fragment_ram[offset] = data & 0xfe;
		state->fragment_ram[0x400 + index] = (data & 1) != 0;
		state->fragment_ram[offset] |= state->fragment_ram[0x400 + index];
	}
	else if (offset < 0x300)
		state->fragment_ram[offset] = data;
	else if (offset < 0x400)
	{
		state->fragment_ram[offset] = data;
		state->fragment_ram[0x400 + index] = (data & 0x80) != 0;
		state->fragment_ram[index] =
			(state->fragment_ram[index] & 0xfe) | state->fragment_ram[0x400 + index];
	}
	else if (offset < 0x500)
	{
		state->fragment_ram[offset] = (data & 1) != 0;
		state->fragment_ram[index] =
			(state->fragment_ram[index] & 0xfe) | state->fragment_ram[offset];
	}
	else
		state->fragment_ram[offset] = data;
}

static void write_full(xavix_machine *machine, uint32_t address, uint8_t data)
{
	uint8_t bank = (uint8_t)(address >> 16);
	uint16_t low = (uint16_t)address;
	if (bank < 0x80 && low < 0x8000)
		xavix_machine_write_low(machine, low, data);
}

uint8_t xavix_machine_read_full(const xavix_machine *machine, uint32_t address)
{
	uint8_t bank = (uint8_t)(address >> 16);
	uint16_t low = (uint16_t)address;
	if (bank < 0x80 && low < 0x8000)
		return xavix_machine_read_low((void *)machine, low);
	return xavix_machine_read_external((void *)machine, address & 0x7fffff);
}

static uint8_t dma_read(void *opaque, uint32_t address)
{
	return xavix_machine_read_full((const xavix_machine *)opaque, address);
}

static void dma_write(void *opaque, uint16_t address, uint8_t data)
{
	write_full((xavix_machine *)opaque, address, data);
}

static void sync_dma_irq(xavix_machine_state *state)
{
	if (xavix_dma_irq_pending(&state->peripherals.dma))
		state->irq_source |= 0x20;
	else
		state->irq_source &= (uint8_t)~0x20;
	update_irq(state);
}

static void sprite_dma_trigger(xavix_machine *machine, uint8_t data)
{
	xavix_machine_state *state = &machine->state;
	unsigned pages = data & 7;
	uint16_t source = (uint16_t)(state->sprite_dma_param1[0] |
		((uint16_t)state->sprite_dma_param1[1] << 8));
	uint16_t destination = (uint16_t)state->sprite_dma_param2[0] << 8;
	unsigned block_size = (uint8_t)(state->sprite_dma_param2[1] - 1);
	unsigned length;
	unsigned input = 0;
	unsigned i;
	if (!(data & 0x40))
		return;
	if (pages == 0)
		pages = 8;
	length = pages << 8;
	for (i = 0; i < length; ++i)
	{
		if ((i & 0xff) <= block_size)
		{
			sprite_ram_write(state, (uint16_t)(destination + i),
				xavix_machine_read_full(machine, (uint16_t)(source + input)));
			++input;
		}
	}
}

static uint8_t raw_io_input(xavix_machine *machine, unsigned port)
{
	xavix_machine_state *state = &machine->state;
	if (port == 0)
		return state->input0;
	if (machine->hooks.read_io1)
		return machine->hooks.read_io1(machine->hooks.context, state->io_direction[1]);
	{
		uint8_t input = state->input1 & (uint8_t)~0x0e;
		if (xavix_eeprom24c08_read_sda(&state->peripherals.eeprom))
			input |= 0x08;
		return xavix_cu5501a_read_io1(&state->peripherals.sensor, input);
	}
}

static uint8_t io_read(xavix_machine *machine, unsigned port)
{
	xavix_machine_state *state = &machine->state;
	uint8_t direction = state->io_direction[port];
	return (uint8_t)((raw_io_input(machine, port) & ~direction) |
		(state->io_data[port] & direction));
}

static void io_write_notify(xavix_machine *machine, unsigned port)
{
	if (port == 1)
	{
		uint8_t direction = machine->state.io_direction[1];
		uint8_t pins = (uint8_t)((machine->state.io_data[1] & direction) |
			(raw_io_input(machine, 1) & ~direction));
		if (machine->hooks.write_io1)
			machine->hooks.write_io1(machine->hooks.context, pins, direction);
		else
		{
			xavix_eeprom24c08_set_lines(&machine->state.peripherals.eeprom,
				(direction & 0x10) ? !!(pins & 0x10) : 0,
				(direction & 0x08) ? !!(pins & 0x08) : 1);
			xavix_cu5501a_write_io1(&machine->state.peripherals.sensor, pins, direction);
		}
	}
}

void xavix_machine_init(xavix_machine *machine, const uint8_t *rom, size_t rom_size)
{
	memset(machine, 0, sizeof(*machine));
	machine->rom = rom;
	machine->rom_size = rom_size;
	xavix_peripherals_init(&machine->state.peripherals, NULL, 0, XAVIX_CLOCK_HZ);
	xavix_machine_reset(machine);
}

void xavix_machine_reset(xavix_machine *machine)
{
	const uint8_t *rom = machine->rom;
	size_t rom_size = machine->rom_size;
	xavix_machine_hooks hooks = machine->hooks;
	xavix_eeprom24c08 eeprom = machine->state.peripherals.eeprom;
	memset(&machine->state, 0, sizeof(machine->state));
	memset(machine->state.main_ram, 0xff, sizeof(machine->state.main_ram));
	machine->state.adc_latch = 0xff;
	machine->state.sound_regbase = 2;
	machine->state.peripherals.eeprom = eeprom;
	machine->state.peripherals.timer.master_clock_hz = XAVIX_CLOCK_HZ;
	xavix_peripherals_reset(&machine->state.peripherals);
	machine->rom = rom;
	machine->rom_size = rom_size;
	machine->hooks = hooks;
}

void xavix_machine_set_hooks(xavix_machine *machine, const xavix_machine_hooks *hooks)
{
	if (hooks)
		machine->hooks = *hooks;
	else
		memset(&machine->hooks, 0, sizeof(machine->hooks));
}

void xavix_machine_set_sword_input(xavix_machine *machine, uint8_t x, uint8_t y,
	enum xavix_sensor_mode mode)
{
	if (machine)
		xavix_cu5501a_set_input(&machine->state.peripherals.sensor, x, y, (uint8_t)mode);
}

void xavix_machine_trigger_ioevent(xavix_machine *machine, uint8_t bits)
{
	xavix_machine_state *state;
	if (!machine)
		return;
	state = &machine->state;
	bits &= state->ioevent_enable;
	if (!bits)
		return;
	state->ioevent_active |= bits;
	if (state->ioevent_active & 0x0f)
		state->irq_source |= 0x08;
	update_irq(state);
}

uint8_t xavix_machine_read_external(void *context, uint32_t address)
{
	const xavix_machine *machine = (const xavix_machine *)context;
	int handled = 0;
	uint8_t value;
	if (!machine || !machine->rom || machine->rom_size == 0)
		return 0xff;
	if (machine->hooks.read_external)
	{
		value = machine->hooks.read_external(machine->hooks.context,
			address & 0x7fffff, &handled);
		if (handled)
			return value;
	}
	return machine->rom[(address & 0x7fffff) % machine->rom_size];
}

void xavix_machine_write_external(void *context, uint32_t address, uint8_t data)
{
	(void)context;
	(void)address;
	(void)data;
}

uint8_t xavix_machine_read_vector(void *context, uint16_t address)
{
	const xavix_machine *machine = (const xavix_machine *)context;
	const xavix_machine_state *state = &machine->state;
	if (state->vector_enable)
	{
		if (address == 0xfffa)
			return state->nmi_vector[0];
		if (address == 0xfffb)
			return state->nmi_vector[1];
		if (address == 0xfffe)
			return state->irq_vector[0];
		if (address == 0xffff)
			return state->irq_vector[1];
	}
	return xavix_machine_read_external(context, address);
}

uint8_t xavix_machine_read_low(void *context, uint16_t address)
{
	xavix_machine *machine = (xavix_machine *)context;
	xavix_machine_state *state = &machine->state;
	address &= 0x7fff;
	if (address < 0x4000)
		return state->main_ram[address];
	if (address < 0x5000)
		return txarray_read(state, address & 0xfff);
	if (address >= 0x6000 && address < 0x6800)
		return state->fragment_ram[address & 0x7ff];
	if (address >= 0x6800 && address < 0x6900)
		return state->palette_sh[address & 0xff];
	if (address >= 0x6900 && address < 0x6a00)
		return state->palette_l[address & 0xff];
	if (address >= 0x6a00 && address < 0x6a20)
		return state->segment_regs[address & 0x1f];
	if (address >= 0x6fc8 && address < 0x6fd0)
		return state->tile_regs[0][address & 7];
	if (address >= 0x6fd0 && address < 0x6fd8)
		return state->tile_regs[1][address & 7];
	if (address == 0x6fd8)
		return state->sprite_reg;
	if (address == 0x6fe0)
		return 0;
	if (address == 0x6fe8)
		return state->arena_start;
	if (address == 0x6fe9)
		return state->arena_end;
	if (address == 0x6fea)
	{
		state->arena_control ^= 0x40;
		return state->arena_control;
	}
	if (address == 0x6ff0)
		return state->colmix_sh;
	if (address == 0x6ff1)
		return state->colmix_l;
	if (address == 0x6ff2)
		return state->colmix_control;
	if (address == 0x6ff8)
		return state->video_control;
	if (address == 0x6ff9)
		return 0;
	if (address >= 0x6ffc && address <= 0x6fff)
		return 0xff;
	if (address >= 0x7400 && address < 0x7580)
		return state->sound_ram[address - 0x7400];
	if (address >= 0x75f0 && address <= 0x75ff)
		return machine->hooks.read_sound ?
			machine->hooks.read_sound(machine->hooks.context, address & 0x0f) :
			state->sound_regs[address & 0x0f];
	if (address >= 0x7900 && address <= 0x7902)
		return state->extbus_control[address - 0x7900];
	if (address >= 0x7980 && address <= 0x7987)
		return xavix_dma_read(&state->peripherals.dma, address - 0x7980);
	if (address == 0x7a00 || address == 0x7a01)
		return io_read(machine, address & 1);
	if (address == 0x7a02 || address == 0x7a03)
		return state->io_direction[address & 1];
	if (address == 0x7a80)
		return state->ioevent_enable;
	if (address == 0x7a81)
		return state->ioevent_active;
	if (address == 0x7b00 || address == 0x7b01 || address == 0x7b10 || address == 0x7b11)
		return state->anport_regs[((address >> 4) & 1) * 2 + (address & 1)];
	if (address == 0x7b80)
		return state->adc_latch;
	if (address == 0x7b81)
		return 0;
	if (address >= 0x7c00 && address <= 0x7c03)
		return xavix_timer_read(&state->peripherals.timer, address - 0x7c00);
	if (address >= 0x7ff0 && address <= 0x7ff6)
		return xavix_math_read(&state->peripherals.math, address - 0x7ff0);
	if (address == 0x7ffa)
		return state->nmi_vector[0];
	if (address == 0x7ffb)
		return state->nmi_vector[1];
	if (address == 0x7ffc)
		return state->irq_source;
	if (address == 0x7ffe)
		return state->irq_vector[0];
	if (address == 0x7fff)
		return state->irq_vector[1];
	return 0xff;
}

void xavix_machine_write_low(void *context, uint16_t address, uint8_t data)
{
	xavix_machine *machine = (xavix_machine *)context;
	xavix_machine_state *state = &machine->state;
	address &= 0x7fff;
	if (address < 0x4000)
	{
		state->main_ram[address] = data;
		return;
	}
	if (address < 0x5000)
	{
		txarray_write(state, address & 0xfff, data);
		return;
	}
	if (address >= 0x6000 && address < 0x6800)
	{
		sprite_ram_write(state, address & 0x7ff, data);
		return;
	}
	if (address >= 0x6800 && address < 0x6900)
		state->palette_sh[address & 0xff] = data;
	else if (address >= 0x6900 && address < 0x6a00)
		state->palette_l[address & 0xff] = data;
	else if (address >= 0x6a00 && address < 0x6a20)
		state->segment_regs[address & 0x1f] = data;
	else if (address >= 0x6fc8 && address < 0x6fd0)
		state->tile_regs[0][address & 7] = data;
	else if (address >= 0x6fd0 && address < 0x6fd8)
		state->tile_regs[1][address & 7] = data;
	else if (address == 0x6fd8)
		state->sprite_reg = data;
	else if (address == 0x6fe0)
		sprite_dma_trigger(machine, data);
	else if (address >= 0x6fe1 && address <= 0x6fe2)
		state->sprite_dma_param1[address - 0x6fe1] = data;
	else if (address >= 0x6fe5 && address <= 0x6fe6)
		state->sprite_dma_param2[address - 0x6fe5] = data;
	else if (address == 0x6fe8)
		state->arena_start = data;
	else if (address == 0x6fe9)
		state->arena_end = data;
	else if (address == 0x6fea)
		state->arena_control = data & 0x7f;
	else if (address == 0x6ff0)
		state->colmix_sh = data;
	else if (address == 0x6ff1)
		state->colmix_l = data;
	else if (address == 0x6ff2)
		state->colmix_control = data;
	else if (address == 0x6ff8)
	{
		if (data & 0x40)
			state->irq_source &= (uint8_t)~0x40;
		if (data & 0x80)
			state->nmi_asserted = 0;
		state->video_control = data & 0x3f;
		update_irq(state);
	}
	else if (address == 0x6ffa)
		state->position_irq_x = data;
	else if (address == 0x6ffb)
		state->position_irq_y = data;
	else if (address >= 0x7400 && address < 0x7580)
		state->sound_ram[address - 0x7400] = data;
	else if (address >= 0x75f0 && address <= 0x75ff)
	{
		state->sound_regs[address & 0x0f] = data;
		if (machine->hooks.write_sound)
			machine->hooks.write_sound(machine->hooks.context, address & 0x0f, data);
		else if (address == 0x75f7)
			state->sound_regbase = data;
		else if (address == 0x75fe)
		{
			state->sound_irq_status &= (uint8_t)~data;
			if (!state->sound_irq_status)
				state->irq_source &= (uint8_t)~0x80;
			update_irq(state);
		}
	}
	else if (address >= 0x7900 && address <= 0x7902)
		state->extbus_control[address - 0x7900] = data;
	else if (address >= 0x7980 && address <= 0x7987)
	{
		xavix_dma_bus bus;
		bus.read = dma_read;
		bus.write = dma_write;
		bus.opaque = machine;
		xavix_dma_write(&state->peripherals.dma, address - 0x7980, data, &bus);
		sync_dma_irq(state);
	}
	else if (address == 0x7a00 || address == 0x7a01)
	{
		unsigned port = address & 1;
		state->io_data[port] = data;
		io_write_notify(machine, port);
	}
	else if (address == 0x7a02 || address == 0x7a03)
	{
		unsigned port = address & 1;
		state->io_direction[port] = data;
		io_write_notify(machine, port);
	}
	else if (address == 0x7a80)
		state->ioevent_enable = data;
	else if (address == 0x7a81)
	{
		state->ioevent_active &= (uint8_t)~data;
		if (!(state->ioevent_active & 0x0f))
			state->irq_source &= (uint8_t)~0x08;
		update_irq(state);
	}
	else if (address == 0x7b00 || address == 0x7b01 || address == 0x7b10 || address == 0x7b11)
		state->anport_regs[((address >> 4) & 1) * 2 + (address & 1)] = data;
	else if (address == 0x7b81)
	{
		unsigned channel = data & 0x13;
		state->adc_control = data;
		channel = (channel & 3) + ((channel & 0x10) ? 4 : 0);
		if (machine->hooks.read_adc)
			state->adc_latch = machine->hooks.read_adc(machine->hooks.context, channel);
		else if (channel == 0)
			state->adc_latch = xavix_cu5501a_read_adc(&state->peripherals.sensor);
		else
			state->adc_latch = 0xff;
	}
	else if (address >= 0x7c00 && address <= 0x7c02)
	{
		xavix_timer_write(&state->peripherals.timer, address - 0x7c00, data);
		if (!xavix_timer_irq_pending(&state->peripherals.timer))
			state->irq_source &= (uint8_t)~0x10;
		update_irq(state);
	}
	else if (address >= 0x7ff0 && address <= 0x7ff6)
		xavix_math_write(&state->peripherals.math, address - 0x7ff0, data);
	else if (address == 0x7ff9)
		state->vector_enable = data;
	else if (address == 0x7ffa)
		state->nmi_vector[0] = data;
	else if (address == 0x7ffb)
		state->nmi_vector[1] = data;
	else if (address == 0x7ffe)
		state->irq_vector[0] = data;
	else if (address == 0x7fff)
		state->irq_vector[1] = data;
}

void xavix_machine_advance(xavix_machine *machine, unsigned cycles)
{
	xavix_machine_state *state = &machine->state;
	state->total_cycles += cycles;
	state->frame_cycles += cycles;
	if (xavix_timer_advance(&state->peripherals.timer, cycles) ||
		xavix_timer_irq_pending(&state->peripherals.timer))
	{
		state->irq_source |= 0x10;
		update_irq(state);
	}
}
