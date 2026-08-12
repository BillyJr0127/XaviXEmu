// SPDX-License-Identifier: BSD-3-Clause
// MAME-derived portions copyright-holder: David Haywood
// XaviXEmu adaptation and modifications:
// Copyright (c) 2026 Billy Jr. and contributors

#include "xavix2_machine.h"

#include <stdlib.h>
#include <string.h>

enum
{
	IRQ_TIMER = 7,
	IRQ_MOTION = 10,
	IRQ_DMA = 12,
	AUDIO_DESCRIPTOR_RAM_OFFSET = 0xf800,
	CONTROLLER_POWER_STATUS_REGISTER = 0xc48,
	CONTROLLER_POWER_STATUS_COUNTER_MASK = 0x03,
	CONTROLLER_POWER_STATUS_GOOD = 0x04
};

static uint8_t machine_read8(void *opaque, uint32_t address);
static uint32_t pio_read(const xavix2_machine_t *machine);

static uint8_t machine_fetch8(void *opaque, uint32_t address)
{
	xavix2_machine_t *machine = (xavix2_machine_t *)opaque;
	if (address < XAVIX2_PROGRAM_RAM_SIZE)
		return machine->program_ram[address];
	return machine_read8(opaque, address);
}

static uint16_t load16(const uint8_t *data)
{
	return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t load32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
		((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void store32(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)(value >> 16);
	data[3] = (uint8_t)(value >> 24);
}

static int sample_experimental_pio(xavix2_machine_t *machine)
{
	uint32_t previous = load32(machine->low_ram + 0x0da4);
	uint32_t current = pio_read(machine) & UINT32_C(0x005fffa0);
	uint32_t changed = previous ^ current;
	uint32_t rising = changed & current;
	uint32_t falling = changed & ~current;
	store32(machine->low_ram + 0x0da4, current);
	store32(machine->low_ram + 0x0da8, rising);
	store32(machine->low_ram + 0x0dac, falling);
	if (rising)
		store32(machine->low_ram + 0x0db0, rising);
	machine->experimental_sampled_pio = current;
	return changed != 0;
}

static uint64_t machine_read64(xavix2_machine_t *machine, uint32_t address)
{
	uint64_t result = 0;
	unsigned index;
	for (index = 0; index < 8; ++index)
		result |= (uint64_t)machine_read8(machine, address + index) << (index * 8);
	return result;
}

static void refresh_interrupt_line(xavix2_machine_t *machine)
{
	xavix2_cpu_set_interrupt(&machine->cpu, machine->interrupt_pending != 0);
}

static unsigned first_interrupt_level(uint32_t lines)
{
	unsigned level;
	for (level = 0; level < 32; ++level)
		if (lines & (UINT32_C(1) << level))
			return level;
	return 32;
}

static void clear_interrupts(xavix2_machine_t *machine, uint32_t mask)
{
	machine->interrupt_active &= ~mask;
	machine->interrupt_pending &= ~mask;
	if (machine->interrupt_latched_valid &&
		(mask & (UINT32_C(1) << machine->interrupt_latched_level)))
		machine->interrupt_latched_valid = 0;
	refresh_interrupt_line(machine);
}

static void acknowledge_interrupt(void *opaque)
{
	xavix2_machine_t *machine = (xavix2_machine_t *)opaque;
	unsigned level = first_interrupt_level(machine->interrupt_pending);
	if (level < 32)
	{
		machine->interrupt_pending &= ~(UINT32_C(1) << level);
		machine->interrupt_latched_level = (uint8_t)level;
		machine->interrupt_latched_valid = 1;
	}
	refresh_interrupt_line(machine);
}

static void raise_interrupt(xavix2_machine_t *machine, unsigned level)
{
	uint32_t line = UINT32_C(1) << level;
	if ((machine->interrupt_enabled | machine->interrupt_nmi) & line)
	{
		if (!(machine->interrupt_active & line))
			machine->interrupt_pending |= line;
		machine->interrupt_active |= line;
		refresh_interrupt_line(machine);
	}
}

void xavix2_machine_raise_irq(xavix2_machine_t *machine, unsigned level)
{
	if (machine && level < 32)
		raise_interrupt(machine, level);
}

void xavix2_machine_clear_irq(xavix2_machine_t *machine, unsigned level)
{
	if (machine && level < 32)
		clear_interrupts(machine, UINT32_C(1) << level);
}

void xavix2_machine_set_capture(xavix2_machine_t *machine,
	uint16_t capture_a, uint16_t capture_b)
{
	if (!machine)
		return;
	machine->experimental_capture_readback = 1;
	machine->experimental_capture_a = capture_a;
	machine->experimental_capture_b = capture_b;
}

static void update_pio(xavix2_machine_t *machine)
{
	uint32_t mode0 = load32(machine->mmio + 0x200);
	uint32_t mode1 = load32(machine->mmio + 0x204);
	uint32_t data = load32(machine->mmio + 0x208);
	uint32_t mask = 0;
	unsigned bit;

	for (bit = 0; bit < 32; ++bit)
	{
		uint32_t modes = bit < 16 ? mode0 : mode1;
		unsigned shift = (bit & 15) * 2;
		if (((modes >> shift) & 3) == 3)
			mask |= UINT32_C(1) << bit;
	}
	machine->pio_output_mask = mask;
	xavix_eeprom24c08_set_lines(&machine->eeprom,
		(mask & (UINT32_C(1) << 20)) ? !!(data & (UINT32_C(1) << 20)) : 0,
		(mask & (UINT32_C(1) << 21)) ? !!(data & (UINT32_C(1) << 21)) : 1);
}

static uint32_t pio_read(const xavix2_machine_t *machine)
{
	uint32_t input = machine->pio_input;
	uint32_t output = load32(machine->mmio + 0x208);
	if (xavix_eeprom24c08_read_sda(&machine->eeprom))
		input |= UINT32_C(1) << 21;
	else
		input &= ~(UINT32_C(1) << 21);
	return (input & ~machine->pio_output_mask) |
		(output & machine->pio_output_mask);
}

static void note_unmapped_read(xavix2_machine_t *machine, uint32_t address)
{
	if (!machine->unmapped_read_count)
		machine->first_unmapped_read = address;
	machine->unmapped_read_count++;
}

static void note_unmapped_write(xavix2_machine_t *machine, uint32_t address)
{
	if (!machine->unmapped_write_count)
		machine->first_unmapped_write = address;
	machine->unmapped_write_count++;
}

static void note_audio_mmio_access(xavix2_machine_t *machine,
	uint32_t offset, uint8_t data, int write)
{
	xavix2_capture_trace_entry *entry;
	if (offset < XAVIX2_AUDIO_MMIO_FIRST ||
		offset >= XAVIX2_AUDIO_MMIO_FIRST + XAVIX2_AUDIO_MMIO_SIZE)
		return;
	entry = &machine->audio_mmio_trace[machine->audio_mmio_trace_next];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)offset;
	entry->data = data;
	entry->write = (uint8_t)write;
	machine->audio_mmio_trace_next =
		(machine->audio_mmio_trace_next + 1) % XAVIX2_AUDIO_TRACE_CAPACITY;
	machine->audio_mmio_trace_total++;
}

static void note_capture_access(xavix2_machine_t *machine, uint32_t offset,
	uint8_t data, int write)
{
	unsigned index = (unsigned)(offset - XAVIX2_CAPTURE_REGISTER_FIRST);
	xavix2_capture_trace_entry *entry;
	if (index >= XAVIX2_CAPTURE_REGISTER_COUNT)
		return;
	if (write)
		machine->capture_write_count[index]++;
	else
		machine->capture_read_count[index]++;
	if (machine->capture_trace_count >= XAVIX2_CAPTURE_TRACE_CAPACITY)
	{
		machine->capture_trace_dropped++;
		return;
	}
	entry = &machine->capture_trace[machine->capture_trace_count++];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)offset;
	entry->data = data;
	entry->write = (uint8_t)(write != 0);
}

