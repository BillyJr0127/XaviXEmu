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
	GPU_STATUS_REGISTER = 0x60a,
	GPU_STATUS_VIDEO_MODE = 0x0040,
	GPU_STATUS_TRIANGLE_READY = 0x0100,
	GPU_STATUS_SPRITE_READY = 0x0200,
	PROJECTOR_COMMAND_REGISTER = 0x858,
	PROJECTOR_FOCAL_REGISTER = 0x840,
	PROJECTOR_NEAR_REGISTER = 0x846,
	PROJECTOR_COUNT_REGISTER = 0x85a,
	PROJECTOR_OUTPUT_COUNT_REGISTER = 0x85c,
	PROJECTOR_SOURCE_REGISTER = 0x860,
	PROJECTOR_DESTINATION_REGISTER = 0x862,
	PROJECTOR_POLYGON_REGISTER = 0x864,
	CONTROLLER_POWER_STATUS_REGISTER = 0xc48,
	CONTROLLER_POWER_STATUS_COUNTER_MASK = 0x03,
	CONTROLLER_POWER_STATUS_GOOD = 0x04
};

enum
{
	XAVIX2_STATE_HEADER_SIZE = 16,
	XAVIX2_STATE_VERSION = 2,
	XAVIX2_STATE_LEGACY_VERSION = 1
};

