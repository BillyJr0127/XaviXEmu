// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "rom_loader.h"
#include "xavix2_machine.h"

#include <windows.h>

#include <ctype.h>
#include <float.h>
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
	unsigned opcode_filter;
	int opcode_filter_enabled;
	uint32_t register5_filter;
	int register5_filter_enabled;
	uint32_t limit;
	uint32_t count;
} instruction_trace;

enum
{
	GPU_SCALE_TRACE_PRINT_LIMIT = 256
};

typedef struct gpu_scale_trace
{
	uint64_t scanned_commands;
	uint64_t nondefault_commands;
	uint64_t last_trigger_count;
	unsigned frames_with_nondefault;
	unsigned printed_commands;
	unsigned first_frame;
	unsigned first_index;
	uint64_t first_command;
	uint32_t first_descriptor;
	uint32_t first_source;
	uint16_t first_width;
	uint16_t first_height;
	uint8_t first_w_field;
	uint8_t first_h_field;
	int first_valid;
} gpu_scale_trace;

typedef struct audio_channel_metrics
{
	uint32_t key_ons;
	uint32_t releases;
	uint32_t slides;
	uint32_t stops;
	uint32_t active_frames;
	uint32_t first_frame;
	uint32_t last_frame;
	uint16_t minimum_pitch;
	uint16_t maximum_pitch;
	uint8_t minimum_volume;
	uint8_t maximum_volume;
	uint8_t seen;
} audio_channel_metrics;

static void (*probe_original_write8)(void *opaque, uint32_t address,
	uint8_t data);
static uint64_t geometry_command_count[256];
static uint64_t gpu_submit_descriptor_count[64];
static uint64_t gpu_enemy_submit_count;
static int gpu_submit_trace_enabled;
static int geometry_detail_trace_enabled;
static int geometry_register_trace_enabled;
static int audio_command_trace_enabled;
static int audio_descriptor_trace_enabled;
static int audio_channel_metrics_enabled;
static unsigned probe_video_frame;
static unsigned geometry_attribute_write_prints;
static unsigned geometry_attribute_table_write_prints;
static unsigned geometry_matrix_trace_prints;
static unsigned geometry_project_trace_prints;
static unsigned geometry_register_write_prints;
static audio_channel_metrics audio_metrics[XAVIX2_AUDIO_VOICES];

static uint16_t get_le16(const uint8_t *source);
static uint32_t get_le32(const uint8_t *source);
static void put_le32(uint8_t *target, uint32_t value);
static int save_bmp(const char *path, const xavix2_machine_t *machine);

static int32_t probe_signed10(uint32_t value)
{
	return (int32_t)(value << 22) >> 22;
}

static void update_audio_metrics(unsigned channel, unsigned operation,
	uint16_t control_flags, const xavix2_audio_voice *voice)
{
	audio_channel_metrics *metrics = &audio_metrics[channel];
	if (!metrics->seen)
	{
		metrics->first_frame = probe_video_frame;
		metrics->minimum_pitch = UINT16_MAX;
		metrics->minimum_volume = UINT8_MAX;
		metrics->seen = 1;
	}
	metrics->last_frame = probe_video_frame;
	if (operation == 0x040 || operation == 0x240)
		metrics->key_ons++;
	else if (operation == 0x080)
		metrics->stops++;
	else if (operation == 0x0c0)
	{
		if (control_flags & UINT16_C(0x0100))
			metrics->releases++;
		else
			metrics->slides++;
	}
	if (operation != 0x080)
	{
		uint8_t volume = voice->volume_left > voice->volume_right ?
			voice->volume_left : voice->volume_right;
		if (voice->pitch < metrics->minimum_pitch)
			metrics->minimum_pitch = voice->pitch;
		if (voice->pitch > metrics->maximum_pitch)
			metrics->maximum_pitch = voice->pitch;
		if (volume < metrics->minimum_volume)
			metrics->minimum_volume = volume;
		if (volume > metrics->maximum_volume)
			metrics->maximum_volume = volume;
	}
}