static uint8_t capture_read(xavix2_machine_t *machine, uint32_t offset)
{
	uint8_t value;
	if (machine->experimental_capture_readback && offset >= 0x244 && offset <= 0x245)
		value = (uint8_t)(machine->experimental_capture_a >> ((offset - 0x244) * 8));
	else if (machine->experimental_capture_readback && offset >= 0x24a && offset <= 0x24b)
		value = (uint8_t)(machine->experimental_capture_b >> ((offset - 0x24a) * 8));
	else if ((offset >= 0x244 && offset <= 0x245) ||
		(offset >= 0x24a && offset <= 0x24b))
		value = 0;
	else
		value = machine->mmio[offset];
	note_capture_access(machine, offset, value, 0);
	return value;
}

static void note_sensor_buffer_access(xavix2_machine_t *machine,
	uint32_t address, uint8_t data, int write)
{
	xavix2_capture_trace_entry *entry;
	if (write)
		machine->sensor_buffer_write_count++;
	else
		machine->sensor_buffer_read_count++;
	if (machine->sensor_buffer_trace_count >= XAVIX2_CAPTURE_TRACE_CAPACITY)
	{
		machine->sensor_buffer_trace_dropped++;
		return;
	}
	entry = &machine->sensor_buffer_trace[machine->sensor_buffer_trace_count++];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)address;
	entry->data = data;
	entry->write = (uint8_t)(write != 0);
}

static void note_irq_context_access(xavix2_machine_t *machine,
	uint32_t address, uint8_t data, int write)
{
	xavix2_capture_trace_entry *entry;
	if (machine->cpu.total_cycles < machine->diagnostic_trace_start_cycle)
		return;
	if (write)
		machine->irq_context_write_count++;
	else
		machine->irq_context_read_count++;
	if (machine->irq_context_trace_count >= XAVIX2_CAPTURE_TRACE_CAPACITY)
	{
		machine->irq_context_trace_dropped++;
		return;
	}
	entry = &machine->irq_context_trace[machine->irq_context_trace_count++];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)address;
	entry->data = data;
	entry->write = (uint8_t)(write != 0);
}