static const uint8_t XAVIX2_STATE_MAGIC[8] =
{
	'X', 'A', 'V', 'I', 'X', '2', 'S', 'T'
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

static void store16(uint8_t *data, uint16_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
}

static int32_t geometry_matrix_product(int32_t left, int32_t right)
{
	return (int32_t)(((int64_t)left * right) >> 24);
}

static void geometry_matrix_multiply(xavix2_machine_t *machine,
	uint16_t source, uint16_t destination)
{
	int32_t left[12];
	int32_t right[12];
	int32_t result[12];
	unsigned row;
	unsigned column;
	unsigned inner;

	if ((uint32_t)source + sizeof(right) > XAVIX2_LOW_RAM_SIZE ||
		(uint32_t)destination + sizeof(result) > XAVIX2_LOW_RAM_SIZE)
		return;
	for (row = 0; row < 12; ++row)
	{
		left[row] = (int32_t)load32(machine->mmio + 0x800 + row * 4);
		right[row] = (int32_t)load32(machine->low_ram + source + row * 4);
	}
	for (row = 0; row < 3; ++row)
	{
		for (column = 0; column < 4; ++column)
		{
			int64_t sum = column == 3 ? left[row * 4 + 3] : 0;
			for (inner = 0; inner < 3; ++inner)
				sum += geometry_matrix_product(left[row * 4 + inner],
					right[inner * 4 + column]);
			result[row * 4 + column] = (int32_t)sum;
		}
	}
	for (row = 0; row < 12; ++row)
		store32(machine->low_ram + destination + row * 4,
			(uint32_t)result[row]);
}

static int32_t geometry_signed10(uint32_t value)
{
	return (int32_t)(value << 22) >> 22;
}

static void geometry_transform_normals(xavix2_machine_t *machine,
	uint16_t source, uint16_t destination, uint32_t count)
{
	int32_t rotation[9];
	uint32_t index;
	unsigned row;
	unsigned column;

	if ((uint32_t)source + count * 4 > XAVIX2_LOW_RAM_SIZE ||
		(uint32_t)destination + count * 12 > XAVIX2_LOW_RAM_SIZE)
		return;
	/* Command 0F transforms packed signed XYZ10 surface normals without the
	 * affine translation used by command 0C.  Its firmware wrapper copies the
	 * 3x3 rotation terms from a Q16.16 3x4 matrix, arithmetic-shifts every
	 * coefficient right by eight, and writes the resulting signed Q8.8 matrix
	 * contiguously at E800..E820.  The command expands each result to three
	 * signed 32-bit components for the following 0B/0E lighting stages. */
	for (row = 0; row < 3; ++row)
		for (column = 0; column < 3; ++column)
			rotation[row * 3 + column] = (int32_t)load32(
				machine->mmio + 0x800 + (row * 3 + column) * 4);
	for (index = 0; index < count; ++index)
	{
		uint32_t packed = load32(machine->low_ram + source + index * 4);
		int32_t coordinate[3];
		coordinate[0] = geometry_signed10(packed);
		coordinate[1] = geometry_signed10(packed >> 10);
		coordinate[2] = geometry_signed10(packed >> 20);
		for (row = 0; row < 3; ++row)
		{
			int64_t result = 0;
			for (column = 0; column < 3; ++column)
				result += (int64_t)rotation[row * 3 + column] *
					coordinate[column];
			store32(machine->low_ram + destination + index * 12 + row * 4,
				(uint32_t)(int32_t)(result >> 8));
		}
	}
}

static void geometry_transform_vertices(xavix2_machine_t *machine,
	uint16_t source, uint16_t destination, uint32_t count)
{
	int32_t rotation[9];
	int32_t translation[3];
	uint32_t index;
	unsigned row;
	unsigned column;

	if ((uint32_t)source + count * 4 > XAVIX2_LOW_RAM_SIZE ||
		(uint32_t)destination + count * 12 > XAVIX2_LOW_RAM_SIZE)
		return;
	/* Command 0C consumes nine signed 32-bit coefficient registers at E800,
	 * E804, E808, ... and translations at E80C/E81C/E82C.  Its wrapper updates
	 * only each coefficient's low word after command 10 has populated the full
	 * register, intentionally preserving the sign/upper half. */
	for (row = 0; row < 3; ++row)
	{
		for (column = 0; column < 3; ++column)
			rotation[row * 3 + column] = (int32_t)load32(
				machine->mmio + 0x800 + row * 0x10 + column * 4);
		translation[row] = (int32_t)load32(
			machine->mmio + 0x80c + row * 0x10);
	}
	for (index = 0; index < count; ++index)
	{
		uint32_t packed = load32(machine->low_ram + source + index * 4);
		int32_t coordinate[3];
		coordinate[0] = geometry_signed10(packed);
		coordinate[1] = geometry_signed10(packed >> 10);
		coordinate[2] = geometry_signed10(packed >> 20);
		for (row = 0; row < 3; ++row)
		{
			int64_t result = translation[row];
			for (column = 0; column < 3; ++column)
				result += (int64_t)rotation[row * 3 + column] *
					coordinate[column];
			store32(machine->low_ram + destination + index * 12 + row * 4,
				(uint32_t)result);
		}
	}
}

static void geometry_project_triangles(xavix2_machine_t *machine,
	uint16_t source, uint16_t polygon_address, uint32_t count)
{
	int32_t focal = (int16_t)load16(machine->mmio + PROJECTOR_FOCAL_REGISTER);
	int32_t near_depth = (int32_t)(int16_t)load16(machine->mmio +
		PROJECTOR_NEAR_REGISTER) * INT32_C(65536);
	int32_t center_x = (int32_t)load16(machine->mmio + 0x848) / 2;
	int32_t center_y = (int32_t)load16(machine->mmio + 0x84a) / 2;
	uint32_t input_index;
	uint32_t output_count = 0;

	store16(machine->mmio + PROJECTOR_OUTPUT_COUNT_REGISTER, 0);
	if ((uint32_t)polygon_address + count * 16 > XAVIX2_LOW_RAM_SIZE)
		return;
	for (input_index = 0; input_index < count; ++input_index)
	{
		uint8_t *input = machine->low_ram + polygon_address + input_index * 16;
		uint32_t d0 = load32(input);
		uint32_t d1 = load32(input + 4);
		uint32_t d2 = load32(input + 8);
		uint32_t d3 = load32(input + 12);
		uint32_t vertex_index[3] = {
			d0 >> 16,
			d1 & 0xffff,
			d1 >> 16
		};
		int32_t projected_x[3];
		int32_t projected_y[3];
		unsigned vertex;
		int valid = 1;
		int64_t area;

		for (vertex = 0; vertex < 3; ++vertex)
		{
			uint32_t vertex_address = (uint32_t)source + vertex_index[vertex] * 12;
			int32_t x;
			int32_t y;
			int32_t z;
			int64_t screen_x;
			int64_t screen_y;
			if (vertex_address + 12 > XAVIX2_LOW_RAM_SIZE)
			{
				valid = 0;
				break;
			}
			x = (int32_t)load32(machine->low_ram + vertex_address);
			y = (int32_t)load32(machine->low_ram + vertex_address + 4);
			z = (int32_t)load32(machine->low_ram + vertex_address + 8);
			/* E846 is the guest-selected near plane in integer world units.
			 * DB2J/DBZ feed command 4D Q16.16 vertices; accepting a triangle
			 * which crosses this plane turns it into a screen-sized slab.  Cull
			 * that primitive until the hardware's exact clipped-edge output is
			 * characterized. */
			if (z <= 0 || z < near_depth)
			{
				valid = 0;
				break;
			}
			screen_x = center_x + (int64_t)x * focal / z;
			screen_y = center_y + (int64_t)y * focal / z;
			/* Values this far outside the 11x10-bit render target cannot cross
			 * its viewport and would make a host-side area product needlessly
			 * large.  Normal guest geometry is well inside this guard. */
			if (screen_x < -65536 || screen_x > 65535 ||
				screen_y < -65536 || screen_y > 65535)
			{
				valid = 0;
				break;
			}
			projected_x[vertex] = (int32_t)screen_x;
			projected_y[vertex] = (int32_t)screen_y;
		}
		if (!valid)
			continue;
		area = (int64_t)(projected_x[1] - projected_x[0]) *
			(projected_y[2] - projected_y[0]) -
			(int64_t)(projected_y[1] - projected_y[0]) *
			(projected_x[2] - projected_x[0]);
		/* Screen Y grows downward, so positive screen-space winding is the
		 * mathematical front face used by the firmware's indexed records. */
		if (area <= 0)
			continue;
		{
			int32_t packed_x[3] = {
				projected_x[0] & 0x7ff,
				projected_x[1] & 0x7ff,
				projected_x[2] & 0x7ff
			};
			int32_t packed_y[3] = {
				projected_y[0] & 0x3ff,
				projected_y[1] & 0x3ff,
				projected_y[2] & 0x3ff
			};
			int64_t packed_area =
				(int64_t)(packed_x[1] - packed_x[0]) *
					(packed_y[2] - packed_y[0]) -
				(int64_t)(packed_y[1] - packed_y[0]) *
					(packed_x[2] - packed_x[0]);
			uint8_t *output = machine->low_ram + polygon_address + output_count * 16;

			/* Full hardware clips the primitive to its selected viewport before
			 * packing 11x10-bit screen coordinates.  Until that edge generator is
			 * modeled, reject a triangle whose wrapped coordinates reverse its
			 * front-face winding.  Keeping it produces a screen-sized polygon (the
			 * DBZ battle's black slab) rather than a clipped edge. */
			if (packed_area <= 0)
				continue;
			uint32_t packed0 = (d0 & 1) |
				((uint32_t)packed_y[0] << 1) |
				((uint32_t)packed_x[0] << 11) |
				((uint32_t)packed_y[1] << 22);
			uint32_t packed1 = (uint32_t)packed_x[1] |
				((uint32_t)packed_y[2] << 11) |
				((uint32_t)packed_x[2] << 21);
			store32(output, packed0);
			store32(output + 4, packed1);
			store32(output + 8, d2);
			store32(output + 12, d3);
			output_count++;
		}
	}
	store16(machine->mmio + PROJECTOR_OUTPUT_COUNT_REGISTER,
		(uint16_t)output_count);
}

static uint16_t geometry_integer_sqrt(uint32_t value)
{
	uint32_t result = 0;
	uint32_t bit = UINT32_C(1) << 30;

	while (bit > value)
		bit >>= 2;
	while (bit)
	{
		if (value >= result + bit)
		{
			value -= result + bit;
			result = (result >> 1) + bit;
		}
		else
			result >>= 1;
		bit >>= 2;
	}
	return (uint16_t)result;
}

static void projector_start(xavix2_machine_t *machine, uint8_t command)
{
	uint32_t source;
	uint32_t destination;
	uint32_t count;
	int32_t focal;
	uint32_t index;

	/* Command 2 projects signed Q16.16 depth/vertical/horizontal triples.
	 * Firmware applies the final /2 and screen-center translation itself.
	 * The unit is synchronous from the CPU's point of view, so E859's busy
	 * bit can remain clear after this routine returns. */
	if (command == 0x10)
	{
		geometry_matrix_multiply(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_DESTINATION_REGISTER));
		return;
	}
	if (command == 0x0c)
	{
		geometry_transform_vertices(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_DESTINATION_REGISTER),
			(uint32_t)load16(machine->mmio + PROJECTOR_COUNT_REGISTER) + 1);
		return;
	}
	if (command == 0x0f)
	{
		geometry_transform_normals(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_DESTINATION_REGISTER),
			(uint32_t)load16(machine->mmio + PROJECTOR_COUNT_REGISTER) + 1);
		return;
	}
	if (command == 0x4d)
	{
		geometry_project_triangles(machine,
			load16(machine->mmio + PROJECTOR_SOURCE_REGISTER),
			load16(machine->mmio + PROJECTOR_POLYGON_REGISTER),
			(uint32_t)load16(machine->mmio + PROJECTOR_COUNT_REGISTER) + 1);
		return;
	}
	if (command == 0x11)
	{
		uint32_t input = load32(machine->mmio + 0x800);
		uint16_t result = geometry_integer_sqrt(input);
		/* Firmware uses command 11 as an unsigned 32-bit square root.  The
		 * input is written to E800 and the 16-bit floor result is read from
		 * E804.  Naruto uses it for live two-dimensional hit distances. */
		store16(machine->mmio + 0x804, result);
		return;
	}
	if (command != 2)
		return;
	source = load16(machine->mmio + PROJECTOR_SOURCE_REGISTER);
	destination = load16(machine->mmio + PROJECTOR_DESTINATION_REGISTER);
	count = (uint32_t)load16(machine->mmio + PROJECTOR_COUNT_REGISTER) + 1;
	focal = (int16_t)load16(machine->mmio + PROJECTOR_FOCAL_REGISTER);
	if (source + count * 12 > XAVIX2_LOW_RAM_SIZE ||
		destination + count * 8 > XAVIX2_LOW_RAM_SIZE)
		return;

	for (index = 0; index < count; ++index)
	{
		const uint8_t *input = machine->low_ram + source + index * 12;
		uint8_t *output = machine->low_ram + destination + index * 8;
		int32_t depth = (int32_t)load32(input);
		int32_t vertical = (int32_t)load32(input + 4);
		int32_t horizontal = (int32_t)load32(input + 8);
		int64_t projected_vertical = 0;
		int64_t projected_horizontal = 0;

		if (depth > 0)
		{
			projected_vertical = ((int64_t)vertical * focal * 2) / depth;
			projected_horizontal = ((int64_t)horizontal * focal * 2) / depth;
		}
		store16(output + 4, (uint16_t)projected_vertical);
		store16(output + 6, (uint16_t)projected_horizontal);
	}
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
			if (offset == GPU_STATUS_REGISTER ||
				offset == GPU_STATUS_REGISTER + 1)
			{
				uint16_t status = GPU_STATUS_VIDEO_MODE |
					GPU_STATUS_TRIANGLE_READY |
					GPU_STATUS_SPRITE_READY;
				return (uint8_t)(status >>
					((offset - GPU_STATUS_REGISTER) * 8));
			}
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