static void trace_probe_write8(void *opaque, uint32_t address, uint8_t data)
{
	xavix2_machine_t *machine = (xavix2_machine_t *)opaque;
	uint16_t audio_command = 0x03f;
	uint16_t control_pitch = 0;
	uint16_t control_flags = 0;
	uint8_t control_left = 0;
	uint8_t control_right = 0;
	uint16_t geometry_source = 0;
	uint16_t geometry_destination = 0;
	uint16_t geometry_polygon = 0;
	uint16_t geometry_input_count = 0;
	uint16_t geometry_matrix_destination = 0;
	uint16_t geometry_apv_destination = 0;
	uint16_t geometry_apv_count = 0;
	uint32_t geometry_matrix_left_translation[3] = { 0, 0, 0 };
	uint32_t geometry_matrix_right_translation[3] = { 0, 0, 0 };
	unsigned geometry_matrix_index = 0;
	int geometry_matrix_pending = 0;
	int geometry_apv_pending = 0;
	if (geometry_register_trace_enabled && geometry_register_write_prints < 2048 &&
		((address >= UINT32_C(0xffffe400) && address <= UINT32_C(0xffffe414)) ||
		 (address >= UINT32_C(0xffffe800) && address <= UINT32_C(0xffffe8ff))))
	{
		printf("geometry_register_write frame=%u cycle=%" PRIu64
			" pc=%08" PRIX32 " address=%08" PRIX32
			" old=%02X new=%02X"
			" r=%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
			",%08" PRIX32 "\n",
			probe_video_frame, machine->cpu.total_cycles, machine->cpu.pc, address,
			machine->mmio[address & 0xfff], data, machine->cpu.r[0],
			machine->cpu.r[1], machine->cpu.r[2], machine->cpu.r[3]);
		geometry_register_write_prints++;
	}
	if (geometry_detail_trace_enabled && geometry_attribute_table_write_prints < 128 &&
		address >= 0xb740 && address < 0xb780)
	{
		printf("geometry_attribute_table_write frame=%u pc=%08" PRIX32
			" address=%04" PRIX32 " old=%02X new=%02X\n",
			probe_video_frame, machine->cpu.pc, address,
			machine->low_ram[address], data);
		geometry_attribute_table_write_prints++;
	}
	if (geometry_detail_trace_enabled && geometry_attribute_write_prints < 256 &&
		((address >= 0x36bc && address < 0x4700 && ((address - 0x36bc) & 15) == 0) ||
		 (address >= 0x55fc && address < 0x6600 && ((address - 0x55fc) & 15) == 0)) &&
		(data & 0x3f) > 4)
	{
		printf("geometry_attribute_write frame=%u pc=%08" PRIX32
			" address=%04" PRIX32 " old=%02X new=%02X\n",
			probe_video_frame, machine->cpu.pc, address,
			machine->low_ram[address], data);
		geometry_attribute_write_prints++;
	}
	if (address == UINT32_C(0xffffe858))
	{
		geometry_command_count[data]++;
		if (geometry_detail_trace_enabled && data == 0x02 &&
			geometry_project_trace_prints < 24)
		{
			uint16_t source = (uint16_t)(machine->mmio[0x860] |
				((uint16_t)machine->mmio[0x861] << 8));
			uint16_t destination = (uint16_t)(machine->mmio[0x862] |
				((uint16_t)machine->mmio[0x863] << 8));
			printf("geometry_02 frame=%u pc=%08" PRIX32
				" source=%04X destination=%04X count=%u focal=%d"
				" near=%04X far=%04X viewport=%04X,%04X input=%08" PRIX32
				",%08" PRIX32 ",%08" PRIX32 "\n",
				probe_video_frame, machine->cpu.pc, source, destination,
				(unsigned)(machine->mmio[0x85a] |
					((uint16_t)machine->mmio[0x85b] << 8)) + 1,
				(int16_t)(machine->mmio[0x840] |
					((uint16_t)machine->mmio[0x841] << 8)),
				(unsigned)(machine->mmio[0x844] |
					((uint16_t)machine->mmio[0x845] << 8)),
				(unsigned)(machine->mmio[0x846] |
					((uint16_t)machine->mmio[0x847] << 8)),
				(unsigned)(machine->mmio[0x848] |
					((uint16_t)machine->mmio[0x849] << 8)),
				(unsigned)(machine->mmio[0x84a] |
					((uint16_t)machine->mmio[0x84b] << 8)),
				get_le32(machine->low_ram + source),
				get_le32(machine->low_ram + source + 4),
				get_le32(machine->low_ram + source + 8));
			geometry_project_trace_prints++;
		}
		if (geometry_detail_trace_enabled && data == 0x10 &&
			geometry_matrix_trace_prints < 12)
		{
			uint16_t matrix_source = (uint16_t)(machine->mmio[0x860] |
				((uint16_t)machine->mmio[0x861] << 8));
			unsigned item;
			printf("geometry_matrix_10 frame=%u pc=%08" PRIX32 " source=%04X left=",
				probe_video_frame, machine->cpu.pc, matrix_source);
			for (item = 0; item < 12; ++item)
				printf("%s%08" PRIX32, item ? "," : "", get_le32(machine->mmio + 0x800 + item * 4));
			printf(" right=");
			for (item = 0; item < 12; ++item)
				printf("%s%08" PRIX32, item ? "," : "", get_le32(machine->low_ram + matrix_source + item * 4));
			printf("\n");
			geometry_matrix_trace_prints++;
		}
		if (geometry_detail_trace_enabled && data == 0x10 &&
			geometry_matrix_trace_prints >= 12 && geometry_matrix_trace_prints < 96)
		{
			uint16_t matrix_source = (uint16_t)(machine->mmio[0x860] |
				((uint16_t)machine->mmio[0x861] << 8));
			unsigned item;
			geometry_matrix_destination = (uint16_t)(machine->mmio[0x862] |
				((uint16_t)machine->mmio[0x863] << 8));
			geometry_matrix_index = geometry_matrix_trace_prints++;
			geometry_matrix_pending = 1;
			for (item = 0; item < 3; ++item)
			{
				geometry_matrix_left_translation[item] = get_le32(
					machine->mmio + 0x80c + item * 16);
				geometry_matrix_right_translation[item] = get_le32(
					machine->low_ram + matrix_source + 12 + item * 16);
			}
			if (geometry_matrix_index == 16 || geometry_matrix_index == 17 ||
				geometry_matrix_index == 20 || geometry_matrix_index == 21)
			{
				printf("geometry_matrix_10_key index=%u source=%04X left=",
					geometry_matrix_index, matrix_source);
				for (item = 0; item < 12; ++item)
					printf("%s%08" PRIX32, item ? "," : "",
						get_le32(machine->mmio + 0x800 + item * 4));
				printf(" right=");
				for (item = 0; item < 12; ++item)
					printf("%s%08" PRIX32, item ? "," : "",
						get_le32(machine->low_ram + matrix_source + item * 4));
				printf("\n");
			}
		}
		if (geometry_detail_trace_enabled && data == 0x4d)
		{
			geometry_source = (uint16_t)(machine->mmio[0x860] |
				((uint16_t)machine->mmio[0x861] << 8));
			geometry_destination = (uint16_t)(machine->mmio[0x862] |
				((uint16_t)machine->mmio[0x863] << 8));
			geometry_polygon = (uint16_t)(machine->mmio[0x864] |
				((uint16_t)machine->mmio[0x865] << 8));
			geometry_input_count = (uint16_t)(machine->mmio[0x85a] |
				((uint16_t)machine->mmio[0x85b] << 8));
			printf("geometry_4d_regs frame=%u pc=%08" PRIX32
				" source=%04X destination=%04X polygon=%04X count=%u"
				" focal=%d near=%d center=%u,%u first_vertex=%08" PRIX32
				",%08" PRIX32 ",%08" PRIX32 "\n",
				probe_video_frame, machine->cpu.pc, geometry_source,
				geometry_destination, geometry_polygon,
				(unsigned)geometry_input_count + 1,
				(int16_t)(machine->mmio[0x840] |
					((uint16_t)machine->mmio[0x841] << 8)),
				(int16_t)(machine->mmio[0x846] |
					((uint16_t)machine->mmio[0x847] << 8)),
				(unsigned)(machine->mmio[0x848] |
					((uint16_t)machine->mmio[0x849] << 8)),
				(unsigned)(machine->mmio[0x84a] |
					((uint16_t)machine->mmio[0x84b] << 8)),
				get_le32(machine->low_ram + geometry_source),
				get_le32(machine->low_ram + geometry_source + 4),
				get_le32(machine->low_ram + geometry_source + 8));
			{
				int32_t min_z = INT32_MAX;
				int32_t max_z = INT32_MIN;
				uint32_t maximum_vertex = 0;
				unsigned positive_triangles = 0;
				unsigned target_triangles = 0;
				unsigned positive_winding = 0;
				unsigned negative_winding = 0;
				unsigned item;
				int32_t focal = (int16_t)(machine->mmio[0x840] |
					((uint16_t)machine->mmio[0x841] << 8));
				int32_t center_x = (machine->mmio[0x848] |
					((uint16_t)machine->mmio[0x849] << 8)) / 2;
				int32_t center_y = (machine->mmio[0x84a] |
					((uint16_t)machine->mmio[0x84b] << 8)) / 2;
				for (item = 0; item <= geometry_input_count &&
					(uint32_t)geometry_polygon + item * 16 + 16 <=
						XAVIX2_LOW_RAM_SIZE; ++item)
				{
					const uint8_t *record = machine->low_ram + geometry_polygon +
						item * 16;
					uint32_t d0 = get_le32(record);
					uint32_t d1 = get_le32(record + 4);
					uint32_t vertices[3] = { d0 >> 16, d1 & 0xffff, d1 >> 16 };
					int32_t px[3];
					int32_t py[3];
					int positive = 1;
					int target = 1;
					unsigned vertex;
					for (vertex = 0; vertex < 3; ++vertex)
					{
						uint32_t address = (uint32_t)geometry_source +
							vertices[vertex] * 12;
						int32_t x, y, z;
						if (vertices[vertex] > maximum_vertex)
							maximum_vertex = vertices[vertex];
						if (address + 12 > XAVIX2_LOW_RAM_SIZE)
						{
							positive = target = 0;
							continue;
						}
						x = (int32_t)get_le32(machine->low_ram + address);
						y = (int32_t)get_le32(machine->low_ram + address + 4);
						z = (int32_t)get_le32(machine->low_ram + address + 8);
						if (z < min_z) min_z = z;
						if (z > max_z) max_z = z;
						if (z <= 0)
						{
							positive = target = 0;
							continue;
						}
						px[vertex] = center_x + (int64_t)x * focal / z;
						py[vertex] = center_y + (int64_t)y * focal / z;
						if (px[vertex] < 0 || px[vertex] > 0x7ff ||
							py[vertex] < 0 || py[vertex] > 0x3ff)
							target = 0;
					}
					if (positive) positive_triangles++;
					if (target)
					{
						int64_t area = (int64_t)(px[1] - px[0]) * (py[2] - py[0]) -
							(int64_t)(py[1] - py[0]) * (px[2] - px[0]);
						target_triangles++;
						if (area > 0) positive_winding++;
						else if (area < 0) negative_winding++;
					}
				}
				{
					unsigned visible = 0;
					unsigned clipped = 0;
					uint32_t minimum_depth = UINT32_MAX;
					uint32_t maximum_depth = 0;
					int minimum_x = INT_MAX, maximum_x = INT_MIN;
					int minimum_y = INT_MAX, maximum_y = INT_MIN;
					for (item = 0; item <= maximum_vertex &&
						(uint32_t)geometry_source + item * 8 + 8 <=
							XAVIX2_LOW_RAM_SIZE; ++item)
					{
						const uint8_t *vector = machine->low_ram +
							geometry_source + item * 8;
						uint32_t depth = get_le32(vector);
						int y = (int16_t)(vector[4] |
							((uint16_t)vector[5] << 8));
						int x = (int16_t)(vector[6] |
							((uint16_t)vector[7] << 8));
						if (depth & UINT32_C(0x80000000)) clipped++;
						else visible++;
						depth &= UINT32_C(0x7fffffff);
						if (depth < minimum_depth) minimum_depth = depth;
						if (depth > maximum_depth) maximum_depth = depth;
						if (x < minimum_x) minimum_x = x;
						if (x > maximum_x) maximum_x = x;
						if (y < minimum_y) minimum_y = y;
						if (y > maximum_y) maximum_y = y;
					}
					printf("geometry_vector16 vertices=%u visible=%u clipped=%u"
						" depth=%08" PRIX32 "..%08" PRIX32
						" x=%d..%d y=%d..%d\n", maximum_vertex + 1,
						visible, clipped, minimum_depth, maximum_depth,
						minimum_x, maximum_x, minimum_y, maximum_y);
				}
				printf("geometry_4d_input_range z=%" PRId32 "..%" PRId32
					" max_vertex=%u positive=%u target=%u winding=%u/%u\n",
					min_z, max_z, maximum_vertex, positive_triangles,
					target_triangles, positive_winding, negative_winding);
			}
			{
				uint32_t attributes[64] = { 0 };
				uint32_t item;
				uint32_t type1 = 0;
				for (item = 0; item <= geometry_input_count &&
					(uint32_t)geometry_polygon + item * 16 + 16 <=
						XAVIX2_LOW_RAM_SIZE; item++)
				{
					const uint8_t *record = machine->low_ram + geometry_polygon +
						item * 16;
					if (get_le32(record) & 1)
						type1++;
					else
						attributes[get_le32(record + 12) & 0x3f]++;
				}
				printf("geometry_4d_attributes polygon=%04X type1=%u",
					geometry_polygon, type1);
				for (item = 0; item < 64; item++)
					if (attributes[item])
						printf(" %02X=%u", item, attributes[item]);
				printf("\n");
			}
			{
				unsigned valid = 0;
				unsigned clipped = 0;
				unsigned backface = 0;
				unsigned forced_face = 0;
				unsigned bad_address = 0;
				unsigned item;
				uint32_t minimum_depth = UINT32_MAX;
				uint32_t maximum_depth = 0;
				for (item = 0; item <= geometry_input_count &&
					(uint32_t)geometry_polygon + item * 16 + 16 <=
						XAVIX2_LOW_RAM_SIZE; ++item)
				{
					const uint8_t *record = machine->low_ram + geometry_polygon +
						item * 16;
					uint32_t d0 = get_le32(record);
					uint32_t d1 = get_le32(record + 4);
					uint16_t vertices[3] = {
						(uint16_t)(d0 >> 16), (uint16_t)d1,
						(uint16_t)(d1 >> 16)
					};
					int32_t x[3] = { 0, 0, 0 };
					int32_t y[3] = { 0, 0, 0 };
					int polygon_clipped = 0;
					unsigned vertex;
					for (vertex = 0; vertex < 3; ++vertex)
					{
						uint32_t position = (uint32_t)geometry_source +
							vertices[vertex] * 8;
						uint32_t depth;
						if (position + 8 > XAVIX2_LOW_RAM_SIZE)
						{
							bad_address++;
							polygon_clipped = 1;
							break;
						}
						depth = get_le32(machine->low_ram + position);
						if ((depth & UINT32_C(0x7fffffff)) < minimum_depth)
							minimum_depth = depth & UINT32_C(0x7fffffff);
						if ((depth & UINT32_C(0x7fffffff)) > maximum_depth)
							maximum_depth = depth & UINT32_C(0x7fffffff);
						x[vertex] = (int16_t)(machine->low_ram[position + 6] |
							((uint16_t)machine->low_ram[position + 7] << 8)) >> 1;
						y[vertex] = (int16_t)(machine->low_ram[position + 4] |
							((uint16_t)machine->low_ram[position + 5] << 8)) >> 1;
						if (depth >> 31) polygon_clipped = 1;
					}
					if (polygon_clipped)
						clipped++;
					else if (!((d0 >> 1) & 1) &&
						(int64_t)(x[0] - x[1]) * (y[2] - y[1]) -
						(int64_t)(y[0] - y[1]) * (x[2] - x[1]) >= 0)
						backface++;
					else
					{
						valid++;
						if ((d0 >> 1) & 1) forced_face++;
					}
			}
		}
		}
		if (geometry_detail_trace_enabled && data == 0x0c)
		{
			geometry_apv_destination = (uint16_t)(machine->mmio[0x862] |
				((uint16_t)machine->mmio[0x863] << 8));
			geometry_apv_count = (uint16_t)(machine->mmio[0x85a] |
				((uint16_t)machine->mmio[0x85b] << 8));
			geometry_apv_pending = 1;
			printf("geometry_matrix_0c frame=%u pc=%08" PRIX32
				" source=%04X destination=%04X count=%u"
				" matrix=%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
				",%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
				",%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
				",%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32 "\n",
				probe_video_frame, machine->cpu.pc,
				(unsigned)(machine->mmio[0x860] |
					((uint16_t)machine->mmio[0x861] << 8)),
				(unsigned)(machine->mmio[0x862] |
					((uint16_t)machine->mmio[0x863] << 8)),
				(unsigned)(machine->mmio[0x85a] |
					((uint16_t)machine->mmio[0x85b] << 8)) + 1,
				get_le32(machine->mmio + 0x800),
				get_le32(machine->mmio + 0x804),
				get_le32(machine->mmio + 0x808),
				get_le32(machine->mmio + 0x80c),
				get_le32(machine->mmio + 0x810),
				get_le32(machine->mmio + 0x814),
				get_le32(machine->mmio + 0x818),
				get_le32(machine->mmio + 0x81c),
				get_le32(machine->mmio + 0x820),
				get_le32(machine->mmio + 0x824),
				get_le32(machine->mmio + 0x828),
				get_le32(machine->mmio + 0x82c));
		}
		if (geometry_detail_trace_enabled &&
			(data == 0x01 || data == 0x07 || data == 0x0d))
		{
			printf("geometry_aux frame=%u pc=%08" PRIX32 " command=%02X"
				" source=%04X destination=%04X polygon=%04X count=%u"
				" matrix=%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
				" source_data=%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
				" polygon_data=%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
				",%08" PRIX32 "\n",
				probe_video_frame, machine->cpu.pc, data,
				(unsigned)(machine->mmio[0x860] |
					((uint16_t)machine->mmio[0x861] << 8)),
				(unsigned)(machine->mmio[0x862] |
					((uint16_t)machine->mmio[0x863] << 8)),
				(unsigned)(machine->mmio[0x864] |
					((uint16_t)machine->mmio[0x865] << 8)),
				(unsigned)(machine->mmio[0x85a] |
					((uint16_t)machine->mmio[0x85b] << 8)) + 1,
				get_le32(machine->mmio + 0x800),
				get_le32(machine->mmio + 0x804),
				get_le32(machine->mmio + 0x808),
				get_le32(machine->low_ram + (machine->mmio[0x860] |
					((uint16_t)machine->mmio[0x861] << 8))),
				get_le32(machine->low_ram + (machine->mmio[0x860] |
					((uint16_t)machine->mmio[0x861] << 8)) + 4),
				get_le32(machine->low_ram + (machine->mmio[0x860] |
					((uint16_t)machine->mmio[0x861] << 8)) + 8),
				get_le32(machine->low_ram + (machine->mmio[0x864] |
					((uint16_t)machine->mmio[0x865] << 8))),
				get_le32(machine->low_ram + (machine->mmio[0x864] |
					((uint16_t)machine->mmio[0x865] << 8)) + 4),
				get_le32(machine->low_ram + (machine->mmio[0x864] |
					((uint16_t)machine->mmio[0x865] << 8)) + 8),
				get_le32(machine->low_ram + (machine->mmio[0x864] |
					((uint16_t)machine->mmio[0x865] << 8)) + 12));
			if (data == 0x01)
				printf("geometry_matrix_full %08" PRIX32 " %08" PRIX32
					" %08" PRIX32 " %08" PRIX32 " %08" PRIX32
					" %08" PRIX32 " %08" PRIX32 " %08" PRIX32
					" %08" PRIX32 " %08" PRIX32 " %08" PRIX32
					" %08" PRIX32 "\n",
					get_le32(machine->mmio + 0x800),
					get_le32(machine->mmio + 0x804),
					get_le32(machine->mmio + 0x808),
					get_le32(machine->mmio + 0x80c),
					get_le32(machine->mmio + 0x810),
					get_le32(machine->mmio + 0x814),
					get_le32(machine->mmio + 0x818),
					get_le32(machine->mmio + 0x81c),
					get_le32(machine->mmio + 0x820),
					get_le32(machine->mmio + 0x824),
					get_le32(machine->mmio + 0x828),
					get_le32(machine->mmio + 0x82c));
			if (data == 0x0d)
				printf("geometry_projector_regs %04X %04X %04X %04X %04X\n",
					machine->mmio[0x840] | ((uint16_t)machine->mmio[0x841] << 8),
					machine->mmio[0x846] | ((uint16_t)machine->mmio[0x847] << 8),
					machine->mmio[0x848] | ((uint16_t)machine->mmio[0x849] << 8),
					machine->mmio[0x84a] | ((uint16_t)machine->mmio[0x84b] << 8),
					machine->mmio[0x84c] | ((uint16_t)machine->mmio[0x84d] << 8));
		}
		if (geometry_detail_trace_enabled && data == 0x0b)
		{
			uint16_t source = (uint16_t)(machine->mmio[0x860] |
				((uint16_t)machine->mmio[0x861] << 8));
			uint16_t polygon = (uint16_t)(machine->mmio[0x864] |
				((uint16_t)machine->mmio[0x865] << 8));
			uint32_t count = (uint32_t)(machine->mmio[0x85a] |
				((uint16_t)machine->mmio[0x85b] << 8)) + 1;
			int32_t minimum_z = INT32_MAX;
			int32_t maximum_z = INT32_MIN;
			int64_t total_z = 0;
			uint32_t samples = 0;
			uint32_t item;
			printf("geometry_light_vector frame=%u value=%d,%d,%d\n",
				probe_video_frame,
				(int16_t)(machine->mmio[0x850] |
					((uint16_t)machine->mmio[0x851] << 8)),
				(int16_t)(machine->mmio[0x852] |
					((uint16_t)machine->mmio[0x853] << 8)),
				(int16_t)(machine->mmio[0x854] |
					((uint16_t)machine->mmio[0x855] << 8)));
			for (item = 0; item < count &&
				(uint32_t)polygon + item * 16 + 16 <= XAVIX2_LOW_RAM_SIZE;
				++item)
			{
				const uint8_t *record = machine->low_ram + polygon + item * 16;
				uint32_t d0 = get_le32(record);
				uint32_t d1 = get_le32(record + 4);
				uint16_t vertices[3] = {
					(uint16_t)(d0 >> 16), (uint16_t)d1, (uint16_t)(d1 >> 16)
				};
				unsigned vertex;
				if (!(d0 & 1))
					continue;
				for (vertex = 0; vertex < 3; ++vertex)
				{
					uint32_t address = (uint32_t)source + vertices[vertex] * 3 + 2;
					int32_t z;
					if (address + 1 > XAVIX2_LOW_RAM_SIZE)
						continue;
					z = (int8_t)machine->low_ram[address];
					if (z < minimum_z) minimum_z = z;
					if (z > maximum_z) maximum_z = z;
					total_z += z;
					samples++;
				}
			}
			printf("geometry_light_range frame=%u polygon=%04X count=%u"
				" samples=%u z=%" PRId32 "..%" PRId32 " avg=%" PRId64 "\n",
				probe_video_frame, polygon, count, samples,
				minimum_z, maximum_z, samples ? total_z / samples : 0);
			for (item = 0; item < 4 && item < count &&
				(uint32_t)polygon + item * 16 + 16 <= XAVIX2_LOW_RAM_SIZE; ++item)
			{
				const uint8_t *record = machine->low_ram + polygon + item * 16;
				uint32_t d0 = get_le32(record);
				uint32_t d1 = get_le32(record + 4);
				uint16_t vertices[3] = {
					(uint16_t)(d0 >> 16), (uint16_t)d1, (uint16_t)(d1 >> 16)
				};
				if ((d0 & 1) &&
					(uint32_t)source + vertices[0] * 3 + 3 <= XAVIX2_LOW_RAM_SIZE &&
					(uint32_t)source + vertices[1] * 3 + 3 <= XAVIX2_LOW_RAM_SIZE &&
					(uint32_t)source + vertices[2] * 3 + 3 <= XAVIX2_LOW_RAM_SIZE)
					printf("geometry_0b_record %u v=%u/%u/%u z=%d/%d/%d"
						" rgb=%04X/%04X/%04X\n", item,
						vertices[0], vertices[1], vertices[2],
						(int8_t)machine->low_ram[source + vertices[0] * 3 + 2],
						(int8_t)machine->low_ram[source + vertices[1] * 3 + 2],
						(int8_t)machine->low_ram[source + vertices[2] * 3 + 2],
						(unsigned)(get_le32(record + 8) & 0x7fff),
						(unsigned)((get_le32(record + 8) >> 16) & 0x7fff),
						(unsigned)(get_le32(record + 12) & 0x7fff));
			}
		}
		if (geometry_detail_trace_enabled && data == 0x0e)
		{
			uint16_t source = (uint16_t)(machine->mmio[0x860] |
				((uint16_t)machine->mmio[0x861] << 8));
			uint16_t destination = (uint16_t)(machine->mmio[0x862] |
				((uint16_t)machine->mmio[0x863] << 8));
			uint16_t polygon = (uint16_t)(machine->mmio[0x864] |
				((uint16_t)machine->mmio[0x865] << 8));
			uint32_t count = (uint32_t)(machine->mmio[0x85a] |
				((uint16_t)machine->mmio[0x85b] << 8)) + 1;
			printf("geometry_0e frame=%u pc=%08" PRIX32
				" source=%04X destination=%04X polygon=%04X count=%u"
				" matrix=%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
				",%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
				",%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
				" source0=%08" PRIX32 " record0=%08" PRIX32
				",%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32 "\n",
				probe_video_frame, machine->cpu.pc, source, destination,
				polygon, count,
				get_le32(machine->mmio + 0x800),
				get_le32(machine->mmio + 0x804),
				get_le32(machine->mmio + 0x808),
				get_le32(machine->mmio + 0x80c),
				get_le32(machine->mmio + 0x810),
				get_le32(machine->mmio + 0x814),
				get_le32(machine->mmio + 0x818),
				get_le32(machine->mmio + 0x81c),
				get_le32(machine->mmio + 0x820),
				source + 4 <= XAVIX2_LOW_RAM_SIZE ?
					get_le32(machine->low_ram + source) : 0,
				polygon + 16 <= XAVIX2_LOW_RAM_SIZE ?
					get_le32(machine->low_ram + polygon) : 0,
				polygon + 16 <= XAVIX2_LOW_RAM_SIZE ?
					get_le32(machine->low_ram + polygon + 4) : 0,
				polygon + 16 <= XAVIX2_LOW_RAM_SIZE ?
					get_le32(machine->low_ram + polygon + 8) : 0,
				polygon + 16 <= XAVIX2_LOW_RAM_SIZE ?
					get_le32(machine->low_ram + polygon + 12) : 0);
			{
				int32_t rotation[9];
				int32_t minimum_z = INT32_MAX;
				int32_t maximum_z = INT32_MIN;
				int64_t total_z = 0;
				uint32_t samples = 0;
				uint32_t positive_lights = 0;
				uint32_t zero_lights = 0;
				uint32_t item;
				unsigned matrix_index;
				for (matrix_index = 0; matrix_index < 9; ++matrix_index)
					rotation[matrix_index] = (int32_t)get_le32(
						machine->mmio + 0x800 + matrix_index * 4);
				for (item = 0; item < count &&
					(uint32_t)source + item * 4 + 4 <= XAVIX2_LOW_RAM_SIZE;
					++item)
				{
					uint32_t packed = get_le32(machine->low_ram + source + item * 4);
					int32_t coordinate[3];
					int64_t transformed_z;
					coordinate[0] = probe_signed10(packed);
					coordinate[1] = probe_signed10(packed >> 10);
					coordinate[2] = probe_signed10(packed >> 20);
					transformed_z = (int64_t)rotation[6] * coordinate[0] +
						(int64_t)rotation[7] * coordinate[1] +
						(int64_t)rotation[8] * coordinate[2];
					transformed_z >>= 10;
					if (transformed_z < minimum_z) minimum_z = (int32_t)transformed_z;
					if (transformed_z > maximum_z) maximum_z = (int32_t)transformed_z;
					total_z += transformed_z;
					samples++;
					if (transformed_z < 0) positive_lights++;
					else zero_lights++;
				}
				printf("geometry_0e_normals samples=%u z=%" PRId32
					"..%" PRId32 " avg=%" PRId64 " lit=%u dark=%u",
					samples, minimum_z, maximum_z,
					samples ? total_z / samples : 0,
					positive_lights, zero_lights);
				for (item = 0; item < 6 && item < count &&
					(uint32_t)source + item * 4 + 4 <= XAVIX2_LOW_RAM_SIZE;
					++item)
				{
					uint32_t packed = get_le32(machine->low_ram + source + item * 4);
					printf(" n%u=%" PRId32 ",%" PRId32 ",%" PRId32,
						item, probe_signed10(packed),
						probe_signed10(packed >> 10),
						probe_signed10(packed >> 20));
				}
				printf("\n");
				for (item = 0; item < 4 && item < count &&
					(uint32_t)polygon + item * 16 + 16 <= XAVIX2_LOW_RAM_SIZE;
					++item)
				{
					const uint8_t *record = machine->low_ram + polygon + item * 16;
					uint32_t d0 = get_le32(record);
					uint32_t d1 = get_le32(record + 4);
					uint16_t vertices[3] = {
						(uint16_t)(d0 >> 16), (uint16_t)d1,
						(uint16_t)(d1 >> 16)
					};
					printf("geometry_0e_record %u v=%u/%u/%u", item,
						vertices[0], vertices[1], vertices[2]);
					if (!(d0 & 1))
					{
						unsigned vertex;
						for (vertex = 0; vertex < 3; ++vertex)
						{
							uint32_t address = (uint32_t)source +
								vertices[vertex] * 4;
							uint32_t packed;
							if (address + 4 > XAVIX2_LOW_RAM_SIZE)
								continue;
							packed = get_le32(machine->low_ram + address);
							int64_t z = (int64_t)rotation[6] * probe_signed10(packed) +
								(int64_t)rotation[7] * probe_signed10(packed >> 10) +
								(int64_t)rotation[8] * probe_signed10(packed >> 20);
							printf(" n%u=%" PRId32 ",%" PRId32 ",%" PRId32
								"->%" PRId64, vertex, probe_signed10(packed),
								probe_signed10(packed >> 10),
								probe_signed10(packed >> 20), z >> 10);
						}
					}
					printf(" d2=%08" PRIX32 "\n", get_le32(record + 8));
				}
			}
			{
				uint32_t attributes[64] = { 0 };
				uint32_t item;
				uint32_t type1 = 0;
				for (item = 0; item < count &&
					(uint32_t)polygon + item * 16 + 16 <= XAVIX2_LOW_RAM_SIZE;
					item++)
				{
					const uint8_t *record = machine->low_ram + polygon + item * 16;
					if (get_le32(record) & 1)
						type1++;
					else
						attributes[get_le32(record + 12) & 0x3f]++;
				}
				printf("geometry_0e_attributes type1=%u", type1);
				for (item = 0; item < 64; item++)
					if (attributes[item])
						printf(" %02X=%u", item, attributes[item]);
				printf("\n");
			}
		}
		if (geometry_detail_trace_enabled && data == 0x0f)
			printf("geometry_0f frame=%u pc=%08" PRIX32
				" source=%04X destination=%04X count=%u\n",
				probe_video_frame, machine->cpu.pc,
				(unsigned)(machine->mmio[0x860] |
					((uint16_t)machine->mmio[0x861] << 8)),
				(unsigned)(machine->mmio[0x862] |
					((uint16_t)machine->mmio[0x863] << 8)),
				(unsigned)(machine->mmio[0x85a] |
					((uint16_t)machine->mmio[0x85b] << 8)) + 1);
	}
	if (geometry_detail_trace_enabled &&
		(address == UINT32_C(0xffffe408) ||
		 address == UINT32_C(0xffffe414)))
	{
		uint32_t offset = address & 0xfff;
		uint16_t list = (uint16_t)(machine->mmio[
			offset == 0x408 ? 0x400 : 0x40c] |
			((uint16_t)machine->mmio[
				offset == 0x408 ? 0x401 : 0x40d] << 8));
		uint16_t count = (uint16_t)(machine->mmio[
			offset == 0x408 ? 0x404 : 0x410] |
			((uint16_t)machine->mmio[
				offset == 0x408 ? 0x405 : 0x411] << 8));
		printf("gpu_trigger_event frame=%u register=%03X list=%04X/%u"
			" prepared=%02X\n", probe_video_frame, offset, list, count,
			machine->gpu_sprite_background_prepared);
	}
	if (gpu_submit_trace_enabled && address == UINT32_C(0xffffe414))
	{
		uint16_t list = (uint16_t)(machine->mmio[0x40c] |
			((uint16_t)machine->mmio[0x40d] << 8));
		uint16_t count = (uint16_t)(machine->mmio[0x410] |
			((uint16_t)machine->mmio[0x411] << 8));
		uint16_t descriptor_table = (uint16_t)(machine->mmio[0x608] |
			((uint16_t)machine->mmio[0x609] << 8));
		unsigned index;
		uint8_t seen[64] = { 0 };
		int enemy_seen = 0;
		for (index = 0; index < count && (uint32_t)list + index * 8 + 8 <=
			XAVIX2_LOW_RAM_SIZE; ++index)
		{
			const uint8_t *raw = machine->low_ram + list + index * 8;
			uint64_t command = (uint64_t)raw[0] |
				((uint64_t)raw[1] << 8) | ((uint64_t)raw[2] << 16) |
				((uint64_t)raw[3] << 24) | ((uint64_t)raw[4] << 32) |
				((uint64_t)raw[5] << 40) | ((uint64_t)raw[6] << 48) |
				((uint64_t)raw[7] << 56);
			unsigned descriptor_index = (unsigned)((command >> 30) & 0x3f);
			uint32_t descriptor_address = (uint32_t)descriptor_table +
				4 * descriptor_index;
			uint32_t descriptor = descriptor_address + 4 <= XAVIX2_LOW_RAM_SIZE ?
				(uint32_t)machine->low_ram[descriptor_address] |
				((uint32_t)machine->low_ram[descriptor_address + 1] << 8) |
				((uint32_t)machine->low_ram[descriptor_address + 2] << 16) |
				((uint32_t)machine->low_ram[descriptor_address + 3] << 24) : 0;
			unsigned data_index = (unsigned)((command >> 58) & 0x3f);
			uint16_t descriptor_data_table = (uint16_t)(machine->mmio[0x622] |
				((uint16_t)machine->mmio[0x623] << 8));
			uint32_t descriptor_data_address =
				(uint32_t)descriptor_data_table + data_index * 2;
			uint16_t descriptor_data =
				descriptor_data_address + 2 <= XAVIX2_LOW_RAM_SIZE ?
				get_le16(machine->low_ram + descriptor_data_address) : 0;
			unsigned bpp = 1 + ((descriptor >> 24) & 7);
			unsigned palette_base = ((descriptor >> 27) & 0x1f) << bpp;
			unsigned x = (unsigned)(command & 0x7ff);
			unsigned y = (unsigned)((command >> 11) & 0x3ff);
			seen[descriptor_index] = 1;
			if (gpu_submit_trace_enabled > 2)
				printf("gpu_sprite_submit probe_frame=%u hardware_frame=%" PRIu64
					" list=%04X/%u index=%u command=%016" PRIX64
					" descriptor_index=%u descriptor=%08" PRIX32
					" data_index=%u descdata=%04X source=%08" PRIX32
					" pos=%u,%u depth=%02X filter=%u bpp=%u palette_base=%u\n",
					probe_video_frame, machine->frame_count, list, count, index, command,
					descriptor_index, descriptor, data_index, descriptor_data,
					((uint32_t)descriptor_data << 14) +
						(uint32_t)((command >> 43) & 0x7fe0), x, y,
					(unsigned)((command >> 21) & 0xff),
					(unsigned)((command >> 29) & 1), bpp, palette_base);
			if (gpu_submit_trace_enabled > 3 && probe_video_frame >= 4 &&
				probe_video_frame <= 6 && x >= 0x300 && x < 0x480 &&
				y >= 0x160 && y < 0x240 && bpp <= 4)
			{
				unsigned palette_count = 1U << bpp;
				unsigned palette_index;
				printf("gpu_sprite_palette probe_frame=%u index=%u"
					" descriptor_index=%u base=%u",
					probe_video_frame, index, descriptor_index, palette_base);
				for (palette_index = 0; palette_index < palette_count &&
					palette_base + palette_index < 0x200; ++palette_index)
					printf(" %u:%04X", palette_index,
						get_le16(machine->palette_ram +
							(palette_base + palette_index) * 4));
				printf("\n");
			}
			if (descriptor == UINT32_C(0xe300654d))
			{
				enemy_seen = 1;
				if (gpu_submit_trace_enabled > 1)
					printf("gpu_enemy_submit hardware_frame=%" PRIu64
						" list=%04X/%u index=%u descriptor_index=%u"
						" command=%016" PRIX64 "\n", machine->frame_count,
						list, count, index, descriptor_index, command);
			}
		}
		gpu_enemy_submit_count += enemy_seen;
		for (index = 0; index < 64; ++index)
			gpu_submit_descriptor_count[index] += seen[index];
	}
	if (geometry_detail_trace_enabled && address == UINT32_C(0xffffe408))
	{
		uint16_t list = (uint16_t)(machine->mmio[0x400] |
			((uint16_t)machine->mmio[0x401] << 8));
		uint16_t count = (uint16_t)(machine->mmio[0x404] |
			((uint16_t)machine->mmio[0x405] << 8));
		uint32_t attributes[64] = { 0 };
		uint32_t type1 = 0;
		uint32_t index;
		for (index = 0; index < count &&
			(uint32_t)list + index * 16 + 16 <= XAVIX2_LOW_RAM_SIZE; index++)
		{
			const uint8_t *record = machine->low_ram + list + index * 16;
			if (get_le32(record) & 1)
				type1++;
			else
				attributes[get_le32(record + 12) & 0x3f]++;
		}
		printf("gpu0_submit frame=%u list=%04X/%u type1=%u", probe_video_frame,
			list, count, type1);
		for (index = 0; index < 64; index++)
			if (attributes[index])
				printf(" %02X=%u", index, attributes[index]);
		printf("\n");
	}
	if (audio_command_trace_enabled &&
		(address == UINT32_C(0xffffea00) ||
		 address == UINT32_C(0xffffea05)) &&
		machine->mmio[address & 0xfff] != data)
	{
		printf("audio_divider frame=%u cycle=%" PRIu64
			" pc=%08" PRIX32 " address=%08" PRIX32
			" old=%u new=%u rate_before=%" PRIu32,
			probe_video_frame, machine->cpu.total_cycles, machine->cpu.pc, address,
			(unsigned)machine->mmio[address & 0xfff], (unsigned)data,
			xavix2_audio_engine_rate(machine->mmio[0xa00], machine->mmio[0xa05]));
		if (address == UINT32_C(0xffffea00))
			printf(" rate_after=%" PRIu32 "\n",
				xavix2_audio_engine_rate(data, machine->mmio[0xa05]));
		else
			printf(" rate_after=%" PRIu32 "\n",
				xavix2_audio_engine_rate(machine->mmio[0xa00], data));
	}
	if ((audio_command_trace_enabled || audio_channel_metrics_enabled) &&
		address == UINT32_C(0xffffea0b))
	{
		audio_command = (uint16_t)(machine->mmio[0xa0a] |
			((uint16_t)data << 8));
		control_pitch = (uint16_t)(machine->mmio[0xa18] |
			((uint16_t)machine->mmio[0xa19] << 8));
		control_flags = (uint16_t)(machine->mmio[0xa1a] |
			((uint16_t)machine->mmio[0xa1b] << 8));
		control_left = machine->mmio[0xa1c];
		control_right = machine->mmio[0xa1d];
	}
	probe_original_write8(opaque, address, data);
	if (gpu_submit_trace_enabled > 4 &&
		address == UINT32_C(0xffffe414) && probe_video_frame >= 4 &&
		probe_video_frame <= 6)
	{
		static unsigned captured_frame;
		static unsigned captured_stage;
		const char *prefix = getenv("XAVIX2_GPU_STAGE_CAPTURE");
		if (captured_frame != probe_video_frame)
		{
			captured_frame = probe_video_frame;
			captured_stage = 0;
		}
		captured_stage++;
		if (prefix)
		{
			char path[MAX_PATH];
			snprintf(path, sizeof(path), "%s-f%02u-s%02u.bmp", prefix,
				probe_video_frame, captured_stage);
			(void)save_bmp(path, machine);
		}
	}
	if (geometry_apv_pending)
	{
		unsigned item;
		printf("geometry_apv_result cycle=%" PRIu64 " pc=%08" PRIX32 " destination=%04X count=%u", machine->cpu.total_cycles, machine->cpu.pc,
			geometry_apv_destination, (unsigned)geometry_apv_count + 1);
		for (item = 0; item <= geometry_apv_count && item < 4; ++item)
		{
			const uint8_t *vector = machine->low_ram + geometry_apv_destination +
				item * 8;
			printf(" %08" PRIX32 "/%04X/%04X", get_le32(vector),
				get_le16(vector + 4), get_le16(vector + 6));
		}
		printf("\n");
	}
	if (geometry_matrix_pending)
	{
		printf("geometry_matrix_10_result index=%u destination=%04X"
			" left_t=%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
			" right_t=%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
			" output_t=%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32 "\n",
			geometry_matrix_index, geometry_matrix_destination,
			geometry_matrix_left_translation[0],
			geometry_matrix_left_translation[1],
			geometry_matrix_left_translation[2],
			geometry_matrix_right_translation[0],
			geometry_matrix_right_translation[1],
			geometry_matrix_right_translation[2],
			get_le32(machine->low_ram + geometry_matrix_destination + 12),
			get_le32(machine->low_ram + geometry_matrix_destination + 28),
			get_le32(machine->low_ram + geometry_matrix_destination + 44));
	}
	if (geometry_polygon)
	{
		unsigned normal_positive = 0;
		unsigned normal_negative = 0;
		unsigned normal_zero = 0;
		unsigned type0 = 0;
		unsigned projected_visible = 0;
		unsigned viewport_visible = 0;
		unsigned item;
		for (item = 0; item <= geometry_input_count &&
			(uint32_t)geometry_polygon + item * 16 + 16 <=
				XAVIX2_LOW_RAM_SIZE; ++item)
		{
			const uint8_t *record = machine->low_ram + geometry_polygon + item * 16;
			uint32_t d0 = get_le32(record);
			uint32_t d1 = get_le32(record + 4);
			uint16_t vertices[3] = {
				(uint16_t)(d0 >> 16), (uint16_t)d1, (uint16_t)(d1 >> 16)
			};
			int64_t normal_z = 0;
			int32_t projected_x[3];
			int32_t projected_y[3];
			int projected_valid = 1;
			unsigned vertex;
			type0 += !(d0 & 1);
			for (vertex = 0; vertex < 3; ++vertex)
			{
				uint32_t normal_address = (uint32_t)geometry_destination +
					vertices[vertex] * 12 + 8;
				if (normal_address + 4 <= XAVIX2_LOW_RAM_SIZE)
					normal_z += (int32_t)get_le32(machine->low_ram +
						normal_address);
				{
					uint32_t position_address = (uint32_t)geometry_source +
						vertices[vertex] * 12;
					int32_t focal = (int16_t)(machine->mmio[0x840] |
						((uint16_t)machine->mmio[0x841] << 8));
					int32_t near_z = (int16_t)(machine->mmio[0x846] |
						((uint16_t)machine->mmio[0x847] << 8));
					if (position_address + 12 > XAVIX2_LOW_RAM_SIZE)
						projected_valid = 0;
					else
					{
						int32_t x = (int32_t)get_le32(machine->low_ram +
							position_address);
						int32_t y = (int32_t)get_le32(machine->low_ram +
							position_address + 4);
						int32_t z = (int32_t)get_le32(machine->low_ram +
							position_address + 8);
						if (z <= 0 || z < near_z * INT32_C(65536))
							projected_valid = 0;
						else
						{
							projected_x[vertex] = (int32_t)(
								(machine->mmio[0x848] |
								((uint16_t)machine->mmio[0x849] << 8)) / 2 +
								(int64_t)x * focal / z);
							projected_y[vertex] = (int32_t)(
								(machine->mmio[0x84a] |
								((uint16_t)machine->mmio[0x84b] << 8)) / 2 +
								(int64_t)y * focal / z);
							if (projected_x[vertex] < 0 || projected_x[vertex] > 0x7ff ||
								projected_y[vertex] < 0 || projected_y[vertex] > 0x3ff)
								projected_valid = 0;
						}
					}
				}
			}
			if (normal_z > 0) normal_positive++;
			else if (normal_z < 0) normal_negative++;
			else normal_zero++;
			if (projected_valid)
			{
				int64_t area = (int64_t)(projected_x[1] - projected_x[0]) *
					(projected_y[2] - projected_y[0]) -
					(int64_t)(projected_y[1] - projected_y[0]) *
					(projected_x[2] - projected_x[0]);
				if (area > 0)
				{
					int32_t min_x = projected_x[0];
					int32_t max_x = projected_x[0];
					int32_t min_y = projected_y[0];
					int32_t max_y = projected_y[0];
					int32_t crop_x = machine->mmio[0x656] |
						((uint16_t)machine->mmio[0x657] << 8);
					int32_t crop_y = machine->mmio[0x658] |
						((uint16_t)machine->mmio[0x659] << 8);
					for (vertex = 1; vertex < 3; ++vertex)
					{
						if (projected_x[vertex] < min_x) min_x = projected_x[vertex];
						if (projected_x[vertex] > max_x) max_x = projected_x[vertex];
						if (projected_y[vertex] < min_y) min_y = projected_y[vertex];
						if (projected_y[vertex] > max_y) max_y = projected_y[vertex];
					}
					projected_visible++;
					if (max_x >= crop_x && min_x < crop_x + 320 &&
						max_y >= crop_y && min_y < crop_y + 240)
						viewport_visible++;
				}
			}
		}
		printf("geometry_4d frame=%u source=%04X normals=%04X polygon=%04X"
			" input=%u output=%u type=%u/%u nz=%u/%u/%u projected=%u/%u\n",
			probe_video_frame, geometry_source, geometry_destination,
			geometry_polygon, (unsigned)geometry_input_count + 1,
			(unsigned)(machine->mmio[0x85c] |
				((uint16_t)machine->mmio[0x85d] << 8)), type0,
			(unsigned)geometry_input_count + 1 - type0, normal_positive,
			normal_negative, normal_zero, projected_visible, viewport_visible);
		{
			uint32_t attributes[64] = { 0 };
			uint32_t output_count = (uint32_t)(machine->mmio[0x85c] |
				((uint16_t)machine->mmio[0x85d] << 8));
			uint32_t minimum_x = 0x7ff, maximum_x = 0;
			uint32_t minimum_y = 0x3ff, maximum_y = 0;
			for (item = 0; item < output_count &&
				(uint32_t)geometry_polygon + item * 16 + 16 <=
					XAVIX2_LOW_RAM_SIZE; item++)
			{
				const uint8_t *record = machine->low_ram + geometry_polygon +
					item * 16;
				uint32_t d0 = get_le32(record);
				uint32_t d1 = get_le32(record + 4);
				uint32_t x[3] = { (d0 >> 11) & 0x7ff, d1 & 0x7ff,
					(d1 >> 21) & 0x7ff };
				uint32_t y[3] = { (d0 >> 1) & 0x3ff, (d0 >> 22) & 0x3ff,
					(d1 >> 11) & 0x3ff };
				unsigned vertex;
				for (vertex = 0; vertex < 3; ++vertex)
				{
					if (x[vertex] < minimum_x) minimum_x = x[vertex];
					if (x[vertex] > maximum_x) maximum_x = x[vertex];
					if (y[vertex] < minimum_y) minimum_y = y[vertex];
					if (y[vertex] > maximum_y) maximum_y = y[vertex];
				}
				if (!(d0 & 1))
					attributes[get_le32(record + 12) & 0x3f]++;
			}
			printf("geometry_4d_output_attributes polygon=%04X range=%u..%u,%u..%u",
				geometry_polygon, minimum_x, maximum_x, minimum_y, maximum_y);
			for (item = 0; item < 64; item++)
				if (attributes[item])
					printf(" %02X=%u", item, attributes[item]);
			printf("\n");
		}
	}
	if (audio_command != 0x03f)
	{
		unsigned channel = audio_command & 0x3f;
		unsigned operation = audio_command & 0x3c0;
		const xavix2_audio_voice *voice = &machine->audio.voice[channel];
		if (audio_channel_metrics_enabled)
			update_audio_metrics(channel, operation, control_flags, voice);
		if (audio_command_trace_enabled)
		{
			printf("audio_command frame=%u cycle=%" PRIu64
				" pc=%08" PRIX32 " cmd=%03X ch=%u"
				" control=%u,%u,%u,%u active=%u loop=%u release=%u"
				" pitch=%u volume=%u,%u"
				" start=%08" PRIX32 " loop_address=%08" PRIX32 "\n",
				probe_video_frame, machine->cpu.total_cycles, machine->cpu.pc,
				audio_command, channel, control_pitch, control_flags,
				control_left, control_right, voice->active, voice->loop,
				voice->release_phase, voice->pitch, voice->volume_left,
				voice->volume_right, voice->start_address,
				voice->loop_address);
			if (audio_descriptor_trace_enabled)
			{
				const uint8_t *descriptor = machine->video_ram + 0xf800 +
					channel * XAVIX2_AUDIO_DESCRIPTOR_SIZE;
				unsigned byte;
				printf("  command_descriptor=");
				for (byte = 0; byte < XAVIX2_AUDIO_DESCRIPTOR_SIZE; ++byte)
					printf("%02X", descriptor[byte]);
				putchar('\n');
			}
		}
	}
}