static void note_sensor_decoded_access(xavix2_machine_t *machine,
	uint32_t address, uint8_t data, int write)
{
	xavix2_capture_trace_entry *entry;
	if (machine->cpu.total_cycles < machine->diagnostic_trace_start_cycle)
		return;
	if (write)
		machine->sensor_decoded_write_count++;
	else
		machine->sensor_decoded_read_count++;
	if (machine->sensor_decoded_trace_count >= XAVIX2_CAPTURE_TRACE_CAPACITY)
	{
		machine->sensor_decoded_trace_dropped++;
		return;
	}
	entry = &machine->sensor_decoded_trace[machine->sensor_decoded_trace_count++];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)address;
	entry->data = data;
	entry->write = (uint8_t)(write != 0);
}

static void note_motion_sample_access(xavix2_machine_t *machine,
	uint32_t address, uint8_t data, int write)
{
	xavix2_capture_trace_entry *entry;
	if (machine->cpu.total_cycles < machine->diagnostic_trace_start_cycle)
		return;
	if (write)
		machine->motion_sample_write_count++;
	else
		machine->motion_sample_read_count++;
	if (machine->motion_sample_trace_count >= XAVIX2_CAPTURE_TRACE_CAPACITY)
	{
		machine->motion_sample_trace_dropped++;
		return;
	}
	entry = &machine->motion_sample_trace[machine->motion_sample_trace_count++];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)address;
	entry->data = data;
	entry->write = (uint8_t)(write != 0);
}

static void note_motion_source_access(xavix2_machine_t *machine,
	uint32_t address, uint8_t data, int write)
{
	xavix2_capture_trace_entry *entry;
	if (machine->cpu.total_cycles < machine->diagnostic_trace_start_cycle)
		return;
	if (write)
		machine->motion_source_write_count++;
	else
		machine->motion_source_read_count++;
	if (machine->motion_source_trace_count >= XAVIX2_CAPTURE_TRACE_CAPACITY)
	{
		machine->motion_source_trace_dropped++;
		return;
	}
	entry = &machine->motion_source_trace[machine->motion_source_trace_count++];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)address;
	entry->data = data;
	entry->write = (uint8_t)(write != 0);
}

static void note_action_state_access(xavix2_machine_t *machine,
	uint32_t address, uint8_t data, int write)
{
	xavix2_capture_trace_entry *entry;
	if (machine->cpu.total_cycles < machine->diagnostic_trace_start_cycle)
		return;
	if (write)
		machine->action_state_write_count++;
	else
		machine->action_state_read_count++;
	if (machine->action_state_trace_count >= XAVIX2_CAPTURE_TRACE_CAPACITY)
	{
		machine->action_state_trace_dropped++;
		return;
	}
	entry = &machine->action_state_trace[machine->action_state_trace_count++];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)address;
	entry->data = data;
	entry->write = (uint8_t)(write != 0);
}

static void note_diagnostic_ram_access(xavix2_machine_t *machine,
	uint32_t address, uint8_t data, int write)
{
	xavix2_capture_trace_entry *entry;
	if (machine->cpu.total_cycles < machine->diagnostic_trace_start_cycle ||
		address < machine->diagnostic_ram_first ||
		address > machine->diagnostic_ram_last)
		return;
	if (write)
		machine->diagnostic_ram_write_count++;
	else
		machine->diagnostic_ram_read_count++;
	if (machine->diagnostic_ram_trace_count >= XAVIX2_CAPTURE_TRACE_CAPACITY)
	{
		machine->diagnostic_ram_trace_dropped++;
		return;
	}
	entry = &machine->diagnostic_ram_trace[machine->diagnostic_ram_trace_count++];
	entry->cycle = machine->cpu.total_cycles;
	entry->pc = machine->cpu.pc;
	entry->offset = (uint16_t)address;
	entry->data = data;
	entry->write = (uint8_t)(write != 0);
}