static int64_t triangle_edge(int32_t ax, int32_t ay, int32_t bx, int32_t by,
	int32_t px, int32_t py)
{
	return (int64_t)(px - ax) * (by - ay) -
		(int64_t)(py - ay) * (bx - ax);
}

static int triangle_texture_color(xavix2_machine_t *machine,
	uint16_t segment_table, uint32_t descriptor, uint32_t d2,
	uint32_t d3, uint32_t u, uint32_t v, uint32_t *color)
{
	static const uint8_t block_width[8] = { 8, 8, 7, 4, 4, 5, 3, 4 };
	static const uint8_t block_height[8] = { 8, 4, 3, 4, 3, 2, 3, 2 };
	uint16_t texture_segment = (uint16_t)(d2 >> 16);
	uint32_t segment_index = texture_segment >> 10;
	uint32_t segment_offset = (texture_segment & 0x3ff) << 5;
	uint32_t segment_address = (uint32_t)segment_table + segment_index * 2;
	uint16_t segment;
	uint32_t source;
	uint32_t width = descriptor & 0xff;
	uint32_t height = (descriptor >> 8) & 0xff;
	uint32_t bit_code = (descriptor >> 24) & 7;
	uint32_t bpp = bit_code + 1;
	uint32_t s = u;
	uint32_t t = v;
	uint32_t bit_address;
	uint64_t packed;
	uint32_t palette_index;
	uint16_t raw_color;
	uint32_t mask_u_bits = (descriptor >> 16) & 0x0f;
	uint32_t mask_v_bits = (descriptor >> 20) & 0x0f;
	uint32_t map = (d3 >> 6) & 1;

	if (!color || segment_address + 2 > XAVIX2_LOW_RAM_SIZE)
		return 0;
	segment = load16(machine->low_ram + segment_address);
	source = ((uint32_t)segment << 14) + segment_offset;

	/* SSD's block-address equations (US20090278845) apply to both map
	 * layouts.  Divided storage is selected only for an unmasked texture
	 * taller than one hardware block; Map 0 and Map 1 fold the lower half
	 * differently.  BAD deliberately keeps the original V row.  The observed
	 * ground requests filtering; nearest-neighbour is the current baseline. */
	{
		uint32_t bw = block_width[bit_code];
		uint32_t bh = block_height[bit_code];
		uint32_t blocks_per_row = width / bw + 1;
		uint32_t word_address;
		if (!mask_u_bits && !mask_v_bits && height > bh &&
			v / bh > height / (2 * bh))
		{
			s = blocks_per_row * bw - u - 1;
			t = map ? (height / bh + 1) * bh - v - 1 : height - v;
		}
		word_address = blocks_per_row * (t / bh) + s / bw;
		bit_address = ((v % bh) * bw + s % bw) * bpp;
		packed = machine_read64(machine, source + word_address * 8);
	}
	palette_index = (((descriptor >> 27) & 0x1f) << bpp) |
		((uint32_t)(packed >> bit_address) &
		((UINT32_C(1) << bpp) - 1));
	raw_color = palette_index < 0x200 ?
		load16(machine->palette_ram + palette_index * 4) : UINT16_C(0x8000);
	if (raw_color & 0x8000)
		return 0;
	*color = rgb555(raw_color);
	return 1;
}

