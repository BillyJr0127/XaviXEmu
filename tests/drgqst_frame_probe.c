// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "core/drgqst_core.h"
#include "core/drgqst_state.h"
#include "persistence.h"
#include "rom_loader.h"

#include <windows.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t framebuffer_hash(const uint32_t *pixels)
{
	uint64_t hash = UINT64_C(1469598103934665603);
	unsigned i;
	for (i = 0; i < XAVIX_VIDEO_PIXELS; ++i)
	{
		uint32_t value = pixels[i];
		unsigned byte;
		for (byte = 0; byte < 4; ++byte)
		{
			hash ^= (uint8_t)value;
			hash *= UINT64_C(1099511628211);
			value >>= 8;
		}
	}
	return hash;
}

static uint8_t calibration_triangle(unsigned frame, unsigned start)
{
	unsigned phase = (frame - start) % 30;
	return (uint8_t)((phase < 15 ? phase : 30 - phase) * 17);
}

static int uses_glove_sensor(enum drgqst_rom_kind kind)
{
	return kind == DRGQST_ROM_BAN_ONEP || kind == DRGQST_ROM_BAN_OMT ||
		kind == DRGQST_ROM_TTV_LOTR || kind == DRGQST_ROM_TTV_SW ||
		kind == DRGQST_ROM_TTV_SWJ;
}

static void apply_calibration_sequence(drgqst_core *core, unsigned frame)
{
	uint8_t x = 0x80;
	uint8_t y = 0x00;
	if (frame >= 2130 && frame < 2190)
		y = calibration_triangle(frame, 2130);
	else if (frame >= 2190 && frame < 2220)
		y = 0x80;
	else if (frame >= 2220 && frame < 2880)
	{
		x = calibration_triangle(frame, 2220);
		y = 0x80;
	}
	else if (frame >= 3120 && frame < 3180)
		y = calibration_triangle(frame, 3120);
	else if (frame >= 2880)
		y = 0x80;
	drgqst_core_set_mouse(core, x, y, 0, 0);
}

static int write_bmp(const char *path, const uint32_t *pixels)
{
	FILE *file = fopen(path, "wb");
	BITMAPFILEHEADER file_header;
	BITMAPINFOHEADER info_header;
	int y;
	if (!file)
		return 0;
	memset(&file_header, 0, sizeof(file_header));
	memset(&info_header, 0, sizeof(info_header));
	file_header.bfType = 0x4d42;
	file_header.bfOffBits = sizeof(file_header) + sizeof(info_header);
	file_header.bfSize = file_header.bfOffBits + XAVIX_VIDEO_PIXELS * 4;
	info_header.biSize = sizeof(info_header);
	info_header.biWidth = XAVIX_VIDEO_WIDTH;
	info_header.biHeight = XAVIX_VIDEO_HEIGHT;
	info_header.biPlanes = 1;
	info_header.biBitCount = 32;
	info_header.biCompression = BI_RGB;
	if (fwrite(&file_header, 1, sizeof(file_header), file) != sizeof(file_header) ||
		fwrite(&info_header, 1, sizeof(info_header), file) != sizeof(info_header))
	{
		fclose(file);
		return 0;
	}
	for (y = XAVIX_VIDEO_HEIGHT - 1; y >= 0; --y)
	{
		if (fwrite(pixels + y * XAVIX_VIDEO_WIDTH, 4, XAVIX_VIDEO_WIDTH, file) != XAVIX_VIDEO_WIDTH)
		{
			fclose(file);
			return 0;
		}
	}
	return fclose(file) == 0;
}

static void write_ram_snapshot(const char *bmp_path, const uint8_t *ram)
{
	char path[MAX_PATH];
	FILE *file;
	if (!bmp_path || strlen(bmp_path) + 5 >= sizeof(path))
		return;
	snprintf(path, sizeof(path), "%s.ram", bmp_path);
	file = fopen(path, "wb");
	if (!file)
		return;
	fwrite(ram, 1, XAVIX_MAIN_RAM_SIZE, file);
	fclose(file);
}