static uint8_t machine_read8(void *opaque, uint32_t address)
{
	xavix2_machine_t *machine = (xavix2_machine_t *)opaque;
	uint32_t offset;
	uint32_t value;

	if (address < XAVIX2_LOW_RAM_SIZE)
	{
		note_diagnostic_ram_access(machine, address,
			machine->low_ram[address], 0);
		if (address >= XAVIX2_MOTION_SAMPLE_FIRST &&
			address < XAVIX2_MOTION_SAMPLE_FIRST + XAVIX2_MOTION_SAMPLE_SIZE)
			note_motion_sample_access(machine, address,
				machine->low_ram[address], 0);
		if (address >= XAVIX2_IRQ_CONTEXT_FIRST &&
			address < XAVIX2_IRQ_CONTEXT_FIRST + XAVIX2_IRQ_CONTEXT_SIZE)
			note_irq_context_access(machine, address,
				machine->low_ram[address], 0);
		if (address >= XAVIX2_SENSOR_BUFFER_FIRST &&
			address < XAVIX2_SENSOR_BUFFER_FIRST + XAVIX2_SENSOR_BUFFER_SIZE)
			note_sensor_buffer_access(machine, address,
				machine->low_ram[address], 0);
		if (address >= XAVIX2_SENSOR_DECODED_FIRST &&
			address < XAVIX2_SENSOR_DECODED_FIRST + XAVIX2_SENSOR_DECODED_SIZE)
			note_sensor_decoded_access(machine, address,
				machine->low_ram[address], 0);
		if (address >= XAVIX2_MOTION_SOURCE_FIRST &&
			address < XAVIX2_MOTION_SOURCE_FIRST + XAVIX2_MOTION_SOURCE_SIZE)
			note_motion_source_access(machine, address,
				machine->low_ram[address], 0);
		if (address >= XAVIX2_ACTION_STATE_FIRST &&
			address < XAVIX2_ACTION_STATE_FIRST + XAVIX2_ACTION_STATE_SIZE)
			note_action_state_access(machine, address,
				machine->low_ram[address], 0);
		if (machine->pio_input && address >= 0x0da4 && address <= 0x0db3)
		{
			machine->input_state_read_count++;
			machine->last_input_state_read_pc = machine->cpu.pc;
			machine->last_input_state_read_address = (uint16_t)address;
			memcpy(machine->last_input_state_regs, machine->cpu.r,
				sizeof(machine->last_input_state_regs));
		}
		return machine->low_ram[address];
	}
	if (address >= UINT32_C(0x00010000) && address <= UINT32_C(0x00ffffff))
		return address < machine->rom_size ? machine->rom[address] : 0;
	if (address >= UINT32_C(0x40000000) && address <= UINT32_C(0x40ffffff))
	{
		offset = address - UINT32_C(0x40000000);
		return offset < machine->rom_size ? machine->rom[offset] : 0;
	}
	if (address >= UINT32_C(0xc0000000) && address <= UINT32_C(0xc00007ff))
		return machine->palette_ram[address - UINT32_C(0xc0000000)];
	if (address >= UINT32_C(0xc0000800) && address <= UINT32_C(0xc001ffff))
		return machine->video_ram[address - UINT32_C(0xc0000800)];

	if (address >= UINT32_C(0xffffe000))
	{
		offset = address - UINT32_C(0xffffe000);
		if (offset < XAVIX2_MMIO_SIZE)
		{
			machine->mmio_read_counts[offset]++;
			machine->mmio_last_read_pc[offset] = machine->cpu.pc;
			note_audio_mmio_access(machine, offset, machine->mmio[offset], 0);
			if (offset == 0x010)
				return (machine->interrupt_active & (UINT32_C(1) << IRQ_DMA)) ? 6 : 0;
			/*
			 * The IRQ 8 prologue compares the undocumented 16-bit registers at
			 * e244 and e24a until they match.  They are not backed by storage in
			 * the current MAME map, so unknown writes must not make them diverge.
			 */
			if (offset >= XAVIX2_CAPTURE_REGISTER_FIRST &&
				offset < XAVIX2_CAPTURE_REGISTER_FIRST + XAVIX2_CAPTURE_REGISTER_COUNT)
				return capture_read(machine, offset);
			if (offset >= 0x208 && offset <= 0x20b)
			{
				value = pio_read(machine);
				machine->pio_read_count++;
				machine->last_pio_read_pc = machine->cpu.pc;
				machine->last_pio_read_value = value;
				if (machine->pio_input)
				{
					machine->pio_input_read_count++;
					machine->pio_observed_input_or |= value;
				}
				return (uint8_t)(value >> ((offset - 0x208) * 8));
			}
			if (offset == 0x238)
				return 0;
			if (offset == 0x239)
				return 2;
			if (offset == 0x60a) return 0x40;
			if (offset == 0x60b) return 0x02;
			if (offset == 0x630 || offset == 0x632) return 0x10;
			if (offset == 0x631 || offset == 0x633) return 0x02;
			/*
			 * Firmware in multiple titles uses bits 1:0 as a debounce
			 * counter while bit 2 reflects an external controller/power
			 * status line.  A write must not clear that input line.
			 */
			if (offset == CONTROLLER_POWER_STATUS_REGISTER)
				return (machine->mmio[offset] &
					CONTROLLER_POWER_STATUS_COUNTER_MASK) |
					CONTROLLER_POWER_STATUS_GOOD;
			if (offset >= 0xa10 && offset <= 0xa17)
				return xavix2_audio_status(&machine->audio, offset - 0xa10);
			if (offset == 0x1c00)
			{
				machine->irq_level_read_count++;
				if (machine->interrupt_latched_valid)
					return machine->interrupt_latched_level;
				{
					unsigned level = first_interrupt_level(machine->interrupt_pending);
					if (level < 32)
						return (uint8_t)level;
				}
				return 0xff;
			}
			if (offset == 0x1c08 || offset == 0x1c09)
				return (uint8_t)(machine->interrupt_nmi >> ((offset - 0x1c08) * 8));
			if (offset == 0x1c0a || offset == 0x1c0b)
				return (uint8_t)(machine->interrupt_enabled >> ((offset - 0x1c0a) * 8));
			return machine->mmio[offset];
		}
	}

	note_unmapped_read(machine, address);
	return 0;
}