static uint32_t blend_triangle_gouraud(uint32_t d2, uint32_t d3,
	int64_t w0, int64_t w1, int64_t w2, int64_t area,
	uint32_t destination)
{
	uint16_t vertex[3] = {
		(uint16_t)(d2 & 0x7fff),
		(uint16_t)((d2 >> 16) & 0x7fff),
		(uint16_t)(d3 & 0x7fff)
	};
	uint32_t source_component[3];
	uint32_t destination_component[3];
	uint32_t output_component[3];
	uint32_t inverse_alpha = (d3 >> 29) & 7;
	unsigned component;

	for (component = 0; component < 3; ++component)
	{
		unsigned shift = component * 5;
		int64_t interpolated = w0 * ((vertex[0] >> shift) & 31) +
			w1 * ((vertex[1] >> shift) & 31) +
			w2 * ((vertex[2] >> shift) & 31);
		int32_t five_bit = (int32_t)(interpolated / area);
		if (five_bit < 0) five_bit = 0;
		if (five_bit > 31) five_bit = 31;
		source_component[component] =
			((uint32_t)five_bit << 3) | ((uint32_t)five_bit >> 2);
	}
	destination_component[0] = (destination >> 16) & 0xff;
	destination_component[1] = (destination >> 8) & 0xff;
	destination_component[2] = destination & 0xff;
	/* Nalpha stores 1-alpha: zero is opaque and seven retains 7/8 of the
	 * destination.  Components above are ordered R,G,B in RGB555. */
	for (component = 0; component < 3; ++component)
		output_component[component] =
			(source_component[component] * (8 - inverse_alpha) +
			destination_component[component] * inverse_alpha) >> 3;
	return UINT32_C(0xff000000) | (output_component[0] << 16) |
		(output_component[1] << 8) | output_component[2];
}

