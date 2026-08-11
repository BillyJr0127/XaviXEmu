// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "rom_loader.h"
#include "xavix2_machine.h"

#include <windows.h>

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	PC_SAMPLE_SLOTS = 4096,
	DEFAULT_CYCLES = 30000000,
	EXECUTION_SLICE = 4096,
	MOTION_SEQUENCE_CAPACITY = 64
};

typedef struct pc_sample
{
	uint32_t pc;
	uint32_t count;
} pc_sample;

typedef struct instruction_trace
{
	uint64_t minimum_cycle;
	uint32_t minimum_pc;
	uint32_t maximum_pc;
	uint32_t limit;
	uint32_t count;
} instruction_trace;

static void trace_instruction(void *opaque, const xavix2_cpu_t *cpu,
	uint32_t pc, uint32_t opcode, uint8_t bytes)
{
	instruction_trace *trace = (instruction_trace *)opaque;
	if (cpu->total_cycles < trace->minimum_cycle ||
		pc < trace->minimum_pc || pc > trace->maximum_pc ||
		trace->count >= trace->limit)
		return;
	printf("insn cycle=%" PRIu64 " pc=%08" PRIX32 " raw=%0*" PRIX32
		" r=%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
		",%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
		" flags=%02" PRIX32 "\n",
		cpu->total_cycles, pc, bytes * 2, opcode >> (32 - bytes * 8),
		cpu->r[0], cpu->r[1], cpu->r[2], cpu->r[3], cpu->r[4], cpu->r[5],
		cpu->r[6], cpu->r[7], cpu->hr[4] & UINT32_C(0xff));
	trace->count++;
}

static void print_capture_trace(const xavix2_machine_t *machine, int verbose)
{
	unsigned index;
	printf("capture_access");
	for (index = 0; index < XAVIX2_CAPTURE_REGISTER_COUNT; ++index)
		if (machine->capture_read_count[index] || machine->capture_write_count[index])
			printf(" e%03X=%" PRIu64 "/%" PRIu64,
				XAVIX2_CAPTURE_REGISTER_FIRST + index,
				machine->capture_read_count[index],
				machine->capture_write_count[index]);
	printf(" trace=%" PRIu32 " dropped=%" PRIu64 "\n",
		machine->capture_trace_count, machine->capture_trace_dropped);
	if (!verbose)
		return;
	for (index = 0; index < machine->capture_trace_count; ++index)
	{
		const xavix2_capture_trace_entry *entry = &machine->capture_trace[index];
		printf("capture cycle=%" PRIu64 " pc=%08" PRIX32 " %c e%03X=%02X\n",
			entry->cycle, entry->pc, entry->write ? 'W' : 'R',
			entry->offset, entry->data);
	}
}

static void print_sensor_buffer_trace(const xavix2_machine_t *machine, int verbose)
{
	unsigned index;
	printf("sensor_buffer reads=%" PRIu64 " writes=%" PRIu64
		" trace=%" PRIu32 " dropped=%" PRIu64 " data=",
		machine->sensor_buffer_read_count, machine->sensor_buffer_write_count,
		machine->sensor_buffer_trace_count, machine->sensor_buffer_trace_dropped);
	for (index = 0; index < XAVIX2_SENSOR_BUFFER_SIZE; ++index)
		printf("%02X", machine->low_ram[XAVIX2_SENSOR_BUFFER_FIRST + index]);
	printf("\n");
	if (!verbose)
		return;
	for (index = 0; index < machine->sensor_buffer_trace_count; ++index)
	{
		const xavix2_capture_trace_entry *entry = &machine->sensor_buffer_trace[index];
		printf("sensor cycle=%" PRIu64 " pc=%08" PRIX32 " %c %04X=%02X\n",
			entry->cycle, entry->pc, entry->write ? 'W' : 'R',
			entry->offset, entry->data);
	}
}

static void print_irq_context_trace(const xavix2_machine_t *machine, int verbose)
{
	unsigned index;
	printf("irq_context reads=%" PRIu64 " writes=%" PRIu64
		" trace=%" PRIu32 " dropped=%" PRIu64 " data=",
		machine->irq_context_read_count, machine->irq_context_write_count,
		machine->irq_context_trace_count, machine->irq_context_trace_dropped);
	for (index = 0; index < XAVIX2_IRQ_CONTEXT_SIZE; ++index)
		printf("%02X", machine->low_ram[XAVIX2_IRQ_CONTEXT_FIRST + index]);
	printf("\n");
	if (!verbose)
		return;
	for (index = 0; index < machine->irq_context_trace_count; ++index)
	{
		const xavix2_capture_trace_entry *entry = &machine->irq_context_trace[index];
		printf("context cycle=%" PRIu64 " pc=%08" PRIX32 " %c %04X=%02X\n",
			entry->cycle, entry->pc, entry->write ? 'W' : 'R',
			entry->offset, entry->data);
	}
}

static void print_sensor_decoded_trace(const xavix2_machine_t *machine, int verbose)
{
	unsigned index;
	printf("sensor_decoded reads=%" PRIu64 " writes=%" PRIu64
		" trace=%" PRIu32 " dropped=%" PRIu64 " data=",
		machine->sensor_decoded_read_count, machine->sensor_decoded_write_count,
		machine->sensor_decoded_trace_count, machine->sensor_decoded_trace_dropped);
	for (index = 0; index < XAVIX2_SENSOR_DECODED_SIZE; ++index)
		printf("%02X", machine->low_ram[XAVIX2_SENSOR_DECODED_FIRST + index]);
	printf("\n");
	if (!verbose)
		return;
	for (index = 0; index < machine->sensor_decoded_trace_count; ++index)
	{
		const xavix2_capture_trace_entry *entry = &machine->sensor_decoded_trace[index];
		printf("decoded cycle=%" PRIu64 " pc=%08" PRIX32 " %c %04X=%02X\n",
			entry->cycle, entry->pc, entry->write ? 'W' : 'R',
			entry->offset, entry->data);
	}
}

static void print_motion_sample_trace(const xavix2_machine_t *machine, int verbose)
{
	unsigned index;
	printf("motion_sample reads=%" PRIu64 " writes=%" PRIu64
		" trace=%" PRIu32 " dropped=%" PRIu64 " data=",
		machine->motion_sample_read_count, machine->motion_sample_write_count,
		machine->motion_sample_trace_count, machine->motion_sample_trace_dropped);
	for (index = 0; index < XAVIX2_MOTION_SAMPLE_SIZE; ++index)
		printf("%02X", machine->low_ram[XAVIX2_MOTION_SAMPLE_FIRST + index]);
	printf("\n");
	if (!verbose)
		return;
	for (index = 0; index < machine->motion_sample_trace_count; ++index)
	{
		const xavix2_capture_trace_entry *entry = &machine->motion_sample_trace[index];
		printf("sample cycle=%" PRIu64 " pc=%08" PRIX32 " %c %04X=%02X\n",
			entry->cycle, entry->pc, entry->write ? 'W' : 'R',
			entry->offset, entry->data);
	}
}

static void print_motion_source_trace(const xavix2_machine_t *machine, int verbose)
{
	unsigned index;
	printf("motion_source reads=%" PRIu64 " writes=%" PRIu64
		" trace=%" PRIu32 " dropped=%" PRIu64 "\n",
		machine->motion_source_read_count, machine->motion_source_write_count,
		machine->motion_source_trace_count, machine->motion_source_trace_dropped);
	if (!verbose)
		return;
	for (index = 0; index < machine->motion_source_trace_count; ++index)
	{
		const xavix2_capture_trace_entry *entry = &machine->motion_source_trace[index];
		printf("motion cycle=%" PRIu64 " pc=%08" PRIX32 " %c %04X=%02X\n",
			entry->cycle, entry->pc, entry->write ? 'W' : 'R',
			entry->offset, entry->data);
	}
}

static void print_action_state_trace(const xavix2_machine_t *machine, int verbose)
{
	unsigned index;
	printf("action_state reads=%" PRIu64 " writes=%" PRIu64
		" trace=%" PRIu32 " dropped=%" PRIu64 " data=",
		machine->action_state_read_count, machine->action_state_write_count,
		machine->action_state_trace_count, machine->action_state_trace_dropped);
	for (index = 0; index < XAVIX2_ACTION_STATE_SIZE; ++index)
		printf("%02X", machine->low_ram[XAVIX2_ACTION_STATE_FIRST + index]);
	printf("\n");
	if (!verbose)
		return;
	for (index = 0; index < machine->action_state_trace_count; ++index)
	{
		const xavix2_capture_trace_entry *entry = &machine->action_state_trace[index];
		printf("action cycle=%" PRIu64 " pc=%08" PRIX32 " %c %04X=%02X\n",
			entry->cycle, entry->pc, entry->write ? 'W' : 'R',
			entry->offset, entry->data);
	}
}

static void print_diagnostic_ram_trace(const xavix2_machine_t *machine, int verbose)
{
	unsigned index;
	if (machine->diagnostic_ram_first > machine->diagnostic_ram_last)
		return;
	printf("ram_trace range=%04X-%04X reads=%" PRIu64 " writes=%" PRIu64
		" trace=%" PRIu32 " dropped=%" PRIu64 "\n",
		machine->diagnostic_ram_first, machine->diagnostic_ram_last,
		machine->diagnostic_ram_read_count, machine->diagnostic_ram_write_count,
		machine->diagnostic_ram_trace_count, machine->diagnostic_ram_trace_dropped);
	if (!verbose)
		return;
	for (index = 0; index < machine->diagnostic_ram_trace_count; ++index)
	{
		const xavix2_capture_trace_entry *entry = &machine->diagnostic_ram_trace[index];
		printf("ram cycle=%" PRIu64 " pc=%08" PRIX32 " %c %04X=%02X\n",
			entry->cycle, entry->pc, entry->write ? 'W' : 'R',
			entry->offset, entry->data);
	}
}