static void dma_start(xavix2_machine_t *machine, uint8_t control)
{
	uint32_t source;
	uint32_t destination;
	uint16_t count;
	uint32_t index;

	if (control != 3 && control != 7)
		return;
	source = load32(machine->mmio + 0x000);
	if (control == 7)
		source |= UINT32_C(0x40000000);
	destination = load16(machine->mmio + 0x004);
	count = load16(machine->mmio + 0x008);
	for (index = 0; index < count; ++index)
	{
		uint8_t data = machine_read8(machine, source + index);
		if (destination + index < XAVIX2_LOW_RAM_SIZE)
		{
			machine->program_ram[destination + index] = data;
			machine->low_ram[destination + index] = data;
		}
		else
			note_unmapped_write(machine, destination + index);
	}
	machine->dma_transfer_count++;
	machine->dma_completion_cycle = machine->cpu.total_cycles + (count ? count : 1);
}

static uint32_t rgb555(uint16_t color)
{
	uint32_t r = color & 31;
	uint32_t g = (color >> 5) & 31;
	uint32_t b = (color >> 10) & 31;
	r = (r << 3) | (r >> 2);
	g = (g << 3) | (g >> 2);
	b = (b << 3) | (b >> 2);
	return UINT32_C(0xff000000) | (r << 16) | (g << 8) | b;
}

static int compare_gpu_order(const void *left, const void *right)
{
	uint32_t a = *(const uint32_t *)left;
	uint32_t b = *(const uint32_t *)right;
	uint32_t a_priority = a & UINT32_C(0x1fe00000);
	uint32_t b_priority = b & UINT32_C(0x1fe00000);
	if (a_priority != b_priority)
		return a_priority < b_priority ? 1 : -1;
	/* Preserve command-list submission order when priorities are equal. */
	return (a & UINT32_C(0xffff)) < (b & UINT32_C(0xffff)) ? -1 :
		(a & UINT32_C(0xffff)) > (b & UINT32_C(0xffff)) ? 1 : 0;
}

static void render_gpu(xavix2_machine_t *machine, uint16_t count, uint16_t address)
{
	uint32_t *order;
	uint16_t descsize_address = load16(machine->mmio + 0x608);
	uint16_t descdata_address = load16(machine->mmio + 0x622);
	uint32_t list_index;

	if (!count)
		return;
	order = (uint32_t *)malloc((size_t)count * sizeof(*order));
	if (!order)
		return;
	for (list_index = 0; list_index < count; ++list_index)
	{
		uint64_t command = machine_read64(machine, (uint32_t)address + 8 * list_index);
		order[list_index] = (uint32_t)(command & UINT64_C(0x1fe00000)) | list_index;
	}
	qsort(order, count, sizeof(*order), compare_gpu_order);

	for (list_index = 0; list_index < count; ++list_index)
	{
		uint32_t command_index = order[list_index] & UINT32_C(0xffff);
		uint64_t command = machine_read64(machine, (uint32_t)address + 8 * command_index);
		uint32_t descriptor_index = (uint32_t)((command >> 30) & 0x3f);
		uint32_t data_index = (uint32_t)((command >> 58) & 0x3f);
		uint32_t descsize = (uint32_t)machine_read8(machine, descsize_address + 4 * descriptor_index) |
			((uint32_t)machine_read8(machine, descsize_address + 4 * descriptor_index + 1) << 8) |
			((uint32_t)machine_read8(machine, descsize_address + 4 * descriptor_index + 2) << 16) |
			((uint32_t)machine_read8(machine, descsize_address + 4 * descriptor_index + 3) << 24);
		uint16_t descdata = (uint16_t)(machine_read8(machine, descdata_address + 2 * data_index) |
			((uint16_t)machine_read8(machine, descdata_address + 2 * data_index + 1) << 8));
		uint32_t source = ((uint32_t)descdata << 14) + (uint32_t)((command >> 43) & 0x7fe0);
		uint32_t x = (uint32_t)(command & 0x7ff);
		uint32_t y = (uint32_t)((command >> 11) & 0x3ff);
		uint32_t sx = 1 + (descsize & 0xff);
		uint32_t sy = 1 + ((descsize >> 8) & 0xff);
		/* The six-bit W/H command fields are unsigned Q2.4 scale factors.
		 * Taking only their upper two bits, as the preliminary MAME model
		 * did, leaves one-pixel seams between Blue Dragon's 16-pixel menu
		 * tiles: those commands use 0x11 (17/16 scale) and place successive
		 * tiles 17 pixels apart. */
		uint32_t scale_x = (uint32_t)((command >> 36) & 0x3f);
		uint32_t scale_y = (uint32_t)((command >> 42) & 0x3f);
		uint32_t bpp = 1 + ((descsize >> 24) & 7);
		uint32_t mask = (UINT32_C(1) << bpp) - 1;
		uint32_t palette_base = ((descsize >> 27) & 0x1f) << bpp;
		uint32_t yy;
		uint64_t packed = 0;
		uint32_t available = 0;

		for (yy = 0; yy < sy; ++yy)
		{
			uint32_t xx;
			packed = machine_read64(machine, source);
			source += 8;
			available = 64;
			for (xx = 0; xx < sx; ++xx)
			{
				uint32_t palette_index;
				uint16_t raw_color;
				uint32_t color;
				uint32_t draw_y_first;
				uint32_t draw_y_end;
				if (available < bpp)
				{
					packed = machine_read64(machine, source);
					source += 8;
					available = 64;
				}
				palette_index = palette_base | ((uint32_t)packed & mask);
				raw_color = palette_index < 0x200 ?
					load16(machine->palette_ram + palette_index * 4) : UINT16_C(0x8000);
				color = raw_color & 0x8000 ? 0 : rgb555(raw_color);
				if (color)
				{
					uint32_t draw_x_first = x +
						(uint32_t)(((uint64_t)xx * scale_x) >> 4);
					uint32_t draw_x_end = x +
						(uint32_t)(((uint64_t)(xx + 1) * scale_x) >> 4);
					draw_y_first = y +
						(uint32_t)(((uint64_t)yy * scale_y) >> 4);
					draw_y_end = y +
						(uint32_t)(((uint64_t)(yy + 1) * scale_y) >> 4);
					for (; draw_y_first < draw_y_end; ++draw_y_first)
					{
						uint32_t draw_x;
						if (draw_y_first >= 0x400)
							continue;
						for (draw_x = draw_x_first; draw_x < draw_x_end; ++draw_x)
						{
							if (draw_x < 0x800)
							{
								machine->screen_data[draw_y_first * 0x800 + draw_x] = color;
								machine->gpu_pixel_write_count++;
							}
						}
					}
				}
				packed >>= bpp;
				available -= bpp;
			}
		}
	}
	free(order);
}