static void render_triangle_gpu(xavix2_machine_t *machine, uint16_t count,
	uint16_t address)
{
	uint16_t descsize_address = load16(machine->mmio + 0x608);
	uint16_t descdata_address = load16(machine->mmio + 0x622);
	uint32_t record_index;

	for (record_index = 0; record_index < count; ++record_index)
	{
		uint32_t record = (uint32_t)address + record_index * 16;
		uint32_t d0;
		uint32_t d1;
		uint32_t d2;
		uint32_t d3;
		uint32_t descriptor_index;
		uint32_t descsize;
		uint32_t width;
		uint32_t height;
		uint32_t weight_b;
		uint32_t weight_c;
		int32_t x[3];
		int32_t y[3];
		int32_t min_x;
		int32_t max_x;
		int32_t min_y;
		int32_t max_y;
		int64_t area;
		int32_t py;

		if (record + 16 > XAVIX2_LOW_RAM_SIZE)
			break;
		d0 = load32(machine->low_ram + record);
		d1 = load32(machine->low_ram + record + 4);
		d2 = load32(machine->low_ram + record + 8);
		d3 = load32(machine->low_ram + record + 12);
		x[0] = (int32_t)((d0 >> 11) & 0x7ff);
		y[0] = (int32_t)((d0 >> 1) & 0x3ff);
		x[1] = (int32_t)(d1 & 0x7ff);
		y[1] = (int32_t)((d0 >> 22) & 0x3ff);
		x[2] = (int32_t)((d1 >> 21) & 0x7ff);
		y[2] = (int32_t)((d1 >> 11) & 0x3ff);
		/* triangle_edge receives doubled pixel centers below; double the
		 * vertices here as well so its arithmetic stays integral. */
		area = triangle_edge(x[0] * 2, y[0] * 2, x[1] * 2, y[1] * 2,
			x[2] * 2, y[2] * 2);
		if (!area)
			continue;

		if (!(d0 & 1))
		{
			descriptor_index = d3 & 0x3f;
			if ((uint32_t)descsize_address + descriptor_index * 4 + 4 >
				XAVIX2_LOW_RAM_SIZE)
				continue;
			descsize = load32(machine->low_ram + descsize_address +
				descriptor_index * 4);
			width = descsize & 0xff;
			height = (descsize >> 8) & 0xff;
			/* Polygon record layout and perspective weights match
			 * CN101116112A, Fig. 13: A=(0,0,64), B=(W,0,Bw),
			 * C=(0,H,Cw). */
			weight_b = d2 & 0xff;
			weight_c = (d3 >> 7) & 0xff;
		}
		else
		{
			descsize = width = height = 0;
			weight_b = weight_c = 0;
		}

		min_x = x[0] < x[1] ? x[0] : x[1];
		if (x[2] < min_x) min_x = x[2];
		max_x = x[0] > x[1] ? x[0] : x[1];
		if (x[2] > max_x) max_x = x[2];
		min_y = y[0] < y[1] ? y[0] : y[1];
		if (y[2] < min_y) min_y = y[2];
		max_y = y[0] > y[1] ? y[0] : y[1];
		if (y[2] > max_y) max_y = y[2];
		if (min_x < 0) min_x = 0;
		if (min_y < 0) min_y = 0;
		if (max_x >= 0x800) max_x = 0x7ff;
		if (max_y >= 0x400) max_y = 0x3ff;

		for (py = min_y; py <= max_y; ++py)
		{
			int32_t px;
			for (px = min_x; px <= max_x; ++px)
			{
				int32_t sample_x = px * 2 + 1;
				int32_t sample_y = py * 2 + 1;
				int64_t w0 = triangle_edge(x[1] * 2, y[1] * 2,
					x[2] * 2, y[2] * 2, sample_x, sample_y);
				int64_t w1 = triangle_edge(x[2] * 2, y[2] * 2,
					x[0] * 2, y[0] * 2, sample_x, sample_y);
				int64_t w2 = triangle_edge(x[0] * 2, y[0] * 2,
					x[1] * 2, y[1] * 2, sample_x, sample_y);
				uint32_t u = 0;
				uint32_t v = 0;
				uint32_t color;
				int64_t denominator;
				int64_t numerator_u;
				int64_t numerator_v;
				if ((area > 0 && (w0 < 0 || w1 < 0 || w2 < 0)) ||
					(area < 0 && (w0 > 0 || w1 > 0 || w2 > 0)))
					continue;
				if (d0 & 1)
				{
					color = blend_triangle_gouraud(d2, d3, w0, w1, w2,
						area, machine->screen_data[py * 0x800 + px]);
					machine->screen_data[py * 0x800 + px] = color;
					machine->gpu_pixel_write_count++;
					continue;
				}
				denominator = w0 * 64 + w1 * weight_b + w2 * weight_c;
				if (!denominator)
					continue;
				numerator_u = w1 * (int64_t)width * weight_b;
				numerator_v = w2 * (int64_t)height * weight_c;
				u = (uint32_t)(numerator_u / denominator);
				v = (uint32_t)(numerator_v / denominator);
				{
					uint32_t mask_u_bits = (descsize >> 16) & 0x0f;
					uint32_t mask_v_bits = (descsize >> 20) & 0x0f;
					if (mask_u_bits >= 8)
						u = 0;
					else if (mask_u_bits)
						u &= (UINT32_C(1) << (8 - mask_u_bits)) - 1;
					if (mask_v_bits >= 8)
						v = 0;
					else if (mask_v_bits)
						v &= (UINT32_C(1) << (8 - mask_v_bits)) - 1;
				}
				if (u > width) u = width;
				if (v > height) v = height;
				if (triangle_texture_color(machine, descdata_address,
					descsize, d2, d3, u, v, &color))
				{
					machine->screen_data[py * 0x800 + px] = color;
					machine->gpu_pixel_write_count++;
				}
			}
		}
	}
}