static void put_le16(uint8_t *target, uint16_t value)
{
	target[0] = (uint8_t)value;
	target[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *target, uint32_t value)
{
	target[0] = (uint8_t)value;
	target[1] = (uint8_t)(value >> 8);
	target[2] = (uint8_t)(value >> 16);
	target[3] = (uint8_t)(value >> 24);
}

static int hex_digit(char character)
{
	if (character >= '0' && character <= '9') return character - '0';
	if (character >= 'a' && character <= 'f') return character - 'a' + 10;
	if (character >= 'A' && character <= 'F') return character - 'A' + 10;
	return -1;
}

static int parse_sensor_packet(const char *text,
	uint8_t packet[XAVIX2_SENSOR_BUFFER_SIZE])
{
	unsigned index;
	if (!text || strlen(text) != XAVIX2_SENSOR_BUFFER_SIZE * 2)
		return 0;
	for (index = 0; index < XAVIX2_SENSOR_BUFFER_SIZE; ++index)
	{
		int high = hex_digit(text[index * 2]);
		int low = hex_digit(text[index * 2 + 1]);
		if (high < 0 || low < 0)
			return 0;
		packet[index] = (uint8_t)((high << 4) | low);
	}
	return 1;
}

static int parse_motion_packet(const char *text,
	uint8_t packet[XAVIX2_MOTION_PACKET_SIZE])
{
	unsigned index;
	if (!text || strlen(text) != XAVIX2_MOTION_PACKET_SIZE * 2)
		return 0;
	for (index = 0; index < XAVIX2_MOTION_PACKET_SIZE; ++index)
	{
		int high = hex_digit(text[index * 2]);
		int low = hex_digit(text[index * 2 + 1]);
		if (high < 0 || low < 0)
			return 0;
		packet[index] = (uint8_t)((high << 4) | low);
	}
	return 1;
}

static int parse_motion_sequence(const char *text,
	uint8_t packets[MOTION_SEQUENCE_CAPACITY][XAVIX2_MOTION_PACKET_SIZE],
	unsigned *count)
{
	char token[XAVIX2_MOTION_PACKET_SIZE * 2 + 1];
	*count = 0;
	while (text && *text)
	{
		const char *separator = strchr(text, ',');
		size_t length = separator ? (size_t)(separator - text) : strlen(text);
		if (*count >= MOTION_SEQUENCE_CAPACITY ||
			length != XAVIX2_MOTION_PACKET_SIZE * 2)
			return 0;
		memcpy(token, text, length);
		token[length] = '\0';
		if (!parse_motion_packet(token, packets[*count]))
			return 0;
		(*count)++;
		if (!separator)
			break;
		text = separator + 1;
	}
	return *count != 0;
}

static uint32_t get_le32(const uint8_t *source)
{
	return (uint32_t)source[0] | ((uint32_t)source[1] << 8) |
		((uint32_t)source[2] << 16) | ((uint32_t)source[3] << 24);
}

static void print_hex_bytes(const char *label, uint32_t address,
	const uint8_t *data, size_t size)
{
	size_t index;
	printf("%s[%04" PRIX32 "]=" , label, address);
	for (index = 0; index < size; ++index)
		printf("%02X", data[index]);
	printf("\n");
}

static int save_bmp(const char *path, const xavix2_machine_t *machine)
{
	uint8_t header[54] = { 0 };
	const uint32_t *pixels;
	unsigned width;
	unsigned height;
	unsigned stride;
	FILE *file;
	unsigned y;
	pixels = xavix2_machine_visible_frame(machine, &width, &height, &stride);
	file = fopen(path, "wb");
	if (!file || !pixels)
	{
		if (file) fclose(file);
		return 0;
	}
	header[0] = 'B';
	header[1] = 'M';
	put_le32(header + 2, 54 + width * height * 4);
	put_le32(header + 10, 54);
	put_le32(header + 14, 40);
	put_le32(header + 18, width);
	put_le32(header + 22, height);
	put_le16(header + 26, 1);
	put_le16(header + 28, 32);
	put_le32(header + 34, width * height * 4);
	if (fwrite(header, 1, sizeof(header), file) != sizeof(header))
	{
		fclose(file);
		return 0;
	}
	for (y = height; y-- > 0; )
		if (fwrite(pixels + y * stride, 4, width, file) != width)
		{
			fclose(file);
			return 0;
		}
	return fclose(file) == 0;
}

static uint64_t frame_hash(const xavix2_machine_t *machine)
{
	const uint32_t *pixels;
	unsigned width;
	unsigned height;
	unsigned stride;
	unsigned x;
	unsigned y;
	uint64_t hash = UINT64_C(1469598103934665603);
	pixels = xavix2_machine_visible_frame(machine, &width, &height, &stride);
	for (y = 0; y < height; ++y)
		for (x = 0; x < width; ++x)
		{
			uint32_t value = pixels[y * stride + x];
			hash ^= value;
			hash *= UINT64_C(1099511628211);
		}
	return hash;
}

static uint64_t byte_hash(const uint8_t *data, size_t size)
{
	uint64_t hash = UINT64_C(1469598103934665603);
	size_t index;
	for (index = 0; index < size; ++index)
	{
		hash ^= data[index];
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

static int save_bytes(const char *path, const void *data, size_t size)
{
	FILE *file = fopen(path, "wb");
	if (!file)
		return 0;
	if (fwrite(data, 1, size, file) != size)
	{
		fclose(file);
		return 0;
	}
	return fclose(file) == 0;
}

static int save_wav(const char *path, const int16_t *samples, size_t frames)
{
	FILE *file;
	uint8_t header[44] = { 0 };
	size_t data_size;
	uint32_t value;

	if (!path || !samples || frames > (UINT32_MAX - 44U) / 4U)
		return 0;
	data_size = frames * 4U;
	memcpy(header, "RIFF", 4);
	value = (uint32_t)(36U + data_size);
	header[4] = (uint8_t)value;
	header[5] = (uint8_t)(value >> 8);
	header[6] = (uint8_t)(value >> 16);
	header[7] = (uint8_t)(value >> 24);
	memcpy(header + 8, "WAVEfmt ", 8);
	header[16] = 16;
	header[20] = 1;
	header[22] = XAVIX2_AUDIO_OUTPUT_CHANNELS;
	value = XAVIX2_AUDIO_OUTPUT_RATE;
	header[24] = (uint8_t)value;
	header[25] = (uint8_t)(value >> 8);
	header[26] = (uint8_t)(value >> 16);
	header[27] = (uint8_t)(value >> 24);
	value = XAVIX2_AUDIO_OUTPUT_RATE * XAVIX2_AUDIO_OUTPUT_CHANNELS * 2U;
	header[28] = (uint8_t)value;
	header[29] = (uint8_t)(value >> 8);
	header[30] = (uint8_t)(value >> 16);
	header[31] = (uint8_t)(value >> 24);
	header[32] = XAVIX2_AUDIO_OUTPUT_CHANNELS * 2U;
	header[34] = 16;
	memcpy(header + 36, "data", 4);
	value = (uint32_t)data_size;
	header[40] = (uint8_t)value;
	header[41] = (uint8_t)(value >> 8);
	header[42] = (uint8_t)(value >> 16);
	header[43] = (uint8_t)(value >> 24);

	file = fopen(path, "wb");
	if (!file)
		return 0;
	if (fwrite(header, 1, sizeof(header), file) != sizeof(header) ||
		fwrite(samples, 1, data_size, file) != data_size)
	{
		fclose(file);
		return 0;
	}
	return fclose(file) == 0;
}

static void add_pc_sample(pc_sample samples[PC_SAMPLE_SLOTS], uint32_t pc)
{
	unsigned slot = (unsigned)((pc ^ (pc >> 12) ^ (pc >> 24)) & (PC_SAMPLE_SLOTS - 1));
	unsigned attempt;
	for (attempt = 0; attempt < PC_SAMPLE_SLOTS; ++attempt)
	{
		pc_sample *sample = &samples[(slot + attempt) & (PC_SAMPLE_SLOTS - 1)];
		if (!sample->count || sample->pc == pc)
		{
			sample->pc = pc;
			sample->count++;
			return;
		}
	}
}

static void print_hot_pcs(const pc_sample samples[PC_SAMPLE_SLOTS])
{
	pc_sample copy[PC_SAMPLE_SLOTS];
	unsigned printed;
	unsigned index;
	memcpy(copy, samples, sizeof(copy));
	printf("sampled PC hotspots:\n");
	for (printed = 0; printed < 10; ++printed)
	{
		unsigned best = PC_SAMPLE_SLOTS;
		for (index = 0; index < PC_SAMPLE_SLOTS; ++index)
			if (copy[index].count &&
				(best == PC_SAMPLE_SLOTS || copy[index].count > copy[best].count))
				best = index;
		if (best == PC_SAMPLE_SLOTS)
			break;
		printf("  %08" PRIX32 "  %" PRIu32 " samples\n", copy[best].pc, copy[best].count);
		copy[best].count = 0;
	}
}

static uint32_t pc_sample_count(const pc_sample samples[PC_SAMPLE_SLOTS], uint32_t pc)
{
	unsigned slot = (unsigned)((pc ^ (pc >> 12) ^ (pc >> 24)) & (PC_SAMPLE_SLOTS - 1));
	unsigned attempt;
	for (attempt = 0; attempt < PC_SAMPLE_SLOTS; ++attempt)
	{
		const pc_sample *sample = &samples[(slot + attempt) & (PC_SAMPLE_SLOTS - 1)];
		if (!sample->count)
			return 0;
		if (sample->pc == pc)
			return sample->count;
	}
	return 0;
}

static void run_input_event(xavix2_machine_t *machine, uint32_t input)
{
	machine->experimental_direct_pio_sample = 1;
	machine->experimental_dispatch_input = 1;
	machine->pio_input = input;
	(void)xavix2_machine_execute(machine, 2 * XAVIX2_CYCLES_PER_FRAME);
	xavix2_machine_raise_irq(machine, 2);
	(void)xavix2_machine_execute(machine, 4096);
	xavix2_machine_clear_irq(machine, 2);
	machine->experimental_callback_pending = 1;
	(void)xavix2_machine_execute(machine, 8 * XAVIX2_CYCLES_PER_FRAME);

}

static void run_sweep_step(xavix2_machine_t *machine, uint32_t input)
{
	run_input_event(machine, input);
	machine->pio_input = 0;
	(void)xavix2_machine_execute(machine, 2 * XAVIX2_CYCLES_PER_FRAME);
	xavix2_machine_raise_irq(machine, 2);
	(void)xavix2_machine_execute(machine, 4096);
	xavix2_machine_clear_irq(machine, 2);
	(void)xavix2_machine_execute(machine, 8 * XAVIX2_CYCLES_PER_FRAME);
}

static void run_sweep_sequence(xavix2_machine_t *machine,
	uint32_t first, uint32_t second)
{
	run_input_event(machine, first);
	run_input_event(machine, second);
	run_input_event(machine, 0);
}

static void run_capture_sample(xavix2_machine_t *machine, uint32_t input)
{
	machine->pio_input = input;
	xavix2_machine_raise_irq(machine, 8);
	(void)xavix2_machine_execute(machine, 200000);
	xavix2_machine_clear_irq(machine, 8);
}

static void run_capture_sequence(xavix2_machine_t *machine,
	uint32_t first, uint32_t second)
{
	run_capture_sample(machine, first);
	run_capture_sample(machine, second);
	run_capture_sample(machine, 0);
}

static unsigned meaningful_ram_differences(const uint8_t *left,
	const uint8_t *right);
static void print_first_ram_differences(const uint8_t *reference,
	const uint8_t *trial, unsigned limit);

static void run_title_pio_event(xavix2_machine_t *machine, uint32_t input)
{
	machine->pio_input = input;
	xavix2_machine_raise_irq(machine, 8);
	(void)xavix2_machine_execute(machine, 200000);
	xavix2_machine_clear_irq(machine, 8);
	machine->pio_input = 0;
	xavix2_machine_raise_irq(machine, 8);
	(void)xavix2_machine_execute(machine, 200000);
	xavix2_machine_clear_irq(machine, 8);
	(void)xavix2_machine_execute(machine, 8 * XAVIX2_CYCLES_PER_FRAME);
}

static void sweep_title_pio_bits(const xavix2_machine_t *baseline)
{
	xavix2_machine_t *reference = (xavix2_machine_t *)malloc(sizeof(*reference));
	xavix2_machine_t *trial = (xavix2_machine_t *)malloc(sizeof(*trial));
	uint64_t reference_frame;
	unsigned bit;
	unsigned candidates = 0;
	if (!reference || !trial)
	{
		fprintf(stderr, "title PIO sweep allocation failed\n");
		free(reference);
		free(trial);
		return;
	}
	memcpy(reference, baseline, sizeof(*reference));
	reference->cpu.opaque = reference;
	run_title_pio_event(reference, 0);
	reference_frame = frame_hash(reference);
	printf("title_pio_sweep reference_frame=%016" PRIX64 "\n", reference_frame);
	for (bit = 0; bit < 32; ++bit)
	{
		uint64_t hash;
		unsigned ram_differences;
		memcpy(trial, baseline, sizeof(*trial));
		trial->cpu.opaque = trial;
		run_title_pio_event(trial, UINT32_C(1) << bit);
		hash = frame_hash(trial);
		ram_differences = meaningful_ram_differences(reference->low_ram,
			trial->low_ram);
		if (hash != reference_frame || ram_differences)
		{
			printf("title_pio_bit=%u frame=%016" PRIX64 " ram_diff=%u first=",
				bit, hash, ram_differences);
			print_first_ram_differences(reference->low_ram, trial->low_ram, 12);
			printf(" pc=%08" PRIX32 "\n", trial->cpu.pc);
			candidates++;
		}
	}
	printf("title_pio_sweep candidates=%u tested=32\n", candidates);
	free(trial);
	free(reference);
}

static void run_sensor_packet(xavix2_machine_t *machine,
	const uint8_t packet[XAVIX2_SENSOR_BUFFER_SIZE])
{
	memcpy(machine->low_ram + XAVIX2_SENSOR_BUFFER_FIRST,
		packet, XAVIX2_SENSOR_BUFFER_SIZE);
	xavix2_machine_raise_irq(machine, 8);
	(void)xavix2_machine_execute(machine, 200000);
	xavix2_machine_clear_irq(machine, 8);
}

static unsigned range_differences(const uint8_t *left, const uint8_t *right,
	unsigned first, unsigned last)
{
	unsigned count = 0;
	unsigned address;
	for (address = first; address < last; ++address)
		if (left[address] != right[address])
			count++;
	return count;
}

static void print_first_ram_differences(const uint8_t *reference,
	const uint8_t *trial, unsigned limit)
{
	unsigned address;
	unsigned printed = 0;
	for (address = 0x400; address < XAVIX2_LOW_RAM_SIZE && printed < limit; ++address)
	{
		if ((address >= 0x0da4 && address <= 0x0dbf) ||
			(address >= XAVIX2_SENSOR_BUFFER_FIRST &&
			address < XAVIX2_SENSOR_BUFFER_FIRST + XAVIX2_SENSOR_BUFFER_SIZE) ||
			reference[address] == trial[address])
			continue;
		printf(" %04X:%02X>%02X", address, reference[address], trial[address]);
		printed++;
	}
}

static void sweep_sensor_packet_bits(const xavix2_machine_t *baseline)
{
	xavix2_machine_t *reference = (xavix2_machine_t *)malloc(sizeof(*reference));
	xavix2_machine_t *trial = (xavix2_machine_t *)malloc(sizeof(*trial));
	uint8_t packet[XAVIX2_SENSOR_BUFFER_SIZE] = { 0 };
	unsigned bit;
	unsigned candidates = 0;
	if (!reference || !trial)
	{
		fprintf(stderr, "sensor packet sweep allocation failed\n");
		free(reference);
		free(trial);
		return;
	}
	memcpy(reference, baseline, sizeof(*reference));
	reference->cpu.opaque = reference;
	run_sensor_packet(reference, packet);
	printf("sensor_packet_sweep reference_motion=");
	for (bit = 0; bit < 32; ++bit)
		printf("%02X", reference->low_ram[0x13a0 + bit]);
	printf("\n");
	for (bit = 0; bit < XAVIX2_SENSOR_BUFFER_SIZE * 8; ++bit)
	{
		unsigned motion_differences;
		unsigned ram_differences;
		memset(packet, 0, sizeof(packet));
		packet[bit / 8] = (uint8_t)(UINT32_C(1) << (bit & 7));
		memcpy(trial, baseline, sizeof(*trial));
		trial->cpu.opaque = trial;
		run_sensor_packet(trial, packet);
		motion_differences = range_differences(reference->low_ram,
			trial->low_ram, 0x1200, 0x1400);
		ram_differences = meaningful_ram_differences(reference->low_ram,
			trial->low_ram);
		if (motion_differences || ram_differences)
		{
			printf("sensor_bit=%u byte=%u mask=%02X motion_diff=%u ram_diff=%u first=",
				bit, bit / 8, packet[bit / 8], motion_differences, ram_differences);
			print_first_ram_differences(reference->low_ram, trial->low_ram, 12);
			printf(" pc=%08" PRIX32 "\n", trial->cpu.pc);
			candidates++;
		}
	}
	printf("sensor_packet_sweep candidates=%u tested=%u\n", candidates,
		XAVIX2_SENSOR_BUFFER_SIZE * 8);
	free(trial);
	free(reference);
}

static void sweep_capture_sequences(const xavix2_machine_t *baseline)
{
	static const unsigned INPUT_BITS[] =
		{ 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 22 };
	xavix2_machine_t *trial = (xavix2_machine_t *)malloc(sizeof(*trial));
	unsigned first;
	unsigned second;
	unsigned candidates = 0;
	if (!trial)
	{
		fprintf(stderr, "capture sequence sweep allocation failed\n");
		return;
	}
	printf("capture_sweep baseline_motion=");
	for (first = 0; first < 32; ++first)
		printf("%02X", baseline->low_ram[0x13a0 + first]);
	printf("\n");
	for (first = 0; first < sizeof(INPUT_BITS) / sizeof(INPUT_BITS[0]); ++first)
		for (second = 0; second < sizeof(INPUT_BITS) / sizeof(INPUT_BITS[0]); ++second)
		{
			if (first == second)
				continue;
			memcpy(trial, baseline, sizeof(*trial));
			trial->cpu.opaque = trial;
			run_capture_sequence(trial, UINT32_C(1) << INPUT_BITS[first],
				UINT32_C(1) << INPUT_BITS[second]);
			if (memcmp(trial->low_ram + 0x13a0,
				baseline->low_ram + 0x13a0, 32) != 0)
			{
				printf("capture_candidate=%u->%u motion=",
					INPUT_BITS[first], INPUT_BITS[second]);
				for (unsigned index = 0; index < 32; ++index)
					printf("%02X", trial->low_ram[0x13a0 + index]);
				printf(" frame=%016" PRIX64 " pc=%08" PRIX32 "\n",
					frame_hash(trial), trial->cpu.pc);
				candidates++;
			}
		}
	printf("capture_sweep candidates=%u tested=%u\n", candidates,
		(unsigned)((sizeof(INPUT_BITS) / sizeof(INPUT_BITS[0])) *
		((sizeof(INPUT_BITS) / sizeof(INPUT_BITS[0])) - 1)));
	free(trial);
}

static unsigned meaningful_ram_differences(const uint8_t *left, const uint8_t *right)
{
	unsigned count = 0;
	unsigned address;
	for (address = 0x400; address < XAVIX2_LOW_RAM_SIZE; ++address)
		if ((address < 0x0da4 || address > 0x0dbf) && left[address] != right[address])
			count++;
	return count;
}

static void sweep_input_pairs(const xavix2_machine_t *baseline)
{
	static const unsigned INPUT_BITS[] =
		{ 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 22 };
	xavix2_machine_t *reference = (xavix2_machine_t *)malloc(sizeof(*reference));
	xavix2_machine_t *trial = (xavix2_machine_t *)malloc(sizeof(*trial));
	uint64_t reference_frame;
	unsigned first;
	unsigned second;
	unsigned candidates = 0;
	if (!reference || !trial)
	{
		fprintf(stderr, "input sweep allocation failed\n");
		free(reference);
		free(trial);
		return;
	}
	memcpy(reference, baseline, sizeof(*reference));
	reference->cpu.opaque = reference;
	run_sweep_step(reference, UINT32_C(1) << INPUT_BITS[0]);
	reference_frame = frame_hash(reference);
	printf("input_sweep reference_frame=%016" PRIX64 " callback=%08" PRIX32 "\n",
		reference_frame, get_le32(reference->low_ram + 0x0dd8));
	for (first = 0; first < sizeof(INPUT_BITS) / sizeof(INPUT_BITS[0]); ++first)
		for (second = first; second < sizeof(INPUT_BITS) / sizeof(INPUT_BITS[0]); ++second)
		{
			uint32_t input = (UINT32_C(1) << INPUT_BITS[first]) |
				(UINT32_C(1) << INPUT_BITS[second]);
			uint64_t hash;
			unsigned ram_differences;
			memcpy(trial, baseline, sizeof(*trial));
			trial->cpu.opaque = trial;
			run_sweep_step(trial, input);
			hash = frame_hash(trial);
			ram_differences = meaningful_ram_differences(
				reference->low_ram, trial->low_ram);
			if (hash != reference_frame || ram_differences ||
				get_le32(trial->low_ram + 0x0dd8) != get_le32(reference->low_ram + 0x0dd8))
			{
				printf("input_candidate=%08" PRIX32 " bits=%u,%u frame=%016" PRIX64
					" ram_diff=%u callback=%08" PRIX32 " pc=%08" PRIX32 "\n",
					input, INPUT_BITS[first], INPUT_BITS[second], hash,
					ram_differences, get_le32(trial->low_ram + 0x0dd8), trial->cpu.pc);
				candidates++;
			}
		}
	printf("input_sweep candidates=%u tested=%u\n", candidates,
		(unsigned)((sizeof(INPUT_BITS) / sizeof(INPUT_BITS[0])) *
		((sizeof(INPUT_BITS) / sizeof(INPUT_BITS[0])) + 1) / 2));
	free(trial);
	free(reference);
}

static void sweep_input_sequences(const xavix2_machine_t *baseline)
{
	static const unsigned INPUT_BITS[] =
		{ 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 22 };
	xavix2_machine_t *reference = (xavix2_machine_t *)malloc(sizeof(*reference));
	xavix2_machine_t *trial = (xavix2_machine_t *)malloc(sizeof(*trial));
	uint64_t reference_frame;
	unsigned first;
	unsigned second;
	unsigned candidates = 0;
	if (!reference || !trial)
	{
		fprintf(stderr, "input sequence sweep allocation failed\n");
		free(reference);
		free(trial);
		return;
	}
	memcpy(reference, baseline, sizeof(*reference));
	reference->cpu.opaque = reference;
	run_sweep_sequence(reference, UINT32_C(1) << INPUT_BITS[0],
		UINT32_C(1) << INPUT_BITS[1]);
	reference_frame = frame_hash(reference);
	printf("sequence_sweep reference_frame=%016" PRIX64 " callback=%08" PRIX32 "\n",
		reference_frame, get_le32(reference->low_ram + 0x0dd8));
	for (first = 0; first < sizeof(INPUT_BITS) / sizeof(INPUT_BITS[0]); ++first)
		for (second = 0; second < sizeof(INPUT_BITS) / sizeof(INPUT_BITS[0]); ++second)
		{
			uint64_t hash;
			unsigned ram_differences;
			if (first == second)
				continue;
			memcpy(trial, baseline, sizeof(*trial));
			trial->cpu.opaque = trial;
			run_sweep_sequence(trial, UINT32_C(1) << INPUT_BITS[first],
				UINT32_C(1) << INPUT_BITS[second]);
			hash = frame_hash(trial);
			ram_differences = meaningful_ram_differences(
				reference->low_ram, trial->low_ram);
			if (hash != reference_frame || ram_differences ||
				get_le32(trial->low_ram + 0x0dd8) != get_le32(reference->low_ram + 0x0dd8))
			{
				printf("sequence_candidate=%u->%u frame=%016" PRIX64
					" ram_diff=%u callback=%08" PRIX32 " pc=%08" PRIX32 "\n",
					INPUT_BITS[first], INPUT_BITS[second], hash,
					ram_differences, get_le32(trial->low_ram + 0x0dd8), trial->cpu.pc);
				candidates++;
			}
		}
	printf("sequence_sweep candidates=%u tested=%u\n", candidates,
		(unsigned)((sizeof(INPUT_BITS) / sizeof(INPUT_BITS[0])) *
		((sizeof(INPUT_BITS) / sizeof(INPUT_BITS[0])) - 1)));
	free(trial);
	free(reference);
}

static void sweep_motion_targets(const xavix2_machine_t *baseline)
{
	xavix2_machine_t *trial = (xavix2_machine_t *)malloc(sizeof(*trial));
	unsigned sample_y;
	unsigned candidates = 0;
	if (!trial)
	{
		fprintf(stderr, "motion target sweep allocation failed\n");
		return;
	}
	for (sample_y = 4; sample_y <= 55; sample_y += 6)
	{
		unsigned sample_x;
		for (sample_x = 4; sample_x <= 55; sample_x += 6)
		{
			uint8_t packet[XAVIX2_MOTION_PACKET_SIZE] =
				{ (uint8_t)sample_x, (uint8_t)sample_y, 0x20,
					0, 0, 0, 0 };
			unsigned frame;
			uint32_t hit_mask;
			memcpy(trial, baseline, sizeof(*trial));
			trial->cpu.opaque = trial;
			for (frame = 0; frame < 10; ++frame)
				(void)xavix2_machine_run_video_frame(trial, packet, 0);
			hit_mask = get_le32(trial->low_ram + 0x6468);
			if (hit_mask)
			{
				printf("motion_target x=%u y=%u mask=%08" PRIX32 "\n",
					sample_x, sample_y, hit_mask);
				candidates++;
			}
		}
	}
	printf("motion_target_sweep candidates=%u tested=81\n", candidates);
	free(trial);
}

static void print_hot_mmio(const xavix2_machine_t *machine)
{
	uint8_t selected[XAVIX2_MMIO_SIZE] = { 0 };
	unsigned printed;
	printf("hot MMIO bytes:\n");
	for (printed = 0; printed < 48; ++printed)
	{
		uint64_t best_count = 0;
		unsigned best = XAVIX2_MMIO_SIZE;
		unsigned offset;
		for (offset = 0; offset < XAVIX2_MMIO_SIZE; ++offset)
		{
			uint64_t count;
			if (selected[offset])
				continue;
			count = machine->mmio_read_counts[offset] +
				machine->mmio_write_counts[offset];
			if (count > best_count)
			{
				best_count = count;
				best = offset;
			}
		}
		if (best == XAVIX2_MMIO_SIZE || !best_count)
			break;
		selected[best] = 1;
		printf("  FFFFE%03X value=%02X reads=%" PRIu64
			"@%08" PRIX32 " writes=%" PRIu64 "@%08" PRIX32 "\n",
			best, machine->mmio[best], machine->mmio_read_counts[best],
			machine->mmio_last_read_pc[best],
			machine->mmio_write_counts[best],
			machine->mmio_last_write_pc[best]);
	}
}

static void print_all_mmio(const xavix2_machine_t *machine)
{
	unsigned offset;
	printf("active MMIO bytes:\n");
	for (offset = 0; offset < XAVIX2_MMIO_SIZE; ++offset)
	{
		uint64_t count = machine->mmio_read_counts[offset] +
			machine->mmio_write_counts[offset];
		if (!count)
			continue;
		printf("  FFFFE%03X value=%02X reads=%" PRIu64
			"@%08" PRIX32 " writes=%" PRIu64 "@%08" PRIX32 "\n",
			offset, machine->mmio[offset], machine->mmio_read_counts[offset],
			machine->mmio_last_read_pc[offset],
			machine->mmio_write_counts[offset],
			machine->mmio_last_write_pc[offset]);
	}
}

static void print_audio_mmio_trace(const xavix2_machine_t *machine)
{
	uint64_t available = machine->audio_mmio_trace_total <
		XAVIX2_AUDIO_TRACE_CAPACITY ? machine->audio_mmio_trace_total :
		XAVIX2_AUDIO_TRACE_CAPACITY;
	uint64_t show = available < 256 ? available : 256;
	uint32_t start = (machine->audio_mmio_trace_next +
		XAVIX2_AUDIO_TRACE_CAPACITY - (uint32_t)show) %
		XAVIX2_AUDIO_TRACE_CAPACITY;
	uint64_t index;
	printf("audio_mmio_trace total=%" PRIu64 " showing_last=%" PRIu64 "\n",
		machine->audio_mmio_trace_total, show);
	for (index = 0; index < show; ++index)
	{
		const xavix2_capture_trace_entry *entry =
			&machine->audio_mmio_trace[(start + (uint32_t)index) %
				XAVIX2_AUDIO_TRACE_CAPACITY];
		printf("  cycle=%" PRIu64 " pc=%08" PRIX32 " %c E%03X=%02X\n",
			entry->cycle, entry->pc, entry->write ? 'W' : 'R',
			entry->offset, entry->data);
	}
}

static void print_audio_summary(const xavix2_machine_t *machine)
{
	const int16_t *samples = xavix2_machine_frame_audio(machine);
	uint64_t hash = UINT64_C(1469598103934665603);
	unsigned nonzero = 0;
	unsigned peak = 0;
	unsigned index;
	printf("audio_status=");
	for (index = 0; index < XAVIX2_AUDIO_VOICES / 8; ++index)
		printf("%02X", xavix2_audio_status(&machine->audio, index));
	for (index = 0; index < XAVIX2_AUDIO_FRAMES_PER_VIDEO_FRAME * 2; ++index)
	{
		unsigned magnitude = samples[index] < 0 ?
			(unsigned)(-(int32_t)samples[index]) : (unsigned)samples[index];
		if (samples[index])
			nonzero++;
		if (magnitude > peak)
			peak = magnitude;
		hash ^= (uint16_t)samples[index];
		hash *= UINT64_C(1099511628211);
	}
	printf(" nonzero=%u peak=%u hash=%016" PRIX64 "\n",
		nonzero, peak, hash);
}

static void print_audio_voices(const xavix2_machine_t *machine)
{
	const uint8_t *descriptors = machine->video_ram + 0xf800;
	uint32_t engine_rate = get_le32(machine->low_ram + 0x150);
	unsigned channel;

	printf("audio_voices engine_rate=%" PRIu32 "\n", engine_rate);
	for (channel = 0; channel < XAVIX2_AUDIO_VOICES; ++channel)
	{
		const xavix2_audio_voice *voice = &machine->audio.voice[channel];
		const uint8_t *descriptor;
		uint32_t start;
		uint32_t loop;
		uint16_t pitch;
		uint32_t source_rate;
		if (!voice->active)
			continue;
		descriptor = descriptors + channel * XAVIX2_AUDIO_DESCRIPTOR_SIZE;
		start = ((uint32_t)(descriptor[2] | ((uint16_t)descriptor[3] << 8)) << 16) |
			(descriptor[6] | ((uint32_t)descriptor[7] << 8));
		loop = ((uint32_t)(descriptor[0x0e] |
			((uint16_t)descriptor[0x0f] << 8)) << 16) |
			(descriptor[0x12] | ((uint32_t)descriptor[0x13] << 8));
		pitch = voice->pitch;
		source_rate = (uint32_t)(((uint64_t)pitch * engine_rate + 16384U) >> 15);
		printf("  ch=%u looped=%u start=%08" PRIX32 " end=%08" PRIX32
			" pos=%08" PRIX32 "+%08" PRIX32 " pitch=%u rate=%" PRIu32
			" volume=%u,%u\n",
			channel, voice->loop, start, loop,
			(uint32_t)(voice->position >> 32), (uint32_t)voice->position,
			pitch, source_rate, voice->volume_left, voice->volume_right);
	}
}

static void print_hex_range(const char *label, const uint8_t *data,
	size_t data_size, uint32_t first, uint32_t length)
{
	uint32_t offset;
	uint64_t end = (uint64_t)first + length;

	if (first >= data_size)
		return;
	if (end > data_size)
		end = data_size;
	for (offset = first; offset < end; offset += 16)
	{
		uint32_t index;
		printf("%s %08" PRIX32 ":", label, offset);
		for (index = 0; index < 16 && (uint64_t)offset + index < end; ++index)
			printf(" %02X", data[offset + index]);
		printf("\n");
	}
}

static void find_ascii_text(const uint8_t *data, size_t data_size,
	const char *needle)
{
	size_t needle_length = strlen(needle);
	size_t offset;

	if (!needle_length || needle_length > data_size)
		return;
	for (offset = 0; offset + needle_length <= data_size; ++offset)
	{
		size_t index;
		for (index = 0; index < needle_length; ++index)
			if (tolower((unsigned char)data[offset + index]) !=
				tolower((unsigned char)needle[index]))
				break;
		if (index == needle_length)
		{
			size_t first = offset > 24 ? offset - 24 : 0;
			size_t last = offset + needle_length + 48;
			if (last > data_size)
				last = data_size;
			printf("ascii %08" PRIX32 ": ", (uint32_t)offset);
			for (index = first; index < last; ++index)
				putchar(isprint(data[index]) ? data[index] : '.');
			putchar('\n');
		}
	}
}

static void find_u32_value(const uint8_t *data, size_t data_size, uint32_t value)
{
	size_t offset;

	for (offset = 0; offset + 4 <= data_size; ++offset)
		if (((uint32_t)data[offset] |
			((uint32_t)data[offset + 1] << 8) |
			((uint32_t)data[offset + 2] << 16) |
			((uint32_t)data[offset + 3] << 24)) == value)
			printf("u32 %08" PRIX32 " = %08" PRIX32 "\n",
				(uint32_t)offset, value);
}

int main(int argc, char **argv)
{
	wchar_t path[32768];
	wchar_t error[384];
	drgqst_rom_image image = { 0 };
	xavix2_machine_t *machine;
	pc_sample samples[PC_SAMPLE_SLOTS] = { { 0, 0 } };
	uint64_t requested = DEFAULT_CYCLES;
	uint64_t completed = 0;
	uint64_t input_at = 0;
	uint64_t input_release_at = UINT64_MAX;
	uint32_t pending_input = 0;
	uint64_t input2_at = UINT64_MAX;
	uint64_t input2_release_at = UINT64_MAX;
	uint32_t pending_input2 = 0;
	uint64_t irq_at = UINT64_MAX;
	uint64_t irq_period = 0;
	uint64_t irq_width = EXECUTION_SLICE;
	uint64_t fine_trace_at = UINT64_MAX;
	uint64_t fine_trace_end = 0;
	unsigned injected_irq = 32;
	unsigned clear_irq = 32;
	uint64_t irq_clear_at = UINT64_MAX;
	unsigned injected_irq2 = 32;
	uint64_t irq2_at = UINT64_MAX;
	uint64_t irq2_clear_at = UINT64_MAX;
	uint8_t sensor_packet[XAVIX2_SENSOR_BUFFER_SIZE] = { 0 };
	uint64_t sensor_packet_at = UINT64_MAX;
	int sensor_packet_pending = 0;
	uint8_t motion_packet[XAVIX2_MOTION_PACKET_SIZE] = { 0 };
	uint8_t video_motion_packet2[XAVIX2_MOTION_PACKET_SIZE] = { 0 };
	uint8_t video_motion_packet3[XAVIX2_MOTION_PACKET_SIZE] = { 0 };
	uint8_t video_motion_packet4[XAVIX2_MOTION_PACKET_SIZE] = { 0 };
	uint8_t video_motion_packet5[XAVIX2_MOTION_PACKET_SIZE] = { 0 };
	uint8_t video_motion_packet6[XAVIX2_MOTION_PACKET_SIZE] = { 0 };
	int video_motion_packet2_valid = 0;
	int video_motion_packet3_valid = 0;
	int video_motion_packet4_valid = 0;
	int video_motion_packet5_valid = 0;
	int video_motion_packet6_valid = 0;
	unsigned video_motion_packet2_at = UINT_MAX;
	unsigned video_motion_packet3_at = UINT_MAX;
	unsigned video_motion_packet4_at = UINT_MAX;
	unsigned video_motion_packet5_at = UINT_MAX;
	unsigned video_motion_packet6_at = UINT_MAX;
	uint64_t motion_packet_at = UINT64_MAX;
	int motion_packet_pending = 0;
	uint8_t motion_sequence[MOTION_SEQUENCE_CAPACITY][XAVIX2_MOTION_PACKET_SIZE];
	unsigned motion_sequence_count = 0;
	unsigned motion_sequence_index = 0;
	uint64_t motion_sequence_at = UINT64_MAX;
	uint64_t motion_sequence_period = XAVIX2_CYCLES_PER_FRAME;
	unsigned video_frames = 0;
	unsigned video_input_at = UINT_MAX;
	unsigned video_input_release = UINT_MAX;
	unsigned video_input2_at = UINT_MAX;
	unsigned video_input2_release = UINT_MAX;
	unsigned video_input3_at = UINT_MAX;
	unsigned video_input3_release = UINT_MAX;
	unsigned video_input4_at = UINT_MAX;
	unsigned video_input4_release = UINT_MAX;
	unsigned video_input5_at = UINT_MAX;
	unsigned video_input5_release = UINT_MAX;
	unsigned video_input6_at = UINT_MAX;
	unsigned video_input6_release = UINT_MAX;
	unsigned video_autofire_at = UINT_MAX;
	unsigned video_autofire_end = UINT_MAX;
	unsigned video_autofire_period = 0;
	unsigned video_defense_at = UINT_MAX;
	unsigned video_trace_at = UINT_MAX;
	unsigned video_trace_period = 1;
	const char *audio_wav_path = NULL;
	int16_t *audio_wav_samples = NULL;
	size_t audio_wav_frames = 0;
	int release_unknown_poll = 0;
	int pulse_irq = 0;
	int fine_trace_started = 0;
	int capture_trace_verbose = 0;
	int sensor_buffer_trace_verbose = 0;
	int irq_context_trace_verbose = 0;
	int sensor_decoded_trace_verbose = 0;
	int motion_sample_trace_verbose = 0;
	int motion_source_trace_verbose = 0;
	int action_state_trace_verbose = 0;
	int diagnostic_ram_trace_verbose = 0;
	int instruction_trace_enabled = 0;
	instruction_trace instruction_log = { 0, 0, UINT32_MAX, 256, 0 };

	if ((argc < 2 || argc > 4) || !MultiByteToWideChar(CP_ACP, 0,
		argv[1], -1, path, (int)(sizeof(path) / sizeof(path[0]))))
	{
		fprintf(stderr, "usage: xavix2-boot-probe <ban_naru.zip> [byte-cycles] [frame.bmp]\n");
		return 64;
	}
	if (argc >= 3)
		requested = _strtoui64(argv[2], NULL, 0);
	if (!drgqst_rom_load_zip(path, &image, error, sizeof(error) / sizeof(error[0])))
	{
		fwprintf(stderr, L"%ls\n", error);
		return 2;
	}
	if (image.kind != DRGQST_ROM_BAN_NARU)
	{
		fprintf(stderr, "the boot probe only accepts ban_naru\n");
		drgqst_rom_release(&image);
		return 2;
	}
	machine = (xavix2_machine_t *)calloc(1, sizeof(*machine));
	if (!machine || !xavix2_machine_init(machine, image.data, image.size))
	{
		fprintf(stderr, "could not initialize XaviX 2 machine\n");
		free(machine);
		drgqst_rom_release(&image);
		return 2;
	}
	{
		const char *input = getenv("XAVIX2_INPUT");
		const char *at = getenv("XAVIX2_INPUT_AT");
		const char *release_at = getenv("XAVIX2_INPUT_RELEASE_AT");
		const char *irq = getenv("XAVIX2_IRQ");
		const char *irq_time = getenv("XAVIX2_IRQ_AT");
		const char *irq_clear_time = getenv("XAVIX2_IRQ_CLEAR_AT");
		const char *irq2 = getenv("XAVIX2_IRQ2");
		const char *irq2_time = getenv("XAVIX2_IRQ2_AT");
		const char *irq2_clear_time = getenv("XAVIX2_IRQ2_CLEAR_AT");
		const char *irq_period_text = getenv("XAVIX2_IRQ_PERIOD");
		const char *irq_width_text = getenv("XAVIX2_IRQ_WIDTH");
		const char *fine_trace_text = getenv("XAVIX2_FINE_TRACE_AT");
		const char *fine_trace_end_text = getenv("XAVIX2_FINE_TRACE_END");
		const char *irq_pulse = getenv("XAVIX2_IRQ_PULSE");
		const char *release_poll = getenv("XAVIX2_RELEASE_POLL");
		const char *direct_input = getenv("XAVIX2_DIRECT_INPUT");
		const char *dispatch_input = getenv("XAVIX2_DISPATCH_INPUT");
		const char *callback_address = getenv("XAVIX2_CALLBACK_PC");
		const char *input2 = getenv("XAVIX2_INPUT2");
		const char *input2_time = getenv("XAVIX2_INPUT2_AT");
		const char *input2_release = getenv("XAVIX2_INPUT2_RELEASE_AT");
		const char *capture_a = getenv("XAVIX2_CAPTURE_A");
		const char *capture_b = getenv("XAVIX2_CAPTURE_B");
		const char *capture_trace = getenv("XAVIX2_CAPTURE_TRACE");
		const char *sensor_buffer_trace = getenv("XAVIX2_SENSOR_BUFFER_TRACE");
		const char *irq_context_trace = getenv("XAVIX2_IRQ_CONTEXT_TRACE");
		const char *sensor_decoded_trace = getenv("XAVIX2_SENSOR_DECODED_TRACE");
		const char *motion_sample_trace = getenv("XAVIX2_MOTION_SAMPLE_TRACE");
		const char *motion_source_trace = getenv("XAVIX2_MOTION_SOURCE_TRACE");
		const char *action_state_trace = getenv("XAVIX2_ACTION_STATE_TRACE");
		const char *ram_trace_first = getenv("XAVIX2_RAM_TRACE_FIRST");
		const char *ram_trace_last = getenv("XAVIX2_RAM_TRACE_LAST");
		const char *ram_trace = getenv("XAVIX2_RAM_TRACE");
		const char *memory_trace_cycle = getenv("XAVIX2_MEMORY_TRACE_CYCLE");
		const char *trace_min = getenv("XAVIX2_TRACE_PC_MIN");
		const char *trace_max = getenv("XAVIX2_TRACE_PC_MAX");
		const char *trace_limit = getenv("XAVIX2_TRACE_LIMIT");
		const char *trace_cycle = getenv("XAVIX2_TRACE_CYCLE");
		const char *sensor_packet_text = getenv("XAVIX2_SENSOR_PACKET");
		const char *sensor_packet_time = getenv("XAVIX2_SENSOR_PACKET_AT");
		const char *motion_packet_text = getenv("XAVIX2_MOTION_PACKET");
		const char *motion_packet_time = getenv("XAVIX2_MOTION_PACKET_AT");
		const char *motion_sequence_text = getenv("XAVIX2_MOTION_SEQUENCE");
		const char *motion_sequence_time = getenv("XAVIX2_MOTION_SEQUENCE_AT");
		const char *motion_sequence_period_text = getenv("XAVIX2_MOTION_SEQUENCE_PERIOD");
		const char *video_frames_text = getenv("XAVIX2_VIDEO_FRAMES");
		const char *video_input_at_text = getenv("XAVIX2_VIDEO_INPUT_AT");
		const char *video_input_release_text =
			getenv("XAVIX2_VIDEO_INPUT_RELEASE");
		const char *video_motion2_text = getenv("XAVIX2_VIDEO_MOTION2");
		const char *video_motion2_at_text = getenv("XAVIX2_VIDEO_MOTION2_AT");
		const char *video_input2_at_text = getenv("XAVIX2_VIDEO_INPUT2_AT");
		const char *video_input2_release_text =
			getenv("XAVIX2_VIDEO_INPUT2_RELEASE");
		const char *video_motion3_text = getenv("XAVIX2_VIDEO_MOTION3");
		const char *video_motion3_at_text = getenv("XAVIX2_VIDEO_MOTION3_AT");
		const char *video_input3_at_text = getenv("XAVIX2_VIDEO_INPUT3_AT");
		const char *video_input3_release_text =
			getenv("XAVIX2_VIDEO_INPUT3_RELEASE");
		const char *video_motion4_text = getenv("XAVIX2_VIDEO_MOTION4");
		const char *video_motion4_at_text = getenv("XAVIX2_VIDEO_MOTION4_AT");
		const char *video_input4_at_text = getenv("XAVIX2_VIDEO_INPUT4_AT");
		const char *video_input4_release_text =
			getenv("XAVIX2_VIDEO_INPUT4_RELEASE");
		const char *video_motion5_text = getenv("XAVIX2_VIDEO_MOTION5");
		const char *video_motion5_at_text = getenv("XAVIX2_VIDEO_MOTION5_AT");
		const char *video_input5_at_text = getenv("XAVIX2_VIDEO_INPUT5_AT");
		const char *video_input5_release_text =
			getenv("XAVIX2_VIDEO_INPUT5_RELEASE");
		const char *video_motion6_text = getenv("XAVIX2_VIDEO_MOTION6");
		const char *video_motion6_at_text = getenv("XAVIX2_VIDEO_MOTION6_AT");
		const char *video_input6_at_text = getenv("XAVIX2_VIDEO_INPUT6_AT");
		const char *video_input6_release_text =
			getenv("XAVIX2_VIDEO_INPUT6_RELEASE");
		const char *video_autofire_at_text = getenv("XAVIX2_VIDEO_AUTOFIRE_AT");
		const char *video_autofire_end_text = getenv("XAVIX2_VIDEO_AUTOFIRE_END");
		const char *video_autofire_period_text =
			getenv("XAVIX2_VIDEO_AUTOFIRE_PERIOD");
		const char *video_defense_at_text = getenv("XAVIX2_VIDEO_DEFENSE_AT");
		const char *video_trace_at_text = getenv("XAVIX2_VIDEO_TRACE_AT");
		const char *video_trace_period_text =
			getenv("XAVIX2_VIDEO_TRACE_PERIOD");
		audio_wav_path = getenv("XAVIX2_AUDIO_WAV");
		if (input) pending_input = (uint32_t)strtoul(input, NULL, 0);
		if (at) input_at = _strtoui64(at, NULL, 0);
		if (release_at) input_release_at = _strtoui64(release_at, NULL, 0);
		if (irq) injected_irq = (unsigned)strtoul(irq, NULL, 0);
		clear_irq = injected_irq;
		if (irq_time) irq_at = _strtoui64(irq_time, NULL, 0);
		if (irq_clear_time) irq_clear_at = _strtoui64(irq_clear_time, NULL, 0);
		if (irq2) injected_irq2 = (unsigned)strtoul(irq2, NULL, 0);
		if (irq2_time) irq2_at = _strtoui64(irq2_time, NULL, 0);
		if (irq2_clear_time) irq2_clear_at = _strtoui64(irq2_clear_time, NULL, 0);
		if (irq_period_text) irq_period = _strtoui64(irq_period_text, NULL, 0);
		if (irq_width_text) irq_width = _strtoui64(irq_width_text, NULL, 0);
		if (fine_trace_text) fine_trace_at = _strtoui64(fine_trace_text, NULL, 0);
		if (fine_trace_end_text) fine_trace_end = _strtoui64(fine_trace_end_text, NULL, 0);
		if (irq_pulse) pulse_irq = atoi(irq_pulse) != 0;
		if (release_poll) release_unknown_poll = atoi(release_poll) != 0;
		if (direct_input) machine->experimental_direct_pio_sample = atoi(direct_input) != 0;
		if (dispatch_input) machine->experimental_dispatch_input = atoi(dispatch_input) != 0;
		if (callback_address)
			machine->experimental_callback_address = (uint32_t)strtoul(callback_address, NULL, 0);
		if (input2) pending_input2 = (uint32_t)strtoul(input2, NULL, 0);
		if (input2_time) input2_at = _strtoui64(input2_time, NULL, 0);
		if (input2_release) input2_release_at = _strtoui64(input2_release, NULL, 0);
		if (capture_a || capture_b)
			xavix2_machine_set_capture(machine,
				(uint16_t)(capture_a ? strtoul(capture_a, NULL, 0) : 0),
				(uint16_t)(capture_b ? strtoul(capture_b, NULL, 0) : 0));
		if (capture_trace) capture_trace_verbose = atoi(capture_trace) != 0;
		if (sensor_buffer_trace) sensor_buffer_trace_verbose = atoi(sensor_buffer_trace) != 0;
		if (irq_context_trace) irq_context_trace_verbose = atoi(irq_context_trace) != 0;
		if (sensor_decoded_trace) sensor_decoded_trace_verbose = atoi(sensor_decoded_trace) != 0;
		if (motion_sample_trace) motion_sample_trace_verbose = atoi(motion_sample_trace) != 0;
		if (motion_source_trace) motion_source_trace_verbose = atoi(motion_source_trace) != 0;
		if (action_state_trace) action_state_trace_verbose = atoi(action_state_trace) != 0;
		if (ram_trace_first && ram_trace_last)
		{
			machine->diagnostic_ram_first = (uint16_t)strtoul(ram_trace_first, NULL, 0);
			machine->diagnostic_ram_last = (uint16_t)strtoul(ram_trace_last, NULL, 0);
		}
		else
		{
			machine->diagnostic_ram_first = 1;
			machine->diagnostic_ram_last = 0;
		}
		if (ram_trace) diagnostic_ram_trace_verbose = atoi(ram_trace) != 0;
		if (memory_trace_cycle)
			machine->diagnostic_trace_start_cycle = _strtoui64(memory_trace_cycle, NULL, 0);
		if (trace_min)
		{
			instruction_log.minimum_pc = (uint32_t)strtoul(trace_min, NULL, 0);
			instruction_trace_enabled = 1;
		}
		if (trace_max)
		{
			instruction_log.maximum_pc = (uint32_t)strtoul(trace_max, NULL, 0);
			instruction_trace_enabled = 1;
		}
		if (trace_limit) instruction_log.limit = (uint32_t)strtoul(trace_limit, NULL, 0);
		if (trace_cycle) instruction_log.minimum_cycle = _strtoui64(trace_cycle, NULL, 0);
		if (video_frames_text)
			video_frames = (unsigned)strtoul(video_frames_text, NULL, 0);
		if (video_input_at_text)
			video_input_at = (unsigned)strtoul(video_input_at_text, NULL, 0);
		if (video_input_release_text)
			video_input_release = (unsigned)strtoul(
				video_input_release_text, NULL, 0);
		if (video_motion2_text)
		{
			if (!parse_motion_packet(video_motion2_text,
				video_motion_packet2))
			{
				fprintf(stderr, "XAVIX2_VIDEO_MOTION2 must be exactly 14 hexadecimal digits\n");
				free(machine);
				drgqst_rom_release(&image);
				return 64;
			}
			video_motion_packet2_valid = 1;
		}
		if (video_motion2_at_text)
			video_motion_packet2_at = (unsigned)strtoul(
				video_motion2_at_text, NULL, 0);
		if (video_input2_at_text)
			video_input2_at = (unsigned)strtoul(video_input2_at_text,
				NULL, 0);
		if (video_input2_release_text)
			video_input2_release = (unsigned)strtoul(
				video_input2_release_text, NULL, 0);
		if (video_motion3_text)
		{
			if (!parse_motion_packet(video_motion3_text,
				video_motion_packet3))
			{
				fprintf(stderr, "XAVIX2_VIDEO_MOTION3 must be exactly 14 hexadecimal digits\n");
				free(machine);
				drgqst_rom_release(&image);
				return 64;
			}
			video_motion_packet3_valid = 1;
		}
		if (video_motion3_at_text)
			video_motion_packet3_at = (unsigned)strtoul(
				video_motion3_at_text, NULL, 0);
		if (video_input3_at_text)
			video_input3_at = (unsigned)strtoul(video_input3_at_text,
				NULL, 0);
		if (video_input3_release_text)
			video_input3_release = (unsigned)strtoul(
				video_input3_release_text, NULL, 0);
		if (video_motion4_text)
		{
			if (!parse_motion_packet(video_motion4_text,
				video_motion_packet4))
			{
				fprintf(stderr, "XAVIX2_VIDEO_MOTION4 must be exactly 14 hexadecimal digits\n");
				free(machine);
				drgqst_rom_release(&image);
				return 64;
			}
			video_motion_packet4_valid = 1;
		}
		if (video_motion4_at_text)
			video_motion_packet4_at = (unsigned)strtoul(
				video_motion4_at_text, NULL, 0);
		if (video_input4_at_text)
			video_input4_at = (unsigned)strtoul(video_input4_at_text,
				NULL, 0);
		if (video_input4_release_text)
			video_input4_release = (unsigned)strtoul(
				video_input4_release_text, NULL, 0);
		if (video_motion5_text)
		{
			if (!parse_motion_packet(video_motion5_text,
				video_motion_packet5))
			{
				fprintf(stderr, "XAVIX2_VIDEO_MOTION5 must be exactly 14 hexadecimal digits\n");
				free(machine);
				drgqst_rom_release(&image);
				return 64;
			}
			video_motion_packet5_valid = 1;
		}
		if (video_motion5_at_text)
			video_motion_packet5_at = (unsigned)strtoul(
				video_motion5_at_text, NULL, 0);
		if (video_input5_at_text)
			video_input5_at = (unsigned)strtoul(video_input5_at_text,
				NULL, 0);
		if (video_input5_release_text)
			video_input5_release = (unsigned)strtoul(
				video_input5_release_text, NULL, 0);
		if (video_motion6_text)
		{
			if (!parse_motion_packet(video_motion6_text,
				video_motion_packet6))
			{
				fprintf(stderr, "XAVIX2_VIDEO_MOTION6 must be exactly 14 hexadecimal digits\n");
				free(machine);
				drgqst_rom_release(&image);
				return 64;
			}
			video_motion_packet6_valid = 1;
		}
		if (video_motion6_at_text)
			video_motion_packet6_at = (unsigned)strtoul(
				video_motion6_at_text, NULL, 0);
		if (video_input6_at_text)
			video_input6_at = (unsigned)strtoul(video_input6_at_text,
				NULL, 0);
		if (video_input6_release_text)
			video_input6_release = (unsigned)strtoul(
				video_input6_release_text, NULL, 0);
		if (video_autofire_at_text)
			video_autofire_at = (unsigned)strtoul(video_autofire_at_text,
				NULL, 0);
		if (video_autofire_end_text)
			video_autofire_end = (unsigned)strtoul(video_autofire_end_text,
				NULL, 0);
		if (video_autofire_period_text)
			video_autofire_period = (unsigned)strtoul(
				video_autofire_period_text, NULL, 0);
		if (video_defense_at_text)
			video_defense_at = (unsigned)strtoul(video_defense_at_text,
				NULL, 0);
		if (video_trace_at_text)
			video_trace_at = (unsigned)strtoul(video_trace_at_text, NULL, 0);
		if (video_trace_period_text)
		{
			video_trace_period = (unsigned)strtoul(video_trace_period_text,
				NULL, 0);
			if (!video_trace_period)
				video_trace_period = 1;
		}
		if (sensor_packet_text)
		{
			if (!parse_sensor_packet(sensor_packet_text, sensor_packet))
			{
				fprintf(stderr, "XAVIX2_SENSOR_PACKET must be exactly 32 hexadecimal digits\n");
				free(machine);
				drgqst_rom_release(&image);
				return 64;
			}
			sensor_packet_pending = 1;
			sensor_packet_at = sensor_packet_time ?
				_strtoui64(sensor_packet_time, NULL, 0) : irq_at;
		}
		if (motion_packet_text)
		{
			if (!parse_motion_packet(motion_packet_text, motion_packet))
			{
				fprintf(stderr, "XAVIX2_MOTION_PACKET must be exactly 14 hexadecimal digits\n");
				free(machine);
				drgqst_rom_release(&image);
				return 64;
			}
			motion_packet_pending = 1;
			motion_packet_at = motion_packet_time ?
				_strtoui64(motion_packet_time, NULL, 0) : irq_at;
		}
		if (motion_sequence_text)
		{
			if (!parse_motion_sequence(motion_sequence_text, motion_sequence,
				&motion_sequence_count))
			{
				fprintf(stderr, "XAVIX2_MOTION_SEQUENCE must contain 14-digit hexadecimal packets separated by commas\n");
				free(machine);
				drgqst_rom_release(&image);
				return 64;
			}
			motion_sequence_at = motion_sequence_time ?
				_strtoui64(motion_sequence_time, NULL, 0) : irq_at;
			if (motion_sequence_period_text)
				motion_sequence_period = _strtoui64(motion_sequence_period_text, NULL, 0);
			if (!motion_sequence_period)
				motion_sequence_period = 1;
		}
		if (instruction_trace_enabled)
		{
			machine->cpu.trace = trace_instruction;
			machine->cpu.trace_opaque = &instruction_log;
		}
		if (!input_at) machine->pio_input = pending_input;
	}

	if (video_frames)
	{
		unsigned frame;
		if (audio_wav_path && (size_t)video_frames <=
			SIZE_MAX / XAVIX2_AUDIO_FRAMES_PER_VIDEO_FRAME /
			XAVIX2_AUDIO_OUTPUT_CHANNELS / sizeof(*audio_wav_samples))
		{
			audio_wav_frames = (size_t)video_frames *
				XAVIX2_AUDIO_FRAMES_PER_VIDEO_FRAME;
			audio_wav_samples = (int16_t *)malloc(audio_wav_frames *
				XAVIX2_AUDIO_OUTPUT_CHANNELS * sizeof(*audio_wav_samples));
			if (!audio_wav_samples)
				fprintf(stderr, "could not allocate XaviX 2 WAV capture\n");
		}
		for (frame = 0; frame < video_frames; ++frame)
		{
			uint8_t defense_packet[XAVIX2_MOTION_PACKET_SIZE];
			const uint8_t *frame_packet = video_motion_packet6_valid &&
				frame >= video_motion_packet6_at ? video_motion_packet6 :
				video_motion_packet5_valid && frame >= video_motion_packet5_at ?
				video_motion_packet5 :
				video_motion_packet4_valid && frame >= video_motion_packet4_at ?
				video_motion_packet4 :
				video_motion_packet3_valid && frame >= video_motion_packet3_at ?
				video_motion_packet3 :
				video_motion_packet2_valid && frame >= video_motion_packet2_at ?
				video_motion_packet2 : motion_packet;
			if (frame >= video_defense_at)
			{
				unsigned phase = (frame - video_defense_at) % 48;
				uint8_t x = (uint8_t)(phase < 24 ? 8 + phase * 2 :
					8 + (47 - phase) * 2);
				defense_packet[0] = x;
				defense_packet[1] = 0x18;
				defense_packet[2] = 0x20;
				defense_packet[3] = x;
				defense_packet[4] = 0x18;
				defense_packet[5] = 0x20;
				defense_packet[6] = 0;
				frame_packet = defense_packet;
			}
			uint32_t frame_input = (frame >= video_input_at &&
				frame < video_input_release) ||
				(frame >= video_input2_at &&
				frame < video_input2_release) ||
				(frame >= video_input3_at &&
				frame < video_input3_release) ||
				(frame >= video_input4_at &&
				frame < video_input4_release) ||
				(frame >= video_input5_at &&
				frame < video_input5_release) ||
				(frame >= video_input6_at &&
				frame < video_input6_release) ||
				(video_autofire_period && frame >= video_autofire_at &&
				frame < video_autofire_end &&
				(frame - video_autofire_at) % video_autofire_period < 3) ?
				pending_input : 0;
			(void)xavix2_machine_run_video_frame(machine,
				frame_packet, frame_input);
			if (audio_wav_samples)
				memcpy(audio_wav_samples + (size_t)frame *
					XAVIX2_AUDIO_FRAMES_PER_VIDEO_FRAME *
					XAVIX2_AUDIO_OUTPUT_CHANNELS,
					xavix2_machine_frame_audio(machine),
					XAVIX2_AUDIO_FRAMES_PER_VIDEO_FRAME *
					XAVIX2_AUDIO_OUTPUT_CHANNELS * sizeof(*audio_wav_samples));
			if (frame >= video_trace_at &&
				(frame - video_trace_at) % video_trace_period == 0)
			{
				unsigned audio_byte;
				printf("video_frame=%u hardware_frame=%" PRIu64
					" byte_cycles=%" PRIu64 " instructions=%" PRIu64
					" interrupts=%" PRIu64 " pc=%08" PRIX32
					" hash=%016" PRIX64 " hit=%08" PRIX32 " audio=",
					frame + 1, machine->frame_count,
					machine->cpu.total_cycles,
					machine->cpu.total_instructions,
					machine->cpu.interrupt_count, machine->cpu.pc,
					frame_hash(machine), get_le32(machine->low_ram + 0x6468));
				for (audio_byte = 0;
					audio_byte < XAVIX2_AUDIO_VOICES / 8; ++audio_byte)
					printf("%02X", xavix2_audio_status(&machine->audio,
						audio_byte));
				putchar('\n');
			}
		}
		if (audio_wav_samples)
			printf("audio_wav=%s %s frames=%zu\n", audio_wav_path,
				save_wav(audio_wav_path, audio_wav_samples,
					audio_wav_frames) ? "saved" : "FAILED",
				audio_wav_frames);
		completed = machine->cpu.total_cycles;
		requested = completed;
	}

	while (completed < requested)
	{
		uint64_t slice = requested - completed;
		uint64_t ran;
		if (slice > EXECUTION_SLICE)
			slice = EXECUTION_SLICE;
		if (completed >= fine_trace_at && completed < fine_trace_end)
		{
			if (!fine_trace_started)
			{
				memset(samples, 0, sizeof(samples));
				fine_trace_started = 1;
			}
			slice = 1;
		}
		if (input_at && completed >= input_at && completed < input_release_at)
			machine->pio_input = pending_input;
		else if (completed >= input_release_at)
			machine->pio_input = 0;
		if (completed >= input2_at && completed < input2_release_at)
			machine->pio_input = pending_input2;
		else if (completed >= input2_release_at)
			machine->pio_input = 0;
		if (sensor_packet_pending && completed >= sensor_packet_at)
		{
			memcpy(machine->low_ram + XAVIX2_SENSOR_BUFFER_FIRST,
				sensor_packet, sizeof(sensor_packet));
			sensor_packet_pending = 0;
		}
		if (motion_packet_pending && completed >= motion_packet_at)
		{
			memcpy(machine->low_ram + XAVIX2_MOTION_PACKET_FIRST,
				motion_packet, sizeof(motion_packet));
			motion_packet_pending = 0;
		}
		if (motion_sequence_index < motion_sequence_count &&
			completed >= motion_sequence_at)
		{
			memcpy(machine->low_ram + XAVIX2_MOTION_PACKET_FIRST,
				motion_sequence[motion_sequence_index], XAVIX2_MOTION_PACKET_SIZE);
			motion_sequence_index++;
			motion_sequence_at += motion_sequence_period;
		}
		if (injected_irq < 32 && completed >= irq_at &&
			(!pulse_irq || (!machine->interrupt_active && machine->cpu.waiting)))
		{
			unsigned level = injected_irq;
			xavix2_machine_raise_irq(machine, level);
			if (pulse_irq)
			{
				uint64_t reads_before = machine->irq_level_read_count;
				unsigned steps;
				for (steps = 0; steps < 256 &&
					machine->irq_level_read_count == reads_before; ++steps)
					completed += xavix2_cpu_execute(&machine->cpu, 1);
				xavix2_machine_clear_irq(machine, level);
				clear_irq = 32;
			}
			if (irq_period)
			{
				clear_irq = level;
				irq_clear_at = completed + irq_width;
				irq_at += irq_period;
			}
			else
				injected_irq = 32;
		}
		if (clear_irq < 32 && completed >= irq_clear_at)
		{
			xavix2_machine_clear_irq(machine, clear_irq);
			clear_irq = 32;
		}
		if (injected_irq2 < 32 && completed >= irq2_at)
		{
			xavix2_machine_raise_irq(machine, injected_irq2);
			irq2_at = UINT64_MAX;
		}
		if (injected_irq2 < 32 && completed >= irq2_clear_at)
		{
			xavix2_machine_clear_irq(machine, injected_irq2);
			injected_irq2 = 32;
		}
		if (release_unknown_poll && machine->cpu.pc >= UINT32_C(0x4001f2c0) &&
			machine->cpu.pc <= UINT32_C(0x4001f2c5))
			machine->low_ram[0x0c5c] = 1;
		ran = xavix2_machine_execute(machine, slice);
		completed += ran;
		add_pc_sample(samples, machine->cpu.pc);
		if (!ran)
			break;
	}

	printf("game=ban_naru requested=%" PRIu64 " executed=%" PRIu64
		" input=%08" PRIX32 "\n", requested, completed, machine->pio_input);
	printf("PC=%08" PRIX32 " instructions=%" PRIu64 " byte_cycles=%" PRIu64
		" waiting=%u flags=%02" PRIX32 " interrupts=%" PRIu64 "\n",
		machine->cpu.pc, machine->cpu.total_instructions,
		machine->cpu.total_cycles, machine->cpu.waiting,
		machine->cpu.hr[4] & UINT32_C(0xff), machine->cpu.interrupt_count);
	printf("R0=%08" PRIX32 " R1=%08" PRIX32 " R2=%08" PRIX32
		" R3=%08" PRIX32 " R4=%08" PRIX32 " R5=%08" PRIX32
		" SP=%08" PRIX32 " LR=%08" PRIX32 "\n",
		machine->cpu.r[0], machine->cpu.r[1], machine->cpu.r[2], machine->cpu.r[3],
		machine->cpu.r[4], machine->cpu.r[5], machine->cpu.r[6], machine->cpu.r[7]);
	printf("unimplemented=%" PRIu64 " first=%08" PRIX32 ":%02X\n",
		machine->cpu.unimplemented_count, machine->cpu.first_unimplemented_pc,
		machine->cpu.first_unimplemented_opcode);
	printf("frames=%" PRIu64 " gpu_triggers=%" PRIu64 " pixels=%" PRIu64
		" first_gpu_pc=%08" PRIX32 " last_gpu_pc=%08" PRIX32
		" register=%08" PRIX32 " count=%u max_count=%u dma=%" PRIu64 "\n",
		machine->frame_count, machine->gpu_trigger_count,
		machine->gpu_pixel_write_count, machine->first_gpu_pc,
		machine->last_gpu_pc, machine->last_gpu_register, machine->last_gpu_count,
		machine->maximum_gpu_count,
		machine->dma_transfer_count);
	printf("irq_active=%08" PRIX32 " irq_enabled=%08" PRIX32
		" irq_nmi=%08" PRIX32 " level_reads=%" PRIu64
		" clear_writes=%" PRIu64 " last_clear=%04X@%08" PRIX32
		" eeprom_writes=%" PRIu32 "\n",
		machine->interrupt_active, machine->interrupt_enabled,
		machine->interrupt_nmi, machine->irq_level_read_count,
		machine->irq_clear_write_count, machine->last_irq_clear_mask,
		machine->last_irq_clear_pc, machine->eeprom.write_generation);
	printf("unmapped_reads=%" PRIu64 " first=%08" PRIX32
		" unmapped_writes=%" PRIu64 " first=%08" PRIX32 "\n",
		machine->unmapped_read_count, machine->first_unmapped_read,
		machine->unmapped_write_count, machine->first_unmapped_write);
	printf("poll_ram[0C5C]=%02X poll_ram[0C50]=%02X poll_ram[0C58]=%02X\n",
		machine->low_ram[0x0c5c], machine->low_ram[0x0c50], machine->low_ram[0x0c58]);
	printf("frame_hash=%016" PRIX64 "\n", frame_hash(machine));
	printf("low_ram_hash=%016" PRIX64 " video_ram_hash=%016" PRIX64 "\n",
		byte_hash(machine->low_ram, sizeof(machine->low_ram)),
		byte_hash(machine->video_ram, sizeof(machine->video_ram)));
	printf("pio_mode=%08" PRIX32 "/%08" PRIX32 " output_mask=%08" PRIX32
		" output_data=%08" PRIX32 " reads=%" PRIu64 " last=%08" PRIX32
		"@%08" PRIX32 " input_reads=%" PRIu64 " observed=%08" PRIX32 "\n",
		get_le32(machine->mmio + 0x200), get_le32(machine->mmio + 0x204),
		machine->pio_output_mask, get_le32(machine->mmio + 0x208),
		machine->pio_read_count, machine->last_pio_read_value,
		machine->last_pio_read_pc, machine->pio_input_read_count,
		machine->pio_observed_input_or);
	printf("input_state_reads=%" PRIu64 " last=%04X@%08" PRIX32 "\n",
		machine->input_state_read_count, machine->last_input_state_read_address,
		machine->last_input_state_read_pc);
	printf("input_state_regs=%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
		",%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
		",%08" PRIX32 "\n",
		machine->last_input_state_regs[0], machine->last_input_state_regs[1],
		machine->last_input_state_regs[2], machine->last_input_state_regs[3],
		machine->last_input_state_regs[4], machine->last_input_state_regs[5],
		machine->last_input_state_regs[6], machine->last_input_state_regs[7]);
	print_hex_bytes("motion_ram", UINT32_C(0x13a0),
		machine->low_ram + 0x13a0, 32);
	print_hex_bytes("motion_history", UINT32_C(0x121a),
		machine->low_ram + 0x121a, 96);
	print_capture_trace(machine, capture_trace_verbose);
	print_sensor_buffer_trace(machine, sensor_buffer_trace_verbose);
	print_irq_context_trace(machine, irq_context_trace_verbose);
	print_sensor_decoded_trace(machine, sensor_decoded_trace_verbose);
	print_motion_sample_trace(machine, motion_sample_trace_verbose);
	print_motion_source_trace(machine, motion_source_trace_verbose);
	print_action_state_trace(machine, action_state_trace_verbose);
	print_diagnostic_ram_trace(machine, diagnostic_ram_trace_verbose);
	{
		const char *ram_dump = getenv("XAVIX2_RAM_DUMP");
		const char *palette_dump = getenv("XAVIX2_PALETTE_DUMP");
		if (ram_dump)
			printf("ram_dump=%s %s\n", ram_dump,
				save_bytes(ram_dump, machine->low_ram, sizeof(machine->low_ram)) ?
				"saved" : "failed");
		if (palette_dump)
			printf("palette_dump=%s %s\n", palette_dump,
				save_bytes(palette_dump, machine->palette_ram,
					sizeof(machine->palette_ram)) ? "saved" : "failed");
	}
	print_hot_pcs(samples);
	printf("trace_points input=%u/%u callback=%u/%u dispatch=%u game=%u pio=%u/%u\n",
		pc_sample_count(samples, UINT32_C(0x0001f6ab)),
		pc_sample_count(samples, UINT32_C(0x0001f6be)),
		pc_sample_count(samples, UINT32_C(0x0001f332)),
		pc_sample_count(samples, UINT32_C(0x0001f342)),
		pc_sample_count(samples, UINT32_C(0x00020017)),
		pc_sample_count(samples, UINT32_C(0x00049ccf)),
		pc_sample_count(samples, UINT32_C(0x000259eb)),
		pc_sample_count(samples, UINT32_C(0x00025a52)));
	if (getenv("XAVIX2_SWEEP_INPUT"))
		sweep_input_pairs(machine);
	if (getenv("XAVIX2_SWEEP_SEQUENCE"))
		sweep_input_sequences(machine);
	if (getenv("XAVIX2_SWEEP_CAPTURE"))
		sweep_capture_sequences(machine);
	if (getenv("XAVIX2_SWEEP_TITLE_PIO"))
		sweep_title_pio_bits(machine);
	if (getenv("XAVIX2_SWEEP_SENSOR_PACKET"))
		sweep_sensor_packet_bits(machine);
	if (getenv("XAVIX2_SWEEP_MOTION_TARGETS"))
		sweep_motion_targets(machine);
	if (getenv("XAVIX2_MMIO_HOT"))
		print_hot_mmio(machine);
	if (getenv("XAVIX2_MMIO_ALL"))
		print_all_mmio(machine);
	if (getenv("XAVIX2_AUDIO_TRACE"))
		print_audio_mmio_trace(machine);
	print_audio_summary(machine);
	if (getenv("XAVIX2_AUDIO_VOICES"))
		print_audio_voices(machine);
	{
		const char *rom_first = getenv("XAVIX2_ROM_HEX_FIRST");
		const char *rom_length = getenv("XAVIX2_ROM_HEX_LENGTH");
		const char *ram_first = getenv("XAVIX2_RAM_HEX_FIRST");
		const char *ram_length = getenv("XAVIX2_RAM_HEX_LENGTH");
		const char *vram_first = getenv("XAVIX2_VRAM_HEX_FIRST");
		const char *vram_length = getenv("XAVIX2_VRAM_HEX_LENGTH");
		const char *ascii_find = getenv("XAVIX2_ROM_FIND_ASCII");
		const char *u32_find = getenv("XAVIX2_ROM_FIND_U32");
		if (rom_first && rom_length)
			print_hex_range("rom", image.data, image.size,
				(uint32_t)strtoul(rom_first, NULL, 0),
				(uint32_t)strtoul(rom_length, NULL, 0));
		if (ram_first && ram_length)
			print_hex_range("ram", machine->low_ram, sizeof(machine->low_ram),
				(uint32_t)strtoul(ram_first, NULL, 0),
				(uint32_t)strtoul(ram_length, NULL, 0));
		if (vram_first && vram_length)
			print_hex_range("vram", machine->video_ram,
				sizeof(machine->video_ram),
				(uint32_t)strtoul(vram_first, NULL, 0),
				(uint32_t)strtoul(vram_length, NULL, 0));
		if (ascii_find)
			find_ascii_text(image.data, image.size, ascii_find);
		if (u32_find)
			find_u32_value(image.data, image.size,
				(uint32_t)strtoul(u32_find, NULL, 0));
	}
	if (argc == 4)
		printf("frame=%s %s\n", argv[3],
			save_bmp(argv[3], machine) ? "saved" : "FAILED");

	free(audio_wav_samples);
	free(machine);
	drgqst_rom_release(&image);
	return 0;
}