static void gpu_trigger(xavix2_machine_t *machine, uint32_t offset)
{
	uint16_t address = offset == 0x408 ? load16(machine->mmio + 0x400) :
		load16(machine->mmio + 0x40c);
	uint16_t count = offset == 0x408 ? load16(machine->mmio + 0x404) :
		load16(machine->mmio + 0x410);
	if (!machine->gpu_trigger_count)
		machine->first_gpu_pc = machine->cpu.pc;
	machine->gpu_trigger_count++;
	machine->last_gpu_pc = machine->cpu.pc;
	machine->last_gpu_register = UINT32_C(0xffffe000) + offset;
	machine->last_gpu_count = count;
	if (count > machine->maximum_gpu_count)
		machine->maximum_gpu_count = count;
	render_gpu(machine, count, address);
}

static void machine_write8(void *opaque, uint32_t address, uint8_t data)
{
	xavix2_machine_t *machine = (xavix2_machine_t *)opaque;
	uint32_t offset;

	if (address < XAVIX2_LOW_RAM_SIZE)
	{
		machine->low_ram[address] = data;
		note_diagnostic_ram_access(machine, address, data, 1);
		if (address >= XAVIX2_MOTION_SAMPLE_FIRST &&
			address < XAVIX2_MOTION_SAMPLE_FIRST + XAVIX2_MOTION_SAMPLE_SIZE)
			note_motion_sample_access(machine, address, data, 1);
		if (address >= XAVIX2_IRQ_CONTEXT_FIRST &&
			address < XAVIX2_IRQ_CONTEXT_FIRST + XAVIX2_IRQ_CONTEXT_SIZE)
			note_irq_context_access(machine, address, data, 1);
		if (address >= XAVIX2_SENSOR_BUFFER_FIRST &&
			address < XAVIX2_SENSOR_BUFFER_FIRST + XAVIX2_SENSOR_BUFFER_SIZE)
			note_sensor_buffer_access(machine, address, data, 1);
		if (address >= XAVIX2_SENSOR_DECODED_FIRST &&
			address < XAVIX2_SENSOR_DECODED_FIRST + XAVIX2_SENSOR_DECODED_SIZE)
			note_sensor_decoded_access(machine, address, data, 1);
		if (address >= XAVIX2_MOTION_SOURCE_FIRST &&
			address < XAVIX2_MOTION_SOURCE_FIRST + XAVIX2_MOTION_SOURCE_SIZE)
			note_motion_source_access(machine, address, data, 1);
		if (address >= XAVIX2_ACTION_STATE_FIRST &&
			address < XAVIX2_ACTION_STATE_FIRST + XAVIX2_ACTION_STATE_SIZE)
			note_action_state_access(machine, address, data, 1);
		return;
	}
	if (address >= UINT32_C(0xc0000000) && address <= UINT32_C(0xc00007ff))
	{
		machine->palette_ram[address - UINT32_C(0xc0000000)] = data;
		return;
	}
	if (address >= UINT32_C(0xc0000800) && address <= UINT32_C(0xc001ffff))
	{
		machine->video_ram[address - UINT32_C(0xc0000800)] = data;
		return;
	}
	if (address >= UINT32_C(0xffffe000))
	{
		offset = address - UINT32_C(0xffffe000);
		if (offset < XAVIX2_MMIO_SIZE)
		{
			machine->mmio_write_counts[offset]++;
			machine->mmio_last_write_pc[offset] = machine->cpu.pc;
			note_audio_mmio_access(machine, offset, data, 1);
			if (offset == CONTROLLER_POWER_STATUS_REGISTER)
				machine->mmio[offset] = data &
					CONTROLLER_POWER_STATUS_COUNTER_MASK;
			else
				machine->mmio[offset] = data;
			if (offset == 0xa0b)
				xavix2_audio_command(&machine->audio,
					load16(machine->mmio + 0xa0a),
					machine->video_ram + AUDIO_DESCRIPTOR_RAM_OFFSET,
					load16(machine->mmio + 0xa18), machine->mmio[0xa1c],
					machine->mmio[0xa1d]);
			if (offset >= XAVIX2_CAPTURE_REGISTER_FIRST &&
				offset < XAVIX2_CAPTURE_REGISTER_FIRST + XAVIX2_CAPTURE_REGISTER_COUNT)
				note_capture_access(machine, offset, data, 1);
			if (offset == 0x00c)
				dma_start(machine, data);
			else if (offset == 0x010 && data == 2)
				clear_interrupts(machine, UINT32_C(1) << IRQ_DMA);
			else if ((offset >= 0x200 && offset <= 0x20b))
				update_pio(machine);
			else if (offset == 0x238)
			{
				if (data == '\n')
					machine->debug_length = 0;
				else if (data != '\r' && data && machine->debug_length + 1 < sizeof(machine->debug_text))
					machine->debug_text[machine->debug_length++] = data;
			}
			else if (offset == 0x408 || offset == 0x414)
				gpu_trigger(machine, offset);
			else if (offset == 0x1c04 || offset == 0x1c05)
			{
				unsigned shift = (offset - 0x1c04) * 8;
				machine->last_irq_clear_mask = (uint16_t)((machine->last_irq_clear_mask &
					~(UINT16_C(0xff) << shift)) | ((uint16_t)data << shift));
				machine->last_irq_clear_pc = machine->cpu.pc;
				machine->irq_clear_write_count++;
				clear_interrupts(machine,
					(uint32_t)data << ((offset - 0x1c04) * 8));
			}
			else if (offset == 0x1c08 || offset == 0x1c09)
			{
				unsigned shift = (offset - 0x1c08) * 8;
				machine->interrupt_nmi = (machine->interrupt_nmi & ~(UINT32_C(0xff) << shift)) |
					((uint32_t)data << shift);
			}
			else if (offset == 0x1c0a || offset == 0x1c0b)
			{
				unsigned shift = (offset - 0x1c0a) * 8;
				machine->interrupt_enabled = (machine->interrupt_enabled & ~(UINT32_C(0xff) << shift)) |
					((uint32_t)data << shift);
				clear_interrupts(machine,
					~(machine->interrupt_enabled | machine->interrupt_nmi));
			}
			return;
		}
	}

	note_unmapped_write(machine, address);
}