static void render_gpu_depth_range(xavix2_machine_t *machine, uint16_t count,
	uint16_t address, uint8_t minimum_depth, uint8_t maximum_depth)
{
	uint32_t *order;
	uint16_t descsize_address = load16(machine->mmio + 0x608);
	uint16_t descdata_address = load16(machine->mmio + 0x622);
	uint32_t list_index;
	uint32_t selected_count = 0;

	if (!count)
		return;
	order = (uint32_t *)malloc((size_t)count * sizeof(*order));
	if (!order)
		return;
	for (list_index = 0; list_index < count; ++list_index)
	{
		uint64_t command = machine_read64(machine, (uint32_t)address + 8 * list_index);
		uint32_t depth = (uint32_t)((command >> 21) & 0xff);
		if (depth < minimum_depth || depth > maximum_depth)
			continue;
		order[selected_count++] =
			(uint32_t)(command & UINT64_C(0x1fe00000)) | list_index;
	}
	qsort(order, selected_count, sizeof(*order), compare_gpu_order);

	for (list_index = 0; list_index < selected_count; ++list_index)
	{
		uint32_t command_index = order[list_index] & UINT32_C(0xffff);
		uint64_t command = machine_read64(machine, (uint32_t)address + 8 * command_index);
		uint32_t depth = (uint32_t)((command >> 21) & 0xff);
		if (depth < minimum_depth || depth > maximum_depth)
			continue;
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
				color = rgb555(raw_color);
				if (!(raw_color & 0x8000))
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
	if (offset == 0x408)
	{
		uint16_t sprite_address = load16(machine->mmio + 0x40c);
		uint16_t sprite_count = load16(machine->mmio + 0x410);
		/* RPU's merger sorter combines polygon and sprite streams by depth.
		 * The Dragon Ball battle fixtures use depth FF for the distant sky,
		 * polygon depth zero, and depths 00..0A for their foreground/HUD.
		 * Reproduce that observed split while the general per-scanline merger
		 * remains future work.  The later E414 trigger draws the foreground. */
		if (!machine->gpu_sprite_background_prepared)
			render_gpu_depth_range(machine, sprite_count, sprite_address,
				0xff, 0xff);
		machine->gpu_sprite_background_prepared = 1;
		render_triangle_gpu(machine, count, address);
	}
	else
	{
		/* Some frames submit only the sprite stream.  Do not discard their
		 * depth-FF objects merely because GPU0 did not provide a point at which
		 * to prepaint the distant band. */
		if (!machine->gpu_sprite_background_prepared)
			render_gpu_depth_range(machine, count, address, 0xff, 0xff);
		render_gpu_depth_range(machine, count, address, 0, 0xfe);
		machine->gpu_sprite_background_prepared = 1;
	}
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
			if (offset == PROJECTOR_COMMAND_REGISTER)
				projector_start(machine, data);
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
	machine->next_timer_cycle = XAVIX2_TIMER_CYCLES;
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

void xavix2_machine_set_fixed_pio_input(xavix2_machine_t *machine,
	uint32_t input)
{
	if (!machine)
		return;
	machine->pio_input = (machine->pio_input & ~machine->pio_fixed_input) |
		input;
	machine->pio_fixed_input = input;
}

void xavix2_machine_reset(xavix2_machine_t *machine)
{
	const uint8_t *rom;
	size_t rom_size;
	uint16_t motion_packet_address;
	uint32_t pio_fixed_input;
	uint8_t eeprom[XAVIX_EEPROM24C08_SIZE];
	if (!machine)
		return;
	rom = machine->rom;
	rom_size = machine->rom_size;
	motion_packet_address = machine->motion_packet_address;
	pio_fixed_input = machine->pio_fixed_input;
	memcpy(eeprom, machine->eeprom.data, sizeof(eeprom));
	(void)xavix2_machine_init(machine, rom, rom_size);
	xavix2_machine_set_motion_packet_address(machine, motion_packet_address);
	xavix2_machine_set_fixed_pio_input(machine, pio_fixed_input);
	(void)xavix_eeprom24c08_load_image(&machine->eeprom, eeprom, sizeof(eeprom));
}

static uint64_t next_event_cycle(const xavix2_machine_t *machine)
{
	uint64_t next = machine->next_vblank_cycle;
	if (machine->next_timer_cycle < next)
		next = machine->next_timer_cycle;
	if (machine->dma_completion_cycle && machine->dma_completion_cycle < next)
		next = machine->dma_completion_cycle;
	return next;
}

static void process_events(xavix2_machine_t *machine)
{
	while (machine->cpu.total_cycles >= machine->next_timer_cycle)
	{
		raise_interrupt(machine, IRQ_TIMER);
		machine->next_timer_cycle += XAVIX2_TIMER_CYCLES;
	}
	while (machine->cpu.total_cycles >= machine->next_vblank_cycle)
	{
		uint16_t background = load16(machine->mmio + 0x60e);
		uint32_t color = rgb555(background);
		uint32_t pixel;
		for (pixel = 0; pixel < 0x400 * 0x800; ++pixel)
			machine->screen_data[pixel] = color;
		machine->gpu_sprite_background_prepared = 0;
		machine->frame_count++;
		if (machine->experimental_direct_pio_sample)
		{
			int changed = sample_experimental_pio(machine);
			if (machine->experimental_dispatch_input)
				machine->experimental_callback_pending |= changed;
		}
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
	machine->pio_input = machine->pio_fixed_input | pio_input;
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
	/* A save may land after the short IRQ-10 service crosses a vblank
	 * boundary. Process that already-due event before subtracting the next
	 * event cycle; otherwise the unsigned difference becomes a huge budget
	 * and the first frame after F7 fast-forwards the guest. */
	process_events(machine);

	/* Stop just before vertical blank clears the command-list framebuffer.
	 * The next call crosses that boundary and renders the following frame. */
	if (machine->cpu.total_cycles < machine->next_vblank_cycle &&
		machine->cpu.total_cycles + render_margin >=
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
	uint32_t origin_x;
	uint32_t origin_y;

	if (!machine)
		return NULL;
	if (width) *width = 320;
	if (height) *height = 240;
	if (stride) *stride = 0x800;
	/* E656/E658 are the guest-selected visible origin.  Title firmware uses
	 * 0x360/0x188, while the Dragon Ball gameplay setup deliberately shifts
	 * X left to 0x310.  Retain the observed title origin until the guest has
	 * programmed a complete, in-range pair. */
	origin_x = load16(machine->mmio + 0x656);
	origin_y = load16(machine->mmio + 0x658);
	if ((!origin_x && !origin_y) || origin_x + 320 > 0x800 ||
		origin_y + 240 > 0x400)
	{
		origin_x = 0x400 - 160;
		origin_y = 0x200 - 120;
	}
	return machine->screen_data + origin_y * 0x800 + origin_x;
}

const int16_t *xavix2_machine_frame_audio(const xavix2_machine_t *machine)
{
	return machine ? xavix2_audio_frame(&machine->audio) : NULL;
}

size_t xavix2_machine_state_size(void)
{
	return XAVIX2_STATE_HEADER_SIZE + sizeof(xavix2_machine_t);
}

int xavix2_machine_state_save(const xavix2_machine_t *machine,
	void *output, size_t output_capacity, size_t *output_size)
{
	uint8_t *bytes = (uint8_t *)output;
	const size_t required = xavix2_machine_state_size();

	if (output_size)
		*output_size = required;
	if (!machine || !bytes || output_capacity < required ||
		sizeof(xavix2_machine_t) > UINT32_MAX)
		return 0;
	memcpy(bytes, XAVIX2_STATE_MAGIC, sizeof(XAVIX2_STATE_MAGIC));
	store32(bytes + 8, XAVIX2_STATE_VERSION);
	store32(bytes + 12, (uint32_t)sizeof(xavix2_machine_t));
	memcpy(bytes + XAVIX2_STATE_HEADER_SIZE, machine, sizeof(*machine));
	return 1;
}

int xavix2_machine_state_load(xavix2_machine_t *machine,
	const void *input, size_t input_size)
{
	const uint8_t *bytes = (const uint8_t *)input;
	const size_t legacy_size = offsetof(xavix2_machine_t, next_timer_cycle);
	uint32_t version;
	uint32_t payload_size;
	int legacy;
	const uint8_t *rom;
	size_t rom_size;
	xavix2_read8_fn read8;
	xavix2_read8_fn fetch8;
	xavix2_write8_fn write8;
	void *opaque;
	xavix2_interrupt_ack_fn interrupt_ack;
	void *interrupt_ack_opaque;
	xavix2_trace_fn trace;
	void *trace_opaque;
	uint64_t audio_mute_mask;
	unsigned channel;

	if (!machine || !bytes || input_size < XAVIX2_STATE_HEADER_SIZE ||
		memcmp(bytes, XAVIX2_STATE_MAGIC, sizeof(XAVIX2_STATE_MAGIC)))
		return 0;
	version = load32(bytes + 8);
	payload_size = load32(bytes + 12);
	legacy = version == XAVIX2_STATE_LEGACY_VERSION &&
		payload_size == legacy_size;
	if ((!legacy && (version != XAVIX2_STATE_VERSION ||
		payload_size != sizeof(xavix2_machine_t))) ||
		input_size != XAVIX2_STATE_HEADER_SIZE + (size_t)payload_size)
		return 0;
	if ((uint32_t)load16(bytes + XAVIX2_STATE_HEADER_SIZE +
		offsetof(xavix2_machine_t, motion_packet_address)) +
		XAVIX2_MOTION_PACKET_SIZE > XAVIX2_LOW_RAM_SIZE)
		return 0;

	/* Files capture guest hardware only.  Keep the current ROM and host bus
	 * callbacks so a state never restores stale process addresses. */
	rom = machine->rom;
	rom_size = machine->rom_size;
	read8 = machine->cpu.read8;
	fetch8 = machine->cpu.fetch8;
	write8 = machine->cpu.write8;
	opaque = machine->cpu.opaque;
	interrupt_ack = machine->cpu.interrupt_ack;
	interrupt_ack_opaque = machine->cpu.interrupt_ack_opaque;
	trace = machine->cpu.trace;
	trace_opaque = machine->cpu.trace_opaque;
	audio_mute_mask = 0;
	for (channel = 0; channel < XAVIX2_AUDIO_VOICES; ++channel)
		if (machine->audio.voice[channel].host_muted)
			audio_mute_mask |= UINT64_C(1) << channel;
	if (legacy)
	{
		memcpy(machine, bytes + XAVIX2_STATE_HEADER_SIZE, legacy_size);
		/* Version 1 tied IRQ 7 to vblank.  Resume at the next half-frame
		 * boundary so an existing F5 file immediately acquires the independent
		 * 120 Hz timer without losing the saved video phase. */
		if (machine->next_vblank_cycle >= XAVIX2_TIMER_CYCLES &&
			machine->cpu.total_cycles <
			machine->next_vblank_cycle - XAVIX2_TIMER_CYCLES)
			machine->next_timer_cycle =
				machine->next_vblank_cycle - XAVIX2_TIMER_CYCLES;
		else
			machine->next_timer_cycle = machine->next_vblank_cycle;
	}
	else
		memcpy(machine, bytes + XAVIX2_STATE_HEADER_SIZE, sizeof(*machine));
	machine->rom = rom;
	machine->rom_size = rom_size;
	machine->audio.rom = rom;
	machine->audio.rom_size = rom_size;
	xavix2_audio_set_mute_mask(&machine->audio, audio_mute_mask);
	machine->cpu.read8 = read8;
	machine->cpu.fetch8 = fetch8;
	machine->cpu.write8 = write8;
	machine->cpu.opaque = opaque;
	machine->cpu.interrupt_ack = interrupt_ack;
	machine->cpu.interrupt_ack_opaque = interrupt_ack_opaque;
	machine->cpu.trace = trace;
	machine->cpu.trace_opaque = trace_opaque;
	return 1;
}