int main(int argc, char **argv)
{
	static const uint8_t ban_onep_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
	{
		0xdb, 0x85, 0xf6, 0xcc, 0x48, 0xd7, 0x7c, 0x5a, 0x49, 0x67,
		0xb9, 0xb8, 0xe2, 0x99, 0x91, 0x67, 0xe3, 0xdf, 0xc8, 0xc8
	};
	drgqst_rom_image image = { 0 };
	drgqst_core *core;
	wchar_t path[32768];
	wchar_t error[384];
	unsigned frames;
	unsigned frame;
	unsigned input_bit = 0;
	unsigned punch_mask = 0;
	unsigned sync_period = 0;
	unsigned trajectory = 0;
	unsigned trajectory_start = 10;
	unsigned load_state = 1;
	unsigned fixed_x = UINT_MAX;
	unsigned fixed_y = UINT_MAX;
	unsigned input_pulses = 1;
	unsigned guard_hold = 0;
	unsigned trajectory_interval = 0;
	unsigned last_cursor_visible = UINT_MAX;
	int state_loaded = 0;
	int durable_eeprom_loaded = 0;
	uint8_t initial_eeprom[DRGQST_PERSISTENCE_EEPROM_SIZE];
	const uint32_t *pixels = NULL;

	if (argc < 3 || !MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path,
		(int)(sizeof(path) / sizeof(path[0]))))
	{
		fprintf(stderr, "usage: drgqst-frame-probe <rom.zip> <frames> [output.bmp] [save-dir] [input-bit] [punch-mask] [sync-period] [trajectory] [trajectory-start] [load-state] [fixed-x] [fixed-y] [input-pulses] [guard-hold] [trajectory-interval]\n");
		return 2;
	}
	frames = (unsigned)strtoul(argv[2], NULL, 0);
	if (!frames || !drgqst_rom_load_zip(path, &image, error, sizeof(error) / sizeof(error[0])))
	{
		fwprintf(stderr, L"ROM/argument error: %ls\n", error);
		return 2;
	}
	core = (drgqst_core *)calloc(1, sizeof(*core));
	if (!core || !drgqst_core_init_profile(core, image.data, image.size,
		image.kind == DRGQST_ROM_BAN_ONEP ? DRGQST_CORE_BAN_ONEP :
		image.kind == DRGQST_ROM_BAN_OMT ? DRGQST_CORE_BAN_OMT :
		image.kind == DRGQST_ROM_TTV_LOTR ? DRGQST_CORE_TTV_CU5501_24C02 :
		(image.kind == DRGQST_ROM_TTV_SW ||
		 image.kind == DRGQST_ROM_TTV_SWJ) ? DRGQST_CORE_TTV_CU5501A_24C02 :
		DRGQST_CORE_DRAGON_QUEST))
	{
		free(core);
		drgqst_rom_release(&image);
		return 2;
	}
	if (argc >= 5)
	{
		wchar_t save_directory[32768];
		uint8_t *state = NULL;
		uint8_t durable_eeprom[DRGQST_PERSISTENCE_EEPROM_SIZE];
		size_t state_size = drgqst_state_serialized_size();
		size_t loaded_size = 0;
		if (!MultiByteToWideChar(CP_ACP, 0, argv[4], -1, save_directory,
			(int)(sizeof(save_directory) / sizeof(save_directory[0]))))
			return 2;
		xavix_eeprom24c08_copy_image(&core->machine.state.peripherals.eeprom,
			durable_eeprom);
		if (argc >= 11)
			load_state = !!strtoul(argv[10], NULL, 0);
		if (argc >= 12)
			fixed_x = (unsigned)strtoul(argv[11], NULL, 0) & 0xff;
		if (argc >= 13)
			fixed_y = (unsigned)strtoul(argv[12], NULL, 0) & 0xff;
		if (argc >= 14)
			input_pulses = (unsigned)strtoul(argv[13], NULL, 0);
		if (argc >= 15)
			guard_hold = (unsigned)strtoul(argv[14], NULL, 0);
		if (argc >= 16)
			trajectory_interval = (unsigned)strtoul(argv[15], NULL, 0);
		loaded_size = 0;
		if (drgqst_persistence_load(save_directory,
			DRGQST_PERSISTENCE_BAN_ONEP_EEPROM, ban_onep_sha1,
			durable_eeprom, sizeof(durable_eeprom), &loaded_size, error,
			sizeof(error) / sizeof(error[0])) && loaded_size == sizeof(durable_eeprom))
		{
			xavix_eeprom24c08_load_image(&core->machine.state.peripherals.eeprom,
				durable_eeprom, sizeof(durable_eeprom));
			durable_eeprom_loaded = 1;
		}
		if (load_state)
		{
			state = (uint8_t *)malloc(state_size);
			loaded_size = 0;
			if (!state || !drgqst_persistence_load(save_directory,
				DRGQST_PERSISTENCE_BAN_ONEP_RUNTIME_STATE, ban_onep_sha1,
				state, state_size, &loaded_size, error,
				sizeof(error) / sizeof(error[0])) || loaded_size != state_size ||
				!drgqst_state_load(core, state, loaded_size))
			{
				fwprintf(stderr, L"state load error: %ls\n", error);
				free(state);
				free(core);
				drgqst_rom_release(&image);
				return 2;
			}
			/* Match the GUI: F7 restores the machine checkpoint but never
			 * rewinds the game's durable EEPROM contents. */
			memcpy(core->machine.state.peripherals.eeprom.data,
				durable_eeprom, sizeof(durable_eeprom));
			core->machine.state.peripherals.eeprom.dirty = 0;
			core->machine.state.peripherals.eeprom.write_generation = 0;
			state_loaded = 1;
		}
		free(state);
		if (argc >= 6)
			input_bit = (unsigned)strtoul(argv[5], NULL, 0) & 0xff;
		if (argc >= 7)
			punch_mask = (unsigned)strtoul(argv[6], NULL, 0) & 3;
		if (argc >= 8)
			sync_period = (unsigned)strtoul(argv[7], NULL, 0);
		if (sync_period > 255)
			sync_period = 255;
		if (sync_period)
			core->ban_onep_sync_period = (uint8_t)sync_period;
		if (argc >= 9)
			trajectory = (unsigned)strtoul(argv[8], NULL, 0) & 0xff;
		if (argc >= 10)
			trajectory_start = (unsigned)strtoul(argv[9], NULL, 0);
	}
	xavix_eeprom24c08_copy_image(&core->machine.state.peripherals.eeprom,
		initial_eeprom);
	for (frame = 1; frame <= frames && !core->cpu.stopped; ++frame)
	{
		if (!uses_glove_sensor(image.kind))
			apply_calibration_sequence(core, frame - 1);
		if (uses_glove_sensor(image.kind))
		{
			int left_pressed = (frame >= 1500 && frame < 1504) ||
				(frame >= 2200 && frame < 2204) ||
				(frame >= 3650 && ((frame - 3650) % 300) < 4);
			int right_pressed = 0;
			uint8_t x = core->machine.state.peripherals.sensor.host_x;
			uint8_t y = core->machine.state.peripherals.sensor.host_y;
			if (fixed_x != UINT_MAX)
				x = (uint8_t)fixed_x;
			if (fixed_y != UINT_MAX)
				y = (uint8_t)fixed_y;
			if (!state_loaded && !trajectory && frame >= trajectory_start &&
				frame < trajectory_start + 4)
			{
				left_pressed = !!(punch_mask & 1);
				right_pressed = !!(punch_mask & 2);
			}
			if (trajectory == 9 && frame >= trajectory_start &&
				((frame - trajectory_start) %
					(trajectory_interval ? trajectory_interval : UINT_MAX)) == 0)
			{
				drgqst_core_trigger_bazooka(core);
				left_pressed = 0;
				right_pressed = 0;
			}
			if (trajectory >= 1 && trajectory <= 8 &&
				frame >= trajectory_start &&
				((frame - trajectory_start) %
					(trajectory_interval ? trajectory_interval : UINT_MAX)) < 12)
			{
				const unsigned step = (frame - trajectory_start) %
					(trajectory_interval ? trajectory_interval : UINT_MAX);
				const unsigned direction = ((trajectory - 1) & 7) + 1;
				const uint8_t low = 0x20;
				const uint8_t high = 0xdf;
				const uint8_t forward = (uint8_t)(low +
					((unsigned)(high - low) * step + 5) / 11);
				const uint8_t reverse = (uint8_t)(high - (forward - low));
				left_pressed = !!(punch_mask & 1);
				right_pressed = !!(punch_mask & 2);
				switch (direction)
				{
				case 1: x = forward; y = 0x80; break;
				case 2: x = reverse; y = 0x80; break;
				case 3: x = 0x80; y = forward; break;
				case 4: x = 0x80; y = reverse; break;
				case 5: x = forward; y = forward; break;
				case 6: x = reverse; y = reverse; break;
				case 7: x = reverse; y = forward; break;
				case 8: x = forward; y = reverse; break;
				default: break;
				}
			}
			if (trajectory == 10 && frame >= trajectory_start &&
				((frame - trajectory_start) %
					(trajectory_interval ? trajectory_interval : UINT_MAX)) < 12)
			{
				const unsigned step = (frame - trajectory_start) %
					(trajectory_interval ? trajectory_interval : UINT_MAX);
				x = (step & 1) ? 0xff : 0x00;
				y = 0x80;
				left_pressed = 0;
				right_pressed = 0;
			}
			if (trajectory == 11 && frame >= trajectory_start &&
				((frame - trajectory_start) %
					(trajectory_interval ? trajectory_interval : UINT_MAX)) < 12)
			{
				const unsigned step = (frame - trajectory_start) %
					(trajectory_interval ? trajectory_interval : UINT_MAX);
				x = (step & 1) ? 0xdf : 0x20;
				y = 0x80;
				left_pressed = !(step & 1);
				right_pressed = !!(step & 1);
			}
			if (frame >= 6200)
				x = calibration_triangle(frame, 6200);
			if (state_loaded && frame >=
				(trajectory_start == 10 ? 360 : trajectory_start) && frame <
				(trajectory_start == 10 ? 364 : trajectory_start + 4))
			{
				left_pressed = !!(punch_mask & 1);
				right_pressed = !!(punch_mask & 2);
			}
			drgqst_core_set_mouse(core, x, y, left_pressed, right_pressed);
			if (state_loaded && guard_hold && frame >= trajectory_start &&
				frame < trajectory_start + guard_hold)
			{
				core->ban_onep_left_punch = 10;
				core->ban_onep_right_punch = 10;
				core->machine.state.input0 &= (uint8_t)~0x03;
			}
			/* Stationary punch diagnostics exercise the optical image only; a
			 * trajectory retains its real button bit to match the GUI path. */
			if (state_loaded && frame >= 350 && !trajectory)
				core->machine.state.input0 &= (uint8_t)~0x03;
			if (!state_loaded && trajectory && frame >= trajectory_start &&
				((frame - trajectory_start) %
					(trajectory_interval ? trajectory_interval : UINT_MAX)) < 12)
				core->machine.state.input0 &= (uint8_t)~0x03;
			if (state_loaded && trajectory && !input_pulses &&
				frame >= trajectory_start && ((frame - trajectory_start) %
					(trajectory_interval ? trajectory_interval : UINT_MAX)) < 12)
				core->machine.state.input0 &= (uint8_t)~0x03;
			if (input_bit)
			{
				const unsigned input_start = state_loaded && trajectory_start == 10 ?
					30 : trajectory_start;
				const unsigned offset = frame >= input_start ?
					frame - input_start : UINT_MAX;
				if (guard_hold && frame >= input_start &&
					frame < input_start + guard_hold)
					core->machine.state.input0 |= (uint8_t)input_bit;
				else if (offset != UINT_MAX && offset / 40 < input_pulses &&
					offset % 40 < 4)
					core->machine.state.input0 |= (uint8_t)input_bit;
				else
					core->machine.state.input0 &= (uint8_t)~input_bit;
			}
		}
		pixels = drgqst_core_run_frame(core);
		{
			const unsigned cursor_visible =
				drgqst_core_internal_cursor_visible(core);
			if (cursor_visible != last_cursor_visible)
			{
				printf("cursor-transition frame=%u visible=%u\n",
					frame, cursor_visible);
				last_cursor_visible = cursor_visible;
			}
		}
		if (frame <= 10 || frame % 60 == 0 || frame == frames)
		{
			const xavix_video_frame_report *report = xavix_video_last_report(&core->video);
			printf("frame=%u pc=%06lX cycles=%llu irq=%02X vc=%02X hash=%016llX opaque=%lu reads=%lu edge=%02X/%02X old=%02X/%02X cursor=%u\n",
				frame, (unsigned long)xavix_cpu_linear_pc(&core->cpu),
				(unsigned long long)core->cpu.total_cycles, core->machine.state.irq_source,
				core->machine.state.video_control,
				(unsigned long long)framebuffer_hash(pixels),
				(unsigned long)report->opaque_pixels_drawn,
				(unsigned long)report->memory_reads,
				core->machine.state.main_ram[0xcb], core->machine.state.main_ram[0xcc],
				core->machine.state.main_ram[0xcd], core->machine.state.main_ram[0xce],
				drgqst_core_internal_cursor_visible(core));
		}
	}
	if (argc >= 4 && pixels && !write_bmp(argv[3], pixels))
		fprintf(stderr, "could not write %s\n", argv[3]);
	if (argc >= 4)
		write_ram_snapshot(argv[3], core->machine.state.main_ram);
	printf("done frames=%u stopped=%u pc=%06lX ram75=%02X ram76=%02X cursor=%u\n",
		frame - 1, core->cpu.stopped, (unsigned long)xavix_cpu_linear_pc(&core->cpu),
		core->machine.state.main_ram[0x75], core->machine.state.main_ram[0x76],
		drgqst_core_internal_cursor_visible(core));
	printf("io=%02X/%02X dir=%02X/%02X adc=%02X/%02X sensor=%u/%u/%u/%u host=%u/%u scan=%u/%u eeprom=%u/%lu\n",
		core->machine.state.io_data[0], core->machine.state.io_data[1],
		core->machine.state.io_direction[0], core->machine.state.io_direction[1],
		core->machine.state.adc_control, core->machine.state.adc_latch,
		(unsigned)core->machine.state.peripherals.sensor.pixel,
		(unsigned)core->machine.state.peripherals.sensor.illuminated,
		(unsigned)core->machine.state.peripherals.sensor.sync_phase,
		(unsigned)core->machine.state.peripherals.sensor.scan_mode,
		(unsigned)core->machine.state.peripherals.sensor.host_x,
		(unsigned)core->machine.state.peripherals.sensor.host_y,
		(unsigned)core->machine.state.peripherals.sensor.scan_x,
		(unsigned)core->machine.state.peripherals.sensor.scan_y,
		(unsigned)core->machine.state.peripherals.eeprom.dirty,
		(unsigned long)core->machine.state.peripherals.eeprom.write_generation);
	if (state_loaded)
	{
		printf("detect bf=%02X c0=%02X 0840=%02X 0841=%02X\n",
			core->machine.state.main_ram[0x00bf],
			core->machine.state.main_ram[0x00c0],
			core->machine.state.main_ram[0x0840],
			core->machine.state.main_ram[0x0841]);
		printf("objects x=%02X/%02X/%02X/%02X y=%02X/%02X/%02X/%02X\n",
			core->machine.state.main_ram[0x0842],
			core->machine.state.main_ram[0x0843],
			core->machine.state.main_ram[0x0844],
			core->machine.state.main_ram[0x0845],
			core->machine.state.main_ram[0x0846],
			core->machine.state.main_ram[0x0847],
			core->machine.state.main_ram[0x0848],
			core->machine.state.main_ram[0x0849]);
	}
	{
		unsigned changed = 0;
		unsigned address;
		for (address = 0; address < DRGQST_PERSISTENCE_EEPROM_SIZE; ++address)
		{
			const uint8_t value = core->machine.state.peripherals.eeprom.data[address];
			if (value != initial_eeprom[address])
			{
				if (changed < 64)
					printf("eeprom-change %03X %02X->%02X\n", address,
						initial_eeprom[address], value);
				++changed;
			}
		}
		printf("eeprom-diff=%u durable-loaded=%u\n", changed,
			(unsigned)durable_eeprom_loaded);
	}
	free(core);
	drgqst_rom_release(&image);
	return 0;
}