int xavix2_machine_init(xavix2_machine_t *machine, const uint8_t *rom,
	size_t rom_size)
{
	if (!machine || !rom || !rom_size)
		return 0;
	memset(machine, 0, sizeof(*machine));
	machine->rom = rom;
	machine->rom_size = rom_size;
	memset(machine->eeprom.data, 0xff, sizeof(machine->eeprom.data));
	xavix_eeprom24c08_init(&machine->eeprom, NULL, 0);
	xavix2_audio_init(&machine->audio, rom, rom_size);
	machine->motion_packet_address = XAVIX2_MOTION_PACKET_FIRST;
	xavix2_cpu_init(&machine->cpu, machine_read8, machine_write8, machine);
	xavix2_cpu_set_fetch(&machine->cpu, machine_fetch8);
	xavix2_cpu_set_interrupt_ack(&machine->cpu, acknowledge_interrupt, machine);
	machine->next_vblank_cycle = XAVIX2_CYCLES_PER_FRAME;
	return 1;
}

void xavix2_machine_set_motion_packet_address(xavix2_machine_t *machine,
	uint16_t address)
{
	if (!machine || (uint32_t)address + XAVIX2_MOTION_PACKET_SIZE >
		XAVIX2_LOW_RAM_SIZE)
		return;
	machine->motion_packet_address = address;
}

void xavix2_machine_reset(xavix2_machine_t *machine)
{
	const uint8_t *rom;
	size_t rom_size;
	uint16_t motion_packet_address;
	uint8_t eeprom[XAVIX_EEPROM24C08_SIZE];
	if (!machine)
		return;
	rom = machine->rom;
	rom_size = machine->rom_size;
	motion_packet_address = machine->motion_packet_address;
	memcpy(eeprom, machine->eeprom.data, sizeof(eeprom));
	(void)xavix2_machine_init(machine, rom, rom_size);
	xavix2_machine_set_motion_packet_address(machine, motion_packet_address);
	(void)xavix_eeprom24c08_load_image(&machine->eeprom, eeprom, sizeof(eeprom));
}

static uint64_t next_event_cycle(const xavix2_machine_t *machine)
{
	uint64_t next = machine->next_vblank_cycle;
	if (machine->dma_completion_cycle && machine->dma_completion_cycle < next)
		next = machine->dma_completion_cycle;
	return next;
}