static void trace_instruction(void *opaque, const xavix2_cpu_t *cpu,
	uint32_t pc, uint32_t opcode, uint8_t bytes)
{
	instruction_trace *trace = (instruction_trace *)opaque;
	if (cpu->total_cycles < trace->minimum_cycle ||
		pc < trace->minimum_pc || pc > trace->maximum_pc ||
		(trace->opcode_filter_enabled &&
			(opcode >> 24) != trace->opcode_filter) ||
		(trace->register5_filter_enabled &&
			cpu->r[5] != trace->register5_filter) ||
		trace->count >= trace->limit)
		return;
	printf("insn cycle=%" PRIu64 " pc=%08" PRIX32 " raw=%0*" PRIX32
		" r=%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
		",%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
		" hr=%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32 ",%08" PRIX32
		" flags=%02" PRIX32 "\n",
		cpu->total_cycles, pc, bytes * 2, opcode >> (32 - bytes * 8),
		cpu->r[0], cpu->r[1], cpu->r[2], cpu->r[3], cpu->r[4], cpu->r[5],
		cpu->r[6], cpu->r[7], cpu->hr[0], cpu->hr[1], cpu->hr[2], cpu->hr[3],
		cpu->hr[4] & UINT32_C(0xff));
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

static int parse_u32_sequence(const char *text,
	uint32_t words[MOTION_SEQUENCE_CAPACITY], unsigned *count)
{
	char token[17];
	*count = 0;
	while (text && *text)
	{
		const char *separator = strchr(text, ',');
		char *end;
		size_t length = separator ? (size_t)(separator - text) : strlen(text);
		if (*count >= MOTION_SEQUENCE_CAPACITY || !length ||
			length >= sizeof(token))
			return 0;
		memcpy(token, text, length);
		token[length] = '\0';
		words[*count] = (uint32_t)strtoul(token, &end, 16);
		if (!*token || *end)
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

static uint16_t motion_packet_address_for_rom(enum drgqst_rom_kind kind)
{
	if (kind == DRGQST_ROM_BAN_DB2J)
		return 0x014d;
	if (kind == DRGQST_ROM_BAN_DBZ)
		return 0x0149;
	return XAVIX2_MOTION_PACKET_FIRST;
}

static int rom_uses_motion_packet(enum drgqst_rom_kind kind)
{
	return kind == DRGQST_ROM_BAN_NARU ||
		kind == DRGQST_ROM_BAN_BLDJ ||
		kind == DRGQST_ROM_BAN_DB2J ||
		kind == DRGQST_ROM_BAN_DBZ;
}

static uint32_t fixed_pio_input_for_rom(enum drgqst_rom_kind kind)
{
	return kind == DRGQST_ROM_BAN_DBZ ? UINT32_C(1) << 23 : 0;
}

static uint16_t get_le16(const uint8_t *source)
{
	return (uint16_t)(source[0] | ((uint16_t)source[1] << 8));
}

static uint64_t get_le64(const uint8_t *source)
{
	return (uint64_t)get_le32(source) |
		((uint64_t)get_le32(source + 4) << 32);
}

static void trace_gpu_scale_fields(const xavix2_machine_t *machine,
	unsigned frame, int descriptor_filter, gpu_scale_trace *trace)
{
	uint16_t address;
	uint16_t count;
	uint16_t descsize_address;
	uint16_t descdata_address;
	unsigned frame_nondefault = 0;
	unsigned index;

	if (machine->gpu_trigger_count == trace->last_trigger_count)
		return;
	trace->last_trigger_count = machine->gpu_trigger_count;
	if ((machine->last_gpu_register & UINT32_C(0xfff)) == 0x408)
	{
		address = get_le16(machine->mmio + 0x400);
		count = get_le16(machine->mmio + 0x404);
	}
	else
	{
		address = get_le16(machine->mmio + 0x40c);
		count = get_le16(machine->mmio + 0x410);
	}
	descsize_address = get_le16(machine->mmio + 0x608);
	descdata_address = get_le16(machine->mmio + 0x622);
	for (index = 0; index < count; ++index)
	{
		uint32_t command_address = (uint32_t)address + 8 * index;
		uint64_t command;
		uint32_t descriptor_index;
		uint32_t data_index;
		uint32_t descriptor_address;
		uint32_t data_address;
		uint32_t descriptor;
		uint16_t descdata;
		uint32_t source;
		uint16_t width;
		uint16_t height;
		uint8_t w_field;
		uint8_t h_field;

		if (command_address + 8 > XAVIX2_LOW_RAM_SIZE)
			break;
		command = get_le64(machine->low_ram + command_address);
		descriptor_index = (uint32_t)((command >> 30) & 0x3f);
		if (descriptor_filter >= 0 &&
			descriptor_index != (uint32_t)descriptor_filter)
			continue;
		data_index = (uint32_t)((command >> 58) & 0x3f);
		descriptor_address = (uint32_t)descsize_address +
			4 * descriptor_index;
		data_address = (uint32_t)descdata_address + 2 * data_index;
		if (descriptor_address + 4 > XAVIX2_LOW_RAM_SIZE ||
			data_address + 2 > XAVIX2_LOW_RAM_SIZE)
			continue;
		descriptor = get_le32(machine->low_ram + descriptor_address);
		descdata = get_le16(machine->low_ram + data_address);
		source = ((uint32_t)descdata << 14) +
			(uint32_t)((command >> 43) & 0x7fe0);
		width = (uint16_t)(1 + (descriptor & 0xff));
		height = (uint16_t)(1 + ((descriptor >> 8) & 0xff));
		w_field = (uint8_t)((command >> 36) & 0x3f);
		h_field = (uint8_t)((command >> 42) & 0x3f);
		trace->scanned_commands++;
		if (w_field == 0x10 && h_field == 0x10)
			continue;
		trace->nondefault_commands++;
		frame_nondefault++;
		if (!trace->first_valid)
		{
			trace->first_valid = 1;
			trace->first_frame = frame;
			trace->first_index = index;
			trace->first_command = command;
			trace->first_descriptor = descriptor;
			trace->first_source = source;
			trace->first_width = width;
			trace->first_height = height;
			trace->first_w_field = w_field;
			trace->first_h_field = h_field;
		}
		if (trace->printed_commands < GPU_SCALE_TRACE_PRINT_LIMIT)
		{
			printf("gpu_scale frame=%u index=%u command=%016" PRIX64
				" descriptor=%08" PRIX32 " desc_index=%" PRIu32
				" data_index=%" PRIu32 " source=%08" PRIX32
				" source_size=%ux%u w_field=%02X h_field=%02X\n",
				frame, index, command, descriptor, descriptor_index,
				data_index, source, width, height, w_field, h_field);
			trace->printed_commands++;
		}
	}
	if (frame_nondefault)
		trace->frames_with_nondefault++;
}

static void print_gpu_scale_trace_summary(const gpu_scale_trace *trace)
{
	printf("gpu_scale_summary scanned=%" PRIu64 " nondefault=%" PRIu64
		" frames=%u printed=%u suppressed=%" PRIu64 "\n",
		trace->scanned_commands, trace->nondefault_commands,
		trace->frames_with_nondefault, trace->printed_commands,
		trace->nondefault_commands - trace->printed_commands);
	if (trace->first_valid)
		printf("gpu_scale_first frame=%u index=%u command=%016" PRIX64
			" descriptor=%08" PRIX32 " source=%08" PRIX32
			" source_size=%ux%u w_field=%02X h_field=%02X\n",
			trace->first_frame, trace->first_index, trace->first_command,
			trace->first_descriptor, trace->first_source,
			trace->first_width, trace->first_height,
			trace->first_w_field, trace->first_h_field);
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

static int save_internal_bmp(const char *path, const xavix2_machine_t *machine)
{
	uint8_t header[54] = { 0 };
	const unsigned width = 0x800;
	const unsigned height = 0x400;
	FILE *file;
	unsigned y;
	file = fopen(path, "wb");
	if (!file || !machine)
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
		if (fwrite(machine->screen_data + y * width, 4, width, file) != width)
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

static int load_runtime_state(const char *path, xavix2_machine_t *machine)
{
	FILE *file;
	uint8_t *data;
	long file_size;
	uint32_t payload_size;
	int loaded;

	if (!path || !machine)
		return 0;
	file = fopen(path, "rb");
	if (!file || fseek(file, 0, SEEK_END))
	{
		if (file) fclose(file);
		return 0;
	}
	file_size = ftell(file);
	if (file_size < 40 || fseek(file, 0, SEEK_SET))
	{
		fclose(file);
		return 0;
	}
	data = (uint8_t *)malloc((size_t)file_size);
	if (!data)
	{
		fclose(file);
		return 0;
	}
	if (fread(data, 1, (size_t)file_size, file) != (size_t)file_size)
	{
		fclose(file);
		free(data);
		return 0;
	}
	if (fclose(file))
	{
		free(data);
		return 0;
	}
	payload_size = get_le32(data + 32);
	loaded = !memcmp(data, "DRGQSAVE", 8) &&
		(uint64_t)payload_size + 40 == (uint64_t)file_size &&
		xavix2_machine_state_load(machine, data + 40, payload_size);
	free(data);
	return loaded;
}

static int load_raw_machine_state(const char *path, xavix2_machine_t *machine)
{
	FILE *file;
	uint8_t *data;
	size_t size = xavix2_machine_state_size();
	int loaded;

	file = path ? fopen(path, "rb") : NULL;
	if (!file)
		return 0;
	data = (uint8_t *)malloc(size);
	if (!data)
	{
		fclose(file);
		return 0;
	}
	loaded = fread(data, 1, size, file) == size && fgetc(file) == EOF &&
		xavix2_machine_state_load(machine, data, size);
	free(data);
	return fclose(file) == 0 && loaded;
}

static int save_raw_machine_state(const char *path,
	const xavix2_machine_t *machine)
{
	size_t size = xavix2_machine_state_size();
	size_t written = 0;
	uint8_t *data = (uint8_t *)malloc(size);
	int saved;
	if (!data)
		return 0;
	saved = xavix2_machine_state_save(machine, data, size, &written) &&
		written == size && save_bytes(path, data, size);
	free(data);
	return saved;
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
	const int show_descriptors = getenv("XAVIX2_AUDIO_DESCRIPTORS") != NULL;
	const uint8_t *descriptors = machine->video_ram + 0xf800;
	uint32_t engine_rate = xavix2_audio_engine_rate(machine->mmio[0xa00],
		machine->mmio[0xa05]);
	unsigned channel;

	printf("audio_voices engine_rate=%" PRIu32
		" divider=%u,%u firmware_ram_0150=%" PRIu32 "\n", engine_rate,
		(unsigned)machine->mmio[0xa00], (unsigned)machine->mmio[0xa05],
		get_le32(machine->low_ram + 0x150));
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
		source_rate = (uint32_t)(((uint64_t)pitch * engine_rate + 32768U) >> 16);
		printf("  ch=%u looped=%u start=%08" PRIX32 " loop=%08" PRIX32
			" pos=%08" PRIX32 "+%08" PRIX32 " pitch=%u rate=%" PRIu32
			" volume=%u,%u descriptor14=%02X%02X%02X%02X%02X%02X\n",
			channel, voice->loop, start, loop,
			(uint32_t)(voice->position >> 32), (uint32_t)voice->position,
			pitch, source_rate, voice->volume_left, voice->volume_right,
			descriptor[0x14], descriptor[0x15], descriptor[0x16],
			descriptor[0x17], descriptor[0x18], descriptor[0x19]);
		if (show_descriptors)
		{
			unsigned byte;
			printf("    descriptor=");
			for (byte = 0; byte < XAVIX2_AUDIO_DESCRIPTOR_SIZE; ++byte)
				printf("%02X", descriptor[byte]);
			putchar('\n');
		}
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
	int frame_timing_enabled = 0;
	LARGE_INTEGER frame_timing_frequency;
	double frame_timing_total_ms = 0.0;
	double frame_timing_minimum_ms = DBL_MAX;
	double frame_timing_maximum_ms = 0.0;
	uint32_t frame_timing_buckets[8] = { 0 };
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
	uint32_t epoch_ir_code = 0;
	int epoch_ir_code_valid = 0;
	unsigned epoch_ir_at = 0;
	uint32_t epoch_ir_sequence[MOTION_SEQUENCE_CAPACITY] = { 0 };
	unsigned epoch_ir_sequence_count = 0;
	unsigned epoch_ir_sequence_period = 15;
	unsigned timer_rate_override = 0;
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
	int gpu_scale_trace_enabled = 0;
	int gpu_descriptor_filter = -1;
	int geometry_command_trace_enabled = 0;
	instruction_trace instruction_log = { 0, 0, UINT32_MAX, 0, 0, 0, 0, 256, 0 };
	gpu_scale_trace scale_trace = { 0 };

	if ((argc < 2 || argc > 4) || !MultiByteToWideChar(CP_ACP, 0,
		argv[1], -1, path, (int)(sizeof(path) / sizeof(path[0]))))
	{
		fprintf(stderr, "usage: xavix2-boot-probe <xavix2.zip> [byte-cycles] [frame.bmp]\n");
		return 64;
	}
	if (argc >= 3)
		requested = _strtoui64(argv[2], NULL, 0);
	if (!drgqst_rom_load_zip(path, &image, error, sizeof(error) / sizeof(error[0])))
	{
		fwprintf(stderr, L"%ls\n", error);
		return 2;
	}
	if (!drgqst_rom_is_xavix2(image.kind))
	{
		fprintf(stderr, "the boot probe only accepts a supported XaviX 2 ROM\n");
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
	xavix2_machine_set_motion_packet_address(machine,
		motion_packet_address_for_rom(image.kind));
	xavix2_machine_set_fixed_pio_input(machine,
		fixed_pio_input_for_rom(image.kind));
	if (image.kind == DRGQST_ROM_EPO_DTCJ)
		xavix2_machine_update_takecopter_timer_rate(machine);
	{
		const char *state_path = getenv("XAVIX2_LOAD_STATE");
		const char *raw_state_path = getenv("XAVIX2_LOAD_RAW_STATE");
		if (state_path && !load_runtime_state(state_path, machine))
		{
			fprintf(stderr, "could not load XAVIX2_LOAD_STATE\n");
			free(machine);
			drgqst_rom_release(&image);
			return 2;
		}
		if (state_path)
			printf("state_resume cycles=%" PRIu64 " next_vblank=%" PRIu64
				" frame=%" PRIu64 "\n", machine->cpu.total_cycles,
				machine->next_vblank_cycle, machine->frame_count);
		if (raw_state_path && !load_raw_machine_state(raw_state_path, machine))
		{
			fprintf(stderr, "could not load XAVIX2_LOAD_RAW_STATE\n");
			free(machine);
			drgqst_rom_release(&image);
			return 2;
		}
	}
	{
		const char *timer_rate_text = getenv("XAVIX2_TIMER_RATE");
		if (timer_rate_text)
		{
			timer_rate_override = (unsigned)strtoul(timer_rate_text, NULL, 0);
			if (timer_rate_override != 60 && timer_rate_override != 120)
			{
				fprintf(stderr, "XAVIX2_TIMER_RATE must be 60 or 120\n");
				free(machine);
				drgqst_rom_release(&image);
				return 64;
			}
			xavix2_machine_set_timer_rate(machine, timer_rate_override);
		}
	}
	if (getenv("XAVIX2_HIGH_RESOLUTION_3D"))
		xavix2_machine_set_high_resolution_3d(machine, 1);
	{
		const char *mute_mask = getenv("XAVIX2_AUDIO_MUTE_MASK");
		if (mute_mask)
			xavix2_audio_set_mute_mask(&machine->audio,
				_strtoui64(mute_mask, NULL, 0));
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
		const char *trace_opcode = getenv("XAVIX2_TRACE_OPCODE");
		const char *trace_register5 = getenv("XAVIX2_TRACE_R5");
		const char *sensor_packet_text = getenv("XAVIX2_SENSOR_PACKET");
		const char *sensor_packet_time = getenv("XAVIX2_SENSOR_PACKET_AT");
		const char *motion_packet_text = getenv("XAVIX2_MOTION_PACKET");
		const char *motion_packet_time = getenv("XAVIX2_MOTION_PACKET_AT");
		const char *motion_sequence_text = getenv("XAVIX2_MOTION_SEQUENCE");
		const char *motion_sequence_time = getenv("XAVIX2_MOTION_SEQUENCE_AT");
		const char *motion_sequence_period_text = getenv("XAVIX2_MOTION_SEQUENCE_PERIOD");
		const char *video_frames_text = getenv("XAVIX2_VIDEO_FRAMES");
		const char *frame_timing_text = getenv("XAVIX2_FRAME_TIMING");
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
		const char *epoch_ir_code_text = getenv("XAVIX2_EPOCH_IR_CODE");
		const char *epoch_ir_at_text = getenv("XAVIX2_EPOCH_IR_AT");
		const char *epoch_ir_sequence_text = getenv("XAVIX2_EPOCH_IR_SEQUENCE");
		const char *epoch_ir_sequence_period_text =
			getenv("XAVIX2_EPOCH_IR_SEQUENCE_PERIOD");
		const char *gpu_scale_trace_text = getenv("XAVIX2_GPU_SCALE_TRACE");
		const char *gpu_descriptor_trace_text =
			getenv("XAVIX2_GPU_DESCRIPTOR_TRACE");
		const char *geometry_command_trace_text =
			getenv("XAVIX2_GE_COMMAND_TRACE");
		const char *geometry_register_trace_text =
			getenv("XAVIX2_GE_REGISTER_TRACE");
		const char *gpu_submit_trace_text =
			getenv("XAVIX2_GPU_SUBMIT_TRACE");
		const char *audio_command_trace_text =
			getenv("XAVIX2_AUDIO_COMMAND_TRACE");
		const char *audio_channel_metrics_text =
			getenv("XAVIX2_AUDIO_CHANNEL_METRICS");
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
			/* Runtime states intentionally preserve machine diagnostics.  A new
			 * probe request must report only accesses made after this resume,
			 * otherwise saved trace entries can consume the bounded buffer before
			 * the requested frame is reached. */
			machine->diagnostic_ram_read_count = 0;
			machine->diagnostic_ram_write_count = 0;
			machine->diagnostic_ram_trace_count = 0;
			machine->diagnostic_ram_trace_dropped = 0;
			machine->diagnostic_trace_start_cycle = machine->cpu.total_cycles;
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
		if (trace_opcode)
		{
			instruction_log.opcode_filter = (unsigned)strtoul(trace_opcode, NULL, 0) & 0xff;
			instruction_log.opcode_filter_enabled = 1;
			instruction_trace_enabled = 1;
		}
		if (trace_register5)
		{
			instruction_log.register5_filter =
				(uint32_t)strtoul(trace_register5, NULL, 0);
			instruction_log.register5_filter_enabled = 1;
			instruction_trace_enabled = 1;
		}
		if (video_frames_text)
			video_frames = (unsigned)strtoul(video_frames_text, NULL, 0);
		if (frame_timing_text)
			frame_timing_enabled = atoi(frame_timing_text) != 0;
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
		if (epoch_ir_code_text)
		{
			epoch_ir_code = (uint32_t)strtoul(epoch_ir_code_text, NULL, 0);
			epoch_ir_code_valid = 1;
		}
		if (epoch_ir_at_text)
			epoch_ir_at = (unsigned)strtoul(epoch_ir_at_text, NULL, 0);
		if (epoch_ir_sequence_text && !parse_u32_sequence(
			epoch_ir_sequence_text, epoch_ir_sequence,
			&epoch_ir_sequence_count))
		{
			fprintf(stderr, "XAVIX2_EPOCH_IR_SEQUENCE must contain hexadecimal words separated by commas\n");
			free(machine);
			drgqst_rom_release(&image);
			return 64;
		}
		if (epoch_ir_sequence_period_text)
			epoch_ir_sequence_period = (unsigned)strtoul(
				epoch_ir_sequence_period_text, NULL, 0);
		if (!epoch_ir_sequence_period)
			epoch_ir_sequence_period = 1;
		if (gpu_scale_trace_text)
			gpu_scale_trace_enabled = atoi(gpu_scale_trace_text) != 0;
		if (gpu_descriptor_trace_text)
		{
			gpu_descriptor_filter = (int)strtol(gpu_descriptor_trace_text,
				NULL, 0);
			if (gpu_descriptor_filter >= 0 && gpu_descriptor_filter < 64)
				gpu_scale_trace_enabled = 1;
			else
				gpu_descriptor_filter = -1;
		}
		if (geometry_command_trace_text)
		{
			geometry_command_trace_enabled =
				atoi(geometry_command_trace_text) != 0;
			geometry_detail_trace_enabled =
				atoi(geometry_command_trace_text) > 1;
		}
		if (geometry_register_trace_text)
			geometry_register_trace_enabled =
				atoi(geometry_register_trace_text) != 0;
		if (gpu_submit_trace_text)
			gpu_submit_trace_enabled = atoi(gpu_submit_trace_text);
		if (audio_command_trace_text)
			audio_command_trace_enabled = atoi(audio_command_trace_text) != 0;
		audio_descriptor_trace_enabled =
			getenv("XAVIX2_AUDIO_DESCRIPTORS") != NULL;
		if (audio_channel_metrics_text)
			audio_channel_metrics_enabled =
				atoi(audio_channel_metrics_text) != 0;
		if (geometry_command_trace_enabled || geometry_register_trace_enabled ||
			gpu_submit_trace_enabled || audio_command_trace_enabled ||
			audio_channel_metrics_enabled)
		{
			unsigned command;
			memset(geometry_command_count, 0, sizeof(geometry_command_count));
			memset(gpu_submit_descriptor_count, 0,
				sizeof(gpu_submit_descriptor_count));
			gpu_enemy_submit_count = 0;
			memset(audio_metrics, 0, sizeof(audio_metrics));
			probe_original_write8 = machine->cpu.write8;
			machine->cpu.write8 = trace_probe_write8;
			for (command = 0; command < 256; ++command)
				geometry_command_count[command] = 0;
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
		int skip_render = getenv("XAVIX2_SKIP_RENDER") != NULL;
		xavix2_machine_set_skip_render(machine, skip_render);
		if (frame_timing_enabled &&
			!QueryPerformanceFrequency(&frame_timing_frequency))
			frame_timing_enabled = 0;
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
			LARGE_INTEGER frame_start;
			LARGE_INTEGER frame_end;
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
			if (motion_sequence_count)
				frame_packet = motion_sequence[(frame /
					(unsigned)motion_sequence_period) % motion_sequence_count];
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
			probe_video_frame = frame + 1;
			if (image.kind == DRGQST_ROM_EPO_DTCJ && !timer_rate_override)
				xavix2_machine_update_takecopter_timer_rate(machine);
			if ((epoch_ir_code_valid || epoch_ir_sequence_count) &&
				frame >= epoch_ir_at && !xavix2_machine_transmit_epoch_ir(
					machine, epoch_ir_sequence_count ? epoch_ir_sequence[
						(frame - epoch_ir_at) / epoch_ir_sequence_period <
							epoch_ir_sequence_count ?
						(frame - epoch_ir_at) / epoch_ir_sequence_period :
							epoch_ir_sequence_count - 1] : epoch_ir_code))
				fprintf(stderr, "epoch IR transmission skipped at frame %u\n",
					frame + 1);
			if (frame_timing_enabled)
				QueryPerformanceCounter(&frame_start);
			(void)xavix2_machine_run_video_frame(machine,
				rom_uses_motion_packet(image.kind) ? frame_packet : NULL,
				frame_input);
			if (frame_timing_enabled)
			{
				double milliseconds;
				unsigned bucket;
				unsigned timing_width;
				unsigned timing_height;
				unsigned timing_stride;
				static const double limit[7] =
					{ 8.0, 12.0, 16.667, 25.0, 33.334, 50.0, 100.0 };
				/* Match the GUI path: enhanced presentation is generated once
				 * after every emulated frame, not only for the final capture. */
				(void)xavix2_machine_visible_frame(machine, &timing_width,
					&timing_height, &timing_stride);
				QueryPerformanceCounter(&frame_end);
				milliseconds = (double)(frame_end.QuadPart -
					frame_start.QuadPart) * 1000.0 /
					(double)frame_timing_frequency.QuadPart;
				frame_timing_total_ms += milliseconds;
				if (milliseconds < frame_timing_minimum_ms)
					frame_timing_minimum_ms = milliseconds;
				if (milliseconds > frame_timing_maximum_ms)
					frame_timing_maximum_ms = milliseconds;
				for (bucket = 0; bucket < 7 &&
					milliseconds >= limit[bucket]; ++bucket)
					;
				frame_timing_buckets[bucket]++;
			}
			if (audio_channel_metrics_enabled)
				for (unsigned channel = 0; channel < XAVIX2_AUDIO_VOICES;
					++channel)
					if (machine->audio.voice[channel].active)
						audio_metrics[channel].active_frames++;
			if (gpu_scale_trace_enabled)
				trace_gpu_scale_fields(machine, frame + 1,
					gpu_descriptor_filter, &scale_trace);
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
					" hash=%016" PRIX64 " hit=%08" PRIX32
					" gpu=%04X/%u,%04X/%u audio=",
					frame + 1, machine->frame_count,
					machine->cpu.total_cycles,
					machine->cpu.total_instructions,
					machine->cpu.interrupt_count, machine->cpu.pc,
					frame_hash(machine), get_le32(machine->low_ram + 0x6468),
					get_le16(machine->mmio + 0x400),
					get_le16(machine->mmio + 0x404),
					get_le16(machine->mmio + 0x40c),
					get_le16(machine->mmio + 0x410));
				for (audio_byte = 0;
					audio_byte < XAVIX2_AUDIO_VOICES / 8; ++audio_byte)
					printf("%02X", xavix2_audio_status(&machine->audio,
						audio_byte));
				putchar('\n');
			}
		}
		if (frame_timing_enabled)
			printf("frame_timing frames=%u avg=%.3fms min=%.3fms max=%.3fms buckets_lt_8_12_16_25_33_50_100_ge="
				"%u,%u,%u,%u,%u,%u,%u,%u\n",
				video_frames, frame_timing_total_ms / video_frames,
				frame_timing_minimum_ms, frame_timing_maximum_ms,
				frame_timing_buckets[0], frame_timing_buckets[1],
				frame_timing_buckets[2], frame_timing_buckets[3],
				frame_timing_buckets[4], frame_timing_buckets[5],
				frame_timing_buckets[6], frame_timing_buckets[7]);
		xavix2_machine_set_skip_render(machine, 0);
		if (gpu_scale_trace_enabled)
			print_gpu_scale_trace_summary(&scale_trace);
		if (geometry_command_trace_enabled)
		{
			unsigned command;
			printf("geometry_commands");
			for (command = 0; command < 256; ++command)
				if (geometry_command_count[command])
					printf(" %02X=%" PRIu64, command,
						geometry_command_count[command]);
			putchar('\n');
		}
		if (gpu_submit_trace_enabled)
		{
			unsigned descriptor;
			printf("gpu_submit_descriptors");
			for (descriptor = 0; descriptor < 64; ++descriptor)
				if (gpu_submit_descriptor_count[descriptor])
					printf(" %u=%" PRIu64, descriptor,
						gpu_submit_descriptor_count[descriptor]);
			putchar('\n');
			printf("gpu_enemy_submissions=%" PRIu64 "\n",
				gpu_enemy_submit_count);
		}
		if (audio_channel_metrics_enabled)
		{
			uint32_t engine_rate = xavix2_audio_engine_rate(machine->mmio[0xa00],
				machine->mmio[0xa05]);
			unsigned channel;
			printf("audio_channel_metrics engine_rate=%" PRIu32 "\n", engine_rate);
			for (channel = 0; channel < XAVIX2_AUDIO_VOICES; ++channel)
			{
				const audio_channel_metrics *metrics = &audio_metrics[channel];
				if (!metrics->seen && !metrics->active_frames)
					continue;
				printf("audio_channel ch=%u key_on=%" PRIu32
					" release=%" PRIu32 " slide=%" PRIu32 " stop=%" PRIu32
					" active_frames=%" PRIu32 " events=%" PRIu32 "-%" PRIu32,
					channel, metrics->key_ons, metrics->releases, metrics->slides,
					metrics->stops, metrics->active_frames, metrics->first_frame,
					metrics->last_frame);
				if (metrics->seen && metrics->minimum_pitch != UINT16_MAX)
					printf(" pitch=%u-%u rate=%" PRIu32 "-%" PRIu32
						" volume=%u-%u", metrics->minimum_pitch,
						metrics->maximum_pitch,
						(uint32_t)(((uint64_t)metrics->minimum_pitch * engine_rate) >> 16),
						(uint32_t)(((uint64_t)metrics->maximum_pitch * engine_rate) >> 16),
						metrics->minimum_volume, metrics->maximum_volume);
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
			memcpy(machine->low_ram + machine->motion_packet_address,
				motion_packet, sizeof(motion_packet));
			motion_packet_pending = 0;
		}
		if (motion_sequence_index < motion_sequence_count &&
			completed >= motion_sequence_at)
		{
			memcpy(machine->low_ram + machine->motion_packet_address,
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

	printf("game=%s requested=%" PRIu64 " executed=%" PRIu64
		" input=%08" PRIX32 "\n", drgqst_rom_short_name(image.kind),
		requested, completed, machine->pio_input);
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
	printf("gpu_lists=%04X/%u,%04X/%u tables=%04X/%04X crop=%04X/%04X\n",
		get_le16(machine->mmio + 0x400), get_le16(machine->mmio + 0x404),
		get_le16(machine->mmio + 0x40c), get_le16(machine->mmio + 0x410),
		get_le16(machine->mmio + 0x608), get_le16(machine->mmio + 0x622),
		get_le16(machine->mmio + 0x656), get_le16(machine->mmio + 0x658));
	printf("gpu_tables=%04X/%04X auxiliary=%04X/%04X work=%04X/%04X mode=%04X\n",
		get_le16(machine->mmio + 0x608), get_le16(machine->mmio + 0x622),
		get_le16(machine->mmio + 0x624), get_le16(machine->mmio + 0x626),
		get_le16(machine->mmio + 0x628), get_le16(machine->mmio + 0x62a),
		get_le16(machine->mmio + 0x650));
	if (geometry_detail_trace_enabled)
	{
		unsigned register_offset;
		printf("gpu_registers_e600");
		for (register_offset = 0x600; register_offset < 0x640;
			register_offset += 2)
			printf(" %03X=%04X", register_offset,
				get_le16(machine->mmio + register_offset));
		printf("\n");
	}
	{
		size_t eeprom_non_ff = 0;
		size_t eeprom_upper_non_ff = 0;
		size_t eeprom_index;
		for (eeprom_index = 0; eeprom_index < sizeof(machine->eeprom.data);
			eeprom_index++)
		{
			if (machine->eeprom.data[eeprom_index] != 0xff)
			{
				eeprom_non_ff++;
				if (eeprom_index >= 0x100)
					eeprom_upper_non_ff++;
			}
		}
		printf("eeprom_non_ff=%zu/%zu upper_0100=%zu\n", eeprom_non_ff,
			sizeof(machine->eeprom.data), eeprom_upper_non_ff);
	}
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
		const char *mmio_dump = getenv("XAVIX2_MMIO_DUMP");
		const char *palette_dump = getenv("XAVIX2_PALETTE_DUMP");
		const char *raw_state_dump = getenv("XAVIX2_SAVE_RAW_STATE");
		if (ram_dump)
			printf("ram_dump=%s %s\n", ram_dump,
				save_bytes(ram_dump, machine->low_ram, sizeof(machine->low_ram)) ?
				"saved" : "failed");
		if (mmio_dump)
			printf("mmio_dump=%s %s\n", mmio_dump,
				save_bytes(mmio_dump, machine->mmio, sizeof(machine->mmio)) ?
				"saved" : "failed");
		if (palette_dump)
			printf("palette_dump=%s %s\n", palette_dump,
				save_bytes(palette_dump, machine->palette_ram,
					sizeof(machine->palette_ram)) ? "saved" : "failed");
		if (raw_state_dump)
			printf("raw_state_dump=%s %s\n", raw_state_dump,
				save_raw_machine_state(raw_state_dump, machine) ?
				"saved" : "failed");
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
	{
		const char *internal_frame = getenv("XAVIX2_INTERNAL_FRAME");
		if (internal_frame)
			printf("internal_frame=%s %s\n", internal_frame,
				save_internal_bmp(internal_frame, machine) ? "saved" : "FAILED");
	}
	if (argc == 4)
		printf("frame=%s %s\n", argv[3],
			save_bmp(argv[3], machine) ? "saved" : "FAILED");

	free(audio_wav_samples);
	free(machine);
	drgqst_rom_release(&image);
	return 0;
}