static void process_events(xavix2_machine_t *machine)
{
	while (machine->cpu.total_cycles >= machine->next_vblank_cycle)
	{
		uint16_t background = load16(machine->mmio + 0x60e);
		uint32_t color = rgb555(background);
		uint32_t pixel;
		for (pixel = 0; pixel < 0x400 * 0x800; ++pixel)
			machine->screen_data[pixel] = color;
		machine->frame_count++;
		if (machine->experimental_direct_pio_sample)
		{
			int changed = sample_experimental_pio(machine);
			if (machine->experimental_dispatch_input)
				machine->experimental_callback_pending |= changed;
		}
		raise_interrupt(machine, IRQ_TIMER);
		machine->next_vblank_cycle += XAVIX2_CYCLES_PER_FRAME;
	}
	if (machine->dma_completion_cycle &&
		machine->cpu.total_cycles >= machine->dma_completion_cycle)
	{
		machine->dma_completion_cycle = 0;
		raise_interrupt(machine, IRQ_DMA);
	}
}

uint64_t xavix2_machine_execute(xavix2_machine_t *machine,
	uint64_t cycle_budget)
{
	uint64_t start;
	uint64_t target;
	if (!machine || !cycle_budget)
		return 0;
	start = machine->cpu.total_cycles;
	target = start + cycle_budget;
	while (machine->cpu.total_cycles < target)
	{
		uint64_t event_cycle;
		uint64_t remaining;
		uint32_t slice;
		uint32_t used;

		process_events(machine);
		if (machine->cpu.waiting && !machine->cpu.interrupt_line)
		{
			if (machine->experimental_dispatch_input &&
				machine->experimental_callback_pending)
			{
				machine->experimental_callback_pending = 0;
				machine->cpu.r[7] = machine->cpu.pc;
				machine->cpu.pc = machine->experimental_callback_address ?
					machine->experimental_callback_address : UINT32_C(0x00020017);
				machine->cpu.waiting = 0;
				continue;
			}
			event_cycle = next_event_cycle(machine);
			machine->cpu.total_cycles = event_cycle < target ? event_cycle : target;
			process_events(machine);
			if (machine->cpu.total_cycles == target && !machine->cpu.interrupt_line)
				break;
		}

		remaining = target - machine->cpu.total_cycles;
		event_cycle = next_event_cycle(machine);
		if (event_cycle > machine->cpu.total_cycles &&
			event_cycle - machine->cpu.total_cycles < remaining)
			remaining = event_cycle - machine->cpu.total_cycles;
		slice = remaining > 4096 ? 4096 : (uint32_t)remaining;
		if (!slice)
		{
			process_events(machine);
			continue;
		}
		used = xavix2_cpu_execute(&machine->cpu, slice);
		if (!used && !machine->cpu.waiting)
			break;
	}
	process_events(machine);
	return machine->cpu.total_cycles - start;
}

uint64_t xavix2_machine_run_video_frame(xavix2_machine_t *machine,
	const uint8_t motion_packet[XAVIX2_MOTION_PACKET_SIZE],
	uint32_t pio_input)
{
	uint64_t start;
	uint64_t target;
	const uint64_t render_margin = 64;

	if (!machine)
		return 0;
	start = machine->cpu.total_cycles;
	if (motion_packet)
		memcpy(machine->low_ram + machine->motion_packet_address,
			motion_packet, XAVIX2_MOTION_PACKET_SIZE);
	machine->pio_input = pio_input;
	machine->experimental_direct_pio_sample = 1;

	/* The two wrist reflectors are latched by the level-10 handler.  Pulse the
	 * line only at the firmware's wait point, matching the observed hardware
	 * cadence and avoiding interference with another active interrupt. */
	if (machine->cpu.waiting && !machine->interrupt_active)
	{
		uint64_t reads_before = machine->irq_level_read_count;
		unsigned steps;
		xavix2_machine_raise_irq(machine, IRQ_MOTION);
		if (machine->interrupt_active)
		{
			for (steps = 0; steps < 256 &&
				machine->irq_level_read_count == reads_before; ++steps)
				(void)xavix2_cpu_execute(&machine->cpu, 1);
			xavix2_machine_clear_irq(machine, IRQ_MOTION);
		}
	}

	/* Stop just before vertical blank clears the command-list framebuffer.
	 * The next call crosses that boundary and renders the following frame. */
	if (machine->cpu.total_cycles + render_margin >=
		machine->next_vblank_cycle)
	{
		uint64_t remaining = machine->next_vblank_cycle -
			machine->cpu.total_cycles;
		(void)xavix2_machine_execute(machine, remaining + 1);
	}
	target = machine->next_vblank_cycle > render_margin ?
		machine->next_vblank_cycle - render_margin :
		machine->next_vblank_cycle;
	if (machine->cpu.total_cycles < target)
		(void)xavix2_machine_execute(machine,
			target - machine->cpu.total_cycles);
	xavix2_audio_render(&machine->audio,
		xavix2_audio_engine_rate(machine->mmio[0xa00], machine->mmio[0xa05]));
	return machine->cpu.total_cycles - start;
}

const uint32_t *xavix2_machine_visible_frame(const xavix2_machine_t *machine,
	unsigned *width, unsigned *height, unsigned *stride)
{
	if (!machine)
		return NULL;
	if (width) *width = 320;
	if (height) *height = 240;
	if (stride) *stride = 0x800;
	return machine->screen_data + (0x200 - 120) * 0x800 + (0x400 - 160);
}

const int16_t *xavix2_machine_frame_audio(const xavix2_machine_t *machine)
{
	return machine ? xavix2_audio_frame(&machine->audio) : NULL;
}
