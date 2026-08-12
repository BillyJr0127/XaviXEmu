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

typedef struct io_probe_counter
{
	uint16_t address;
	uint64_t reads;
	uint64_t nonzero;
	uint32_t last_pc;
	uint32_t last_nonzero_pc;
	uint8_t last_value;
	uint8_t last_nonzero_value;
} io_probe_counter;

typedef struct io_probe
{
	xavix_cpu_read8_fn original_read;
	xavix_cpu_write8_fn original_write;
	void *original_opaque;
	drgqst_core *core;
	unsigned input_read_logs;
	unsigned external_read_logs;
	unsigned tvpc_key_read_logs;
	int trace_tvpc_keyboard;
	io_probe_counter counters[24];
	io_probe_counter writes[18];
} io_probe;

static void probe_cpu_write(void *opaque, xavix_cpu_bus_t bus,
	uint32_t address, uint8_t data)
{
	io_probe *probe = (io_probe *)opaque;
	if (bus == XAVIX_CPU_BUS_LOW)
	{
		unsigned index;
		for (index = 0; index < sizeof(probe->writes) /
			sizeof(probe->writes[0]); ++index)
		{
			if (probe->writes[index].address == address)
			{
				probe->writes[index].reads++;
				probe->writes[index].last_pc =
					xavix_cpu_linear_pc(&probe->core->cpu);
				probe->writes[index].last_value = data;
				if (data)
				{
					probe->writes[index].nonzero++;
					probe->writes[index].last_nonzero_pc =
						xavix_cpu_linear_pc(&probe->core->cpu);
					probe->writes[index].last_nonzero_value = data;
				}
				break;
			}
		}
	}
	probe->original_write(probe->original_opaque, bus, address, data);
}

static uint8_t probe_cpu_read(void *opaque, xavix_cpu_bus_t bus,
	uint32_t address)
{
	io_probe *probe = (io_probe *)opaque;
	uint8_t value = probe->original_read(probe->original_opaque, bus, address);
	if (probe->trace_tvpc_keyboard && bus == XAVIX_CPU_BUS_EXTERNAL &&
		xavix_cpu_linear_pc(&probe->core->cpu) >= 0x028000 &&
		xavix_cpu_linear_pc(&probe->core->cpu) < 0x028200 &&
		address != xavix_cpu_linear_pc(&probe->core->cpu) &&
		probe->external_read_logs < 256)
	{
		printf("tvpc-external-read pc=%06lX address=%06lX value=%02X\n",
			(unsigned long)xavix_cpu_linear_pc(&probe->core->cpu),
			(unsigned long)address, value);
		++probe->external_read_logs;
	}
	if (bus == XAVIX_CPU_BUS_LOW)
	{
		unsigned index;
		if (probe->trace_tvpc_keyboard &&
			address >= 0x00ac && address <= 0x00b0 && value &&
			(xavix_cpu_linear_pc(&probe->core->cpu) < 0x028000 ||
			xavix_cpu_linear_pc(&probe->core->cpu) >= 0x028200) &&
			probe->tvpc_key_read_logs < 256)
		{
			printf("tvpc-key-read address=%04lX pc=%06lX value=%02X\n",
				(unsigned long)address,
				(unsigned long)xavix_cpu_linear_pc(&probe->core->cpu), value);
			++probe->tvpc_key_read_logs;
		}
		if ((address == 0x002e || address == 0x0030 ||
			address == 0x00a4 || address == 0x00a5) && value &&
			probe->input_read_logs < 128)
		{
			printf("input-read address=%04lX pc=%06lX value=%02X\n",
				(unsigned long)address,
				(unsigned long)xavix_cpu_linear_pc(&probe->core->cpu), value);
			++probe->input_read_logs;
		}
		for (index = 0; index < sizeof(probe->counters) /
			sizeof(probe->counters[0]); ++index)
		{
			if (probe->counters[index].address == address)
			{
				probe->counters[index].reads++;
				probe->counters[index].last_pc =
					xavix_cpu_linear_pc(&probe->core->cpu);
				probe->counters[index].last_value = value;
				if (value)
				{
					probe->counters[index].nonzero++;
					probe->counters[index].last_nonzero_pc =
						xavix_cpu_linear_pc(&probe->core->cpu);
					probe->counters[index].last_nonzero_value = value;
				}
				break;
			}
		}
	}
	return value;
}

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

static int persistence_profile(enum drgqst_rom_kind rom_kind,
	const uint8_t **sha1, enum drgqst_persistence_kind *eeprom_kind,
	enum drgqst_persistence_kind *state_kind, size_t *eeprom_size)
{
	static const uint8_t ban_onep_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
	{
		0xdb, 0x85, 0xf6, 0xcc, 0x48, 0xd7, 0x7c, 0x5a, 0x49, 0x67,
		0xb9, 0xb8, 0xe2, 0x99, 0x91, 0x67, 0xe3, 0xdf, 0xc8, 0xc8
	};
	static const uint8_t lotr_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
	{
		0x26, 0x4a, 0x9d, 0x43, 0x27, 0xaf, 0x0a, 0x07, 0x58, 0x41,
		0xad, 0x61, 0x29, 0xdb, 0x67, 0xd8, 0x2c, 0xf7, 0x41, 0xf1
	};
	static const uint8_t sw_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
	{
		0x1e, 0xd8, 0xd5, 0x56, 0xf3, 0x1b, 0x41, 0x82, 0x25, 0x9c,
		0xa8, 0xc7, 0x66, 0xd6, 0x0c, 0x82, 0x4d, 0x8d, 0x97, 0x44
	};
	static const uint8_t swj_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
	{
		0x40, 0x6f, 0x0b, 0xcc, 0xb0, 0x1c, 0xd4, 0xa2, 0x6f, 0xe4,
		0xa5, 0x67, 0x5d, 0x7e, 0xbe, 0xcc, 0x78, 0xc5, 0x81, 0x47
	};
	static const uint8_t mx_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
	{
		0x13, 0x7f, 0x97, 0xd7, 0xd8, 0x57, 0x69, 0x7a, 0x13, 0xe0,
		0xc8, 0x98, 0x45, 0x09, 0x99, 0x4d, 0xc7, 0xbc, 0x5f, 0xc5
	};
	static const uint8_t jump_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
	{
		0xbc, 0xa7, 0x53, 0x5b, 0xaa, 0x6a, 0x54, 0xad, 0x3e, 0xe0,
		0x92, 0x9b, 0xd3, 0xb7, 0x4a, 0x22, 0xcb, 0x51, 0x39, 0xda
	};
	static const uint8_t sdb_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
	{
		0x47, 0xa9, 0x68, 0x22, 0xd4, 0xd7, 0xd6, 0xa0, 0xf6, 0xbe,
		0x5c, 0xd7, 0x29, 0xc3, 0x74, 0x7d, 0xba, 0xb6, 0x59, 0x79
	};
	static const uint8_t bowl_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
	{
		0xeb, 0xe3, 0x79, 0x21, 0x72, 0xdc, 0x43, 0x90, 0x4b, 0x92,
		0x26, 0xbe, 0xb2, 0x7f, 0x1d, 0xa8, 0x9d, 0x23, 0x88, 0xcc
	};
	static const uint8_t tak_chq_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
	{
		0xa3, 0x08, 0x84, 0xda, 0x55, 0x54, 0x48, 0x3e, 0xbf, 0xd0,
		0x00, 0x9c, 0xf5, 0xdd, 0x17, 0x68, 0xbe, 0x8a, 0x99, 0xcb
	};
	static const uint8_t hamd_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
	{
		0xc6, 0x1d, 0x43, 0x6d, 0x6b, 0x80, 0x37, 0x17, 0xb8, 0xc8,
		0x4d, 0x20, 0x22, 0x49, 0x93, 0x80, 0xf7, 0x1c, 0xce, 0xd8
	};
	static const uint8_t dor_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
	{
		0x98, 0xfa, 0x86, 0xf8, 0x5e, 0x00, 0xaa, 0x40, 0xe7, 0xa5,
		0x85, 0xff, 0x0b, 0xc9, 0x30, 0xcb, 0x5c, 0xa8, 0x83, 0x62
	};

	*eeprom_size = DRGQST_PERSISTENCE_EEPROM_SIZE;

	switch (rom_kind)
	{
	case DRGQST_ROM_BAN_ONEP:
		*sha1 = ban_onep_sha1;
		*eeprom_kind = DRGQST_PERSISTENCE_BAN_ONEP_EEPROM;
		*state_kind = DRGQST_PERSISTENCE_BAN_ONEP_RUNTIME_STATE;
		return 1;
	case DRGQST_ROM_TTV_LOTR:
		*sha1 = lotr_sha1;
		*eeprom_kind = DRGQST_PERSISTENCE_TTV_LOTR_EEPROM;
		*state_kind = DRGQST_PERSISTENCE_TTV_LOTR_RUNTIME_STATE;
		return 1;
	case DRGQST_ROM_TTV_SW:
		*sha1 = sw_sha1;
		*eeprom_kind = DRGQST_PERSISTENCE_TTV_SW_EEPROM;
		*state_kind = DRGQST_PERSISTENCE_TTV_SW_RUNTIME_STATE;
		return 1;
	case DRGQST_ROM_TTV_SWJ:
		*sha1 = swj_sha1;
		*eeprom_kind = DRGQST_PERSISTENCE_TTV_SWJ_EEPROM;
		*state_kind = DRGQST_PERSISTENCE_TTV_SWJ_RUNTIME_STATE;
		return 1;
	case DRGQST_ROM_TTV_MX:
		*sha1 = mx_sha1;
		*eeprom_kind = DRGQST_PERSISTENCE_TTV_MX_EEPROM;
		*state_kind = DRGQST_PERSISTENCE_TTV_MX_RUNTIME_STATE;
		return 1;
	case DRGQST_ROM_TOM_JUMP:
		*sha1 = jump_sha1;
		*eeprom_kind = DRGQST_PERSISTENCE_TOM_JUMP_EEPROM;
		*state_kind = DRGQST_PERSISTENCE_TOM_JUMP_RUNTIME_STATE;
		return 1;
	case DRGQST_ROM_EPO_SDB:
		*sha1 = sdb_sha1;
		*eeprom_kind = DRGQST_PERSISTENCE_EPO_SDB_NVRAM;
		*state_kind = DRGQST_PERSISTENCE_EPO_SDB_RUNTIME_STATE;
		*eeprom_size = DRGQST_PERSISTENCE_PARALLEL_NVRAM_SIZE;
		return 1;
	case DRGQST_ROM_EPO_BOWL:
		*sha1 = bowl_sha1;
		*eeprom_kind = DRGQST_PERSISTENCE_EPO_BOWL_EEPROM;
		*state_kind = DRGQST_PERSISTENCE_EPO_BOWL_RUNTIME_STATE;
		return 1;
	case DRGQST_ROM_TAK_CHQ:
		*sha1 = tak_chq_sha1;
		*eeprom_kind = DRGQST_PERSISTENCE_TAK_CHQ_EEPROM;
		*state_kind = DRGQST_PERSISTENCE_TAK_CHQ_RUNTIME_STATE;
		return 1;
	case DRGQST_ROM_EPO_HAMD:
		*sha1 = hamd_sha1;
		*eeprom_kind = DRGQST_PERSISTENCE_EEPROM;
		*state_kind = DRGQST_PERSISTENCE_EPO_HAMD_RUNTIME_STATE;
		*eeprom_size = 0;
		return 1;
	case DRGQST_ROM_TVPC_DOR:
		*sha1 = dor_sha1;
		*eeprom_kind = DRGQST_PERSISTENCE_TVPC_DOR_EEPROM;
		*state_kind = DRGQST_PERSISTENCE_TVPC_DOR_RUNTIME_STATE;
		*eeprom_size = DRGQST_PERSISTENCE_EEPROM24C16_SIZE;
		return 1;
	default:
		return 0;
	}
}

int main(int argc, char **argv)
{
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
	unsigned sdb_p1_x = UINT_MAX;
	unsigned sdb_p1_y = UINT_MAX;
	unsigned sdb_p2_x = UINT_MAX;
	unsigned sdb_p2_y = UINT_MAX;
	unsigned last_cursor_visible = UINT_MAX;
	unsigned pcm_peak = 0;
	uint64_t pcm_samples = 0;
	uint64_t pcm_nonzero = 0;
	int state_loaded = 0;
	int durable_eeprom_loaded = 0;
	uint8_t initial_eeprom[DRGQST_PERSISTENCE_PARALLEL_NVRAM_SIZE];
	size_t eeprom_size;
	const uint32_t *pixels = NULL;
	io_probe io_trace =
	{
		NULL, NULL, NULL, NULL, 0, 0, 0, 0,
		{
			{ .address = 0x7a00 }, { .address = 0x7a01 },
			{ .address = 0x7a02 }, { .address = 0x7a03 },
			{ .address = 0x7a80 }, { .address = 0x7a81 },
			{ .address = 0x7b00 }, { .address = 0x7b01 },
			{ .address = 0x7b10 }, { .address = 0x7b11 },
			{ .address = 0x7b80 }, { .address = 0x7b81 },
			{ .address = 0x0037 }, { .address = 0x003e },
			{ .address = 0x003f }, { .address = 0x0040 },
			{ .address = 0x0041 }, { .address = 0x0044 },
			{ .address = 0x0048 }, { .address = 0x00ac },
			{ .address = 0x00ad }, { .address = 0x00ae },
			{ .address = 0x00af }, { .address = 0x00b0 }
		},
		{
			{ .address = 0x00a4 }, { .address = 0x00a5 },
			{ .address = 0x002e }, { .address = 0x0030 },
			{ .address = 0x0037 }, { .address = 0x003e },
			{ .address = 0x003f }, { .address = 0x0040 },
			{ .address = 0x0041 }, { .address = 0x0044 },
			{ .address = 0x0048 }, { .address = 0x00ac },
			{ .address = 0x00ad }, { .address = 0x00ae },
			{ .address = 0x00af }, { .address = 0x00b0 },
			{ .address = 0x009e }, { .address = 0x009f }
		}
	};

	if (argc < 3 || !MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path,
		(int)(sizeof(path) / sizeof(path[0]))))
	{
		fprintf(stderr, "usage: drgqst-frame-probe <rom.zip> <frames> [output.bmp] [save-dir] [input-bit] [punch-mask] [sync-period] [trajectory] [trajectory-start] [load-state] [fixed-x] [fixed-y] [input-pulses] [guard-hold] [trajectory-interval] [sdb-p1-x] [sdb-p1-y] [sdb-p2-x] [sdb-p2-y]\n");
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
		image.kind == DRGQST_ROM_EPO_BOWL ?
			DRGQST_CORE_EPO_BOWL_SENSOR_24C04 :
		(image.kind == DRGQST_ROM_TTV_MX ||
		 image.kind == DRGQST_ROM_TOM_JUMP ||
		 image.kind == DRGQST_ROM_TAK_CHQ) ?
			DRGQST_CORE_XAVIX2000_I2C_24C04 :
		image.kind == DRGQST_ROM_EPO_SDB ?
			DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM_SDB :
		image.kind == DRGQST_ROM_EPO_HAMD ? DRGQST_CORE_XAVIX_BASE :
		image.kind == DRGQST_ROM_TVPC_DOR ? DRGQST_CORE_XAVIX_I2C_24C16 :
		DRGQST_CORE_DRAGON_QUEST))
	{
		free(core);
		drgqst_rom_release(&image);
		return 2;
	}
	eeprom_size = image.kind == DRGQST_ROM_EPO_HAMD ? 0 :
		image.kind == DRGQST_ROM_EPO_SDB ?
		DRGQST_PERSISTENCE_PARALLEL_NVRAM_SIZE :
		image.kind == DRGQST_ROM_TVPC_DOR ?
		DRGQST_PERSISTENCE_EEPROM24C16_SIZE :
		DRGQST_PERSISTENCE_EEPROM_SIZE;
	io_trace.original_read = core->cpu.read8;
	io_trace.original_write = core->cpu.write8;
	io_trace.original_opaque = core->cpu.opaque;
	io_trace.core = core;
	core->cpu.read8 = probe_cpu_read;
	core->cpu.write8 = probe_cpu_write;
	core->cpu.opaque = &io_trace;
	if (argc >= 6)
		input_bit = (unsigned)strtoul(argv[5], NULL, 0) & 0xff;
	if (argc >= 7)
		punch_mask = (unsigned)strtoul(argv[6], NULL, 0) & 3;
	if (argc >= 8)
		sync_period = (unsigned)strtoul(argv[7], NULL, 0);
	if (sync_period > 255)
		sync_period = 255;
	if (argc >= 9)
		trajectory = (unsigned)strtoul(argv[8], NULL, 0);
	if (argc >= 10)
		trajectory_start = (unsigned)strtoul(argv[9], NULL, 0);
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
	if (argc >= 17)
		sdb_p1_x = (unsigned)strtoul(argv[16], NULL, 0) & 0xff;
	if (argc >= 18)
		sdb_p1_y = (unsigned)strtoul(argv[17], NULL, 0) & 0xff;
	if (argc >= 19)
		sdb_p2_x = (unsigned)strtoul(argv[18], NULL, 0) & 0xff;
	if (argc >= 20)
		sdb_p2_y = (unsigned)strtoul(argv[19], NULL, 0) & 0xff;
	io_trace.trace_tvpc_keyboard = trajectory == 25;
	if (argc >= 5 && strcmp(argv[4], "-"))
	{
		const uint8_t *rom_sha1 = NULL;
		enum drgqst_persistence_kind eeprom_kind;
		enum drgqst_persistence_kind state_kind;
		wchar_t save_directory[32768];
		uint8_t *state = NULL;
		uint8_t durable_eeprom[DRGQST_PERSISTENCE_PARALLEL_NVRAM_SIZE];
		size_t state_size = drgqst_state_serialized_size();
		size_t loaded_size = 0;
		if (!MultiByteToWideChar(CP_ACP, 0, argv[4], -1, save_directory,
			(int)(sizeof(save_directory) / sizeof(save_directory[0]))))
			return 2;
		if (!persistence_profile(image.kind, &rom_sha1, &eeprom_kind,
			&state_kind, &eeprom_size))
		{
			fprintf(stderr, "no persistence profile for ROM kind %u\n",
				(unsigned)image.kind);
			return 2;
		}
		if (image.kind == DRGQST_ROM_EPO_SDB)
			memcpy(durable_eeprom,
				core->machine.state.main_ram + XAVIX_PARALLEL_NVRAM_BASE,
				eeprom_size);
		else if (eeprom_size)
			xavix_eeprom_copy_image(&core->machine.state.peripherals.eeprom,
				durable_eeprom, eeprom_size);
		loaded_size = 0;
		if (eeprom_size && drgqst_persistence_load(save_directory, eeprom_kind,
			rom_sha1, durable_eeprom, eeprom_size, &loaded_size, error,
			sizeof(error) / sizeof(error[0])) && loaded_size == eeprom_size)
		{
			if (image.kind == DRGQST_ROM_EPO_SDB)
				memcpy(core->machine.state.main_ram +
					XAVIX_PARALLEL_NVRAM_BASE, durable_eeprom, eeprom_size);
			else
				xavix_eeprom_load_image(&core->machine.state.peripherals.eeprom,
					durable_eeprom, eeprom_size);
			durable_eeprom_loaded = 1;
		}
		if (load_state)
		{
			state = (uint8_t *)malloc(state_size);
			loaded_size = 0;
			if (!state || !drgqst_persistence_load(save_directory,
				state_kind, rom_sha1,
				state, state_size, &loaded_size, error,
				sizeof(error) / sizeof(error[0])) ||
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
			if (image.kind == DRGQST_ROM_EPO_SDB)
				memcpy(core->machine.state.main_ram +
					XAVIX_PARALLEL_NVRAM_BASE, durable_eeprom, eeprom_size);
			else if (eeprom_size)
				memcpy(core->machine.state.peripherals.eeprom.data,
					durable_eeprom, eeprom_size);
			core->machine.state.peripherals.eeprom.dirty = 0;
			core->machine.state.peripherals.eeprom.write_generation = 0;
			state_loaded = 1;
		}
		free(state);
		if (sync_period)
			core->ban_onep_sync_period = (uint8_t)sync_period;
	}
	if (image.kind == DRGQST_ROM_EPO_SDB)
		memcpy(initial_eeprom,
			core->machine.state.main_ram + XAVIX_PARALLEL_NVRAM_BASE,
			eeprom_size);
	else if (eeprom_size)
		xavix_eeprom_copy_image(&core->machine.state.peripherals.eeprom,
			initial_eeprom, eeprom_size);
	for (frame = 1; frame <= frames && !core->cpu.stopped; ++frame)
	{
		if (!uses_glove_sensor(image.kind))
			apply_calibration_sequence(core, frame - 1);
		if (image.kind == DRGQST_ROM_TAK_CHQ)
		{
			if (fixed_x != UINT_MAX)
				core->machine.state.anport_regs[2] = (uint8_t)fixed_x;
			if (fixed_y != UINT_MAX)
				core->machine.state.anport_regs[3] = (uint8_t)fixed_y;
			/* Diagnostic-only prelude: enter the car-select screen before a
			 * later P0 bit sweep.  No host control mapping is implied. */
			if (trajectory == 29)
			{
				const unsigned offset = frame >= 1500 ? frame - 1500 : UINT_MAX;
				if (offset != UINT_MAX && offset / 40 < 8 && offset % 40 < 4)
					core->machine.state.input0 |= 0x80;
				else
					core->machine.state.input0 &= (uint8_t)~0x80;
			}
		}
		if (image.kind == DRGQST_ROM_TVPC_DOR)
		{
			const int apply_delta = (trajectory < 25 || trajectory > 28) &&
				(trajectory != 17 ||
				(frame >= trajectory_start && frame < trajectory_start + 4));
			if (apply_delta && fixed_x != UINT_MAX)
				core->machine.state.anport_regs[2] = (uint8_t)fixed_x;
			if (apply_delta && fixed_y != UINT_MAX)
				core->machine.state.anport_regs[3] = (uint8_t)fixed_y;
			if ((trajectory == 22 || trajectory == 23) &&
				frame >= trajectory_start)
			{
				const unsigned phase = (frame - trajectory_start) & 15;
				const int delta = phase < 8 ? 8 : -8;
				const unsigned port = trajectory == 22 ? 3 : 2;
				core->machine.state.anport_regs[port] = (uint8_t)(
					core->machine.state.anport_regs[port] + delta);
			}
			if (trajectory == 24 && frame >= trajectory_start)
			{
				if (((frame - trajectory_start) & 7) < 3)
					core->machine.state.input0 |= 0x80;
				else
					core->machine.state.input0 &= (uint8_t)~0x80;
			}
			if (trajectory == 25 && frame >= trajectory_start)
			{
				const unsigned key = (frame - trajectory_start) / 3;
				unsigned row;
				for (row = 0; row < 8; ++row)
					drgqst_core_set_tvpc_keyboard_row(core, row, 0);
				if (key < 64)
					drgqst_core_set_tvpc_keyboard_row(core, key / 8,
						(uint8_t)(1U << (key & 7)));
			}
			if ((trajectory == 27 || trajectory == 28) &&
				frame >= trajectory_start)
			{
				const unsigned phase = (frame - trajectory_start) & 3;
				unsigned row;
				for (row = 0; row < 8; ++row)
					drgqst_core_set_tvpc_keyboard_row(core, row, 0);
				if (fixed_x < 64 &&
					(trajectory == 27 ? phase < 2 : phase == 0))
					drgqst_core_set_tvpc_keyboard_row(core, fixed_x / 8,
						(uint8_t)(1U << (fixed_x & 7)));
				if (trajectory == 28 && fixed_y < 64 && phase == 2)
					drgqst_core_set_tvpc_keyboard_row(core, fixed_y / 8,
						(uint8_t)(1U << (fixed_y & 7)));
			}
		}
		if (image.kind == DRGQST_ROM_EPO_SDB)
		{
			const unsigned offset = frame >= trajectory_start ?
				frame - trajectory_start : UINT_MAX;
			const int pulse = offset != UINT_MAX &&
				offset / 40 < input_pulses && offset % 40 < 4;
			const uint8_t p1x = sdb_p1_x == UINT_MAX ?
				core->machine.state.anport_regs[0] : (uint8_t)sdb_p1_x;
			const uint8_t p1y = sdb_p1_y == UINT_MAX ?
				core->machine.state.anport_regs[1] : (uint8_t)sdb_p1_y;
			const uint8_t p2x = sdb_p2_x == UINT_MAX ?
				core->machine.state.anport_regs[2] : (uint8_t)sdb_p2_x;
			const uint8_t p2y = sdb_p2_y == UINT_MAX ?
				core->machine.state.anport_regs[3] : (uint8_t)sdb_p2_y;
			drgqst_core_set_sdb_input(core, 0, p1x, p1y,
				pulse && (punch_mask & 1));
			drgqst_core_set_sdb_input(core, 1, p2x, p2y,
				pulse && (punch_mask & 2));
		}
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
			if (trajectory == 12 && frame >= trajectory_start &&
				((frame - trajectory_start) %
					(trajectory_interval ? trajectory_interval : UINT_MAX)) < 16)
			{
				static const int8_t circle_x[16] =
				{
					0, 23, 42, 54, 58, 54, 42, 23,
					0, -23, -42, -54, -58, -54, -42, -23
				};
				static const int8_t circle_y[16] =
				{
					-58, -54, -42, -23, 0, 23, 42, 54,
					58, 54, 42, 23, 0, -23, -42, -54
				};
				const unsigned step = (frame - trajectory_start) %
					(trajectory_interval ? trajectory_interval : UINT_MAX);
				x = (uint8_t)(0x80 + circle_x[step]);
				y = (uint8_t)(0x80 + circle_y[step]);
				left_pressed = !!(punch_mask & 1);
				right_pressed = !!(punch_mask & 2);
			}
			if ((trajectory == 14 || trajectory == 15) &&
				frame >= trajectory_start)
			{
				static const int8_t circle_x[16] =
				{
					0, 23, 42, 54, 58, 54, 42, 23,
					0, -23, -42, -54, -58, -54, -42, -23
				};
				static const int8_t circle_y[16] =
				{
					-58, -54, -42, -23, 0, 23, 42, 54,
					58, 54, 42, 23, 0, -23, -42, -54
				};
				const unsigned period = trajectory_interval ?
					trajectory_interval : 64;
				const unsigned phase = (frame - trajectory_start) % period;
				unsigned index = phase * 16 / period;
				if (trajectory == 15)
					index = (16 - index) & 15;
				x = (uint8_t)(0x80 + circle_x[index]);
				y = (uint8_t)(0x80 + circle_y[index]);
				left_pressed = !!(punch_mask & 1);
				right_pressed = !!(punch_mask & 2);
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
			if (state_loaded && guard_hold && frame >= trajectory_start &&
				frame < trajectory_start + guard_hold)
			{
				left_pressed = !!(punch_mask & 1);
				right_pressed = !!(punch_mask & 2);
			}
			drgqst_core_set_mouse(core, x, y, left_pressed, right_pressed);
			if (trajectory == 18 && frame >= trajectory_start)
				xavix_machine_set_sword_input(&core->machine, x, y,
					XAVIX_SENSOR_NONE);
			if (trajectory >= 30 && trajectory <= 33 &&
				frame >= trajectory_start)
			{
				static const enum xavix_sensor_mode modes[4] =
				{
					XAVIX_SENSOR_VERTICAL,
					XAVIX_SENSOR_HORIZONTAL,
					XAVIX_SENSOR_DIAGONAL_DOWN,
					XAVIX_SENSOR_DIAGONAL_UP
				};
				xavix_machine_set_sword_input(&core->machine, x, y,
					modes[trajectory - 30]);
			}
			if (trajectory == 34 && frame >= trajectory_start)
				xavix_machine_set_sword_input(&core->machine, x, y,
					XAVIX_SENSOR_POINT);
			if (trajectory == 35 && frame >= trajectory_start)
				xavix_machine_set_sword_input(&core->machine, x, y,
					frame == trajectory_start ?
					XAVIX_SENSOR_POINT : XAVIX_SENSOR_NONE);
			if (trajectory == 13 && frame >= trajectory_start &&
				frame < trajectory_start + (guard_hold ? guard_hold : 120))
				xavix_machine_set_sword_input(&core->machine, x, y,
					XAVIX_SENSOR_VERTICAL);
			if (trajectory == 16 && frame >= trajectory_start)
			{
				static const enum xavix_sensor_mode rotation[8] =
				{
					XAVIX_SENSOR_VERTICAL, XAVIX_SENSOR_DIAGONAL_DOWN,
					XAVIX_SENSOR_HORIZONTAL, XAVIX_SENSOR_DIAGONAL_UP,
					XAVIX_SENSOR_VERTICAL, XAVIX_SENSOR_DIAGONAL_DOWN,
					XAVIX_SENSOR_HORIZONTAL, XAVIX_SENSOR_DIAGONAL_UP
				};
				const unsigned period = trajectory_interval ?
					trajectory_interval : 64;
				const unsigned phase = (frame - trajectory_start) % period;
				xavix_machine_set_sword_input(&core->machine, x, y,
					rotation[phase * 8 / period]);
			}
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
		if (!uses_glove_sensor(image.kind) && input_bit)
		{
			const unsigned offset = frame >= trajectory_start ?
				frame - trajectory_start : UINT_MAX;
			if (guard_hold && frame >= trajectory_start &&
				frame < trajectory_start + guard_hold)
				core->machine.state.input0 |= (uint8_t)input_bit;
			else if (offset != UINT_MAX && offset / 40 < input_pulses &&
				offset % 40 < 4)
				core->machine.state.input0 |= (uint8_t)input_bit;
			else
				core->machine.state.input0 &= (uint8_t)~input_bit;
		}
		if (image.kind == DRGQST_ROM_EPO_HAMD && trajectory == 21)
		{
			static const uint8_t patterns[] =
				{ 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x81 };
			const unsigned offset = frame >= trajectory_start ?
				frame - trajectory_start : UINT_MAX;
			core->machine.state.input0 = 0;
			if (offset != UINT_MAX && offset / 60 < sizeof(patterns) &&
				offset % 60 < 4)
				core->machine.state.input0 = patterns[offset / 60];
		}
		if (image.kind == DRGQST_ROM_EPO_HAMD && trajectory == 19 &&
			frame >= trajectory_start)
		{
			const unsigned period = trajectory_interval ?
				trajectory_interval : 2;
			const unsigned packet_index =
				(frame - trajectory_start) / period;
			if (packet_index < 64 &&
				(frame - trajectory_start) % period == 0)
					drgqst_core_trigger_hamd_packet(core,
						(uint8_t)(packet_index * 2 + 1));
		}
		if (image.kind == DRGQST_ROM_EPO_HAMD && trajectory == 20 &&
			frame >= trajectory_start)
		{
			const uint8_t first = fixed_x != UINT_MAX ?
				(uint8_t)fixed_x : 0x15;
			const uint8_t second = fixed_y != UINT_MAX ?
				(uint8_t)fixed_y : first;
			drgqst_core_trigger_hamd_packet(core, first);
			if (second != first)
				drgqst_core_trigger_hamd_packet(core, second);
		}
		if (image.kind == DRGQST_ROM_EPO_HAMD && trajectory == 29 &&
			frame >= trajectory_start && frame < trajectory_start +
				(guard_hold ? guard_hold : 4))
		{
			const uint8_t first = fixed_x != UINT_MAX ?
				(uint8_t)fixed_x : 0x15;
			const uint8_t second = fixed_y != UINT_MAX ?
				(uint8_t)fixed_y : first;
			drgqst_core_trigger_hamd_packet(core, first);
			if (second != first)
				drgqst_core_trigger_hamd_packet(core, second);
		}
		pixels = drgqst_core_run_frame(core);
		if (image.kind == DRGQST_ROM_TAK_CHQ)
		{
			const int16_t *audio = drgqst_core_frame_audio(core);
			unsigned sample;
			for (sample = 0;
				sample < DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME * 2;
				++sample)
			{
				const int value = audio[sample];
				const unsigned magnitude = value < 0 ?
					(unsigned)(-value) : (unsigned)value;
				++pcm_samples;
				if (value)
					++pcm_nonzero;
				if (magnitude > pcm_peak)
					pcm_peak = magnitude;
			}
		}
		if (image.kind == DRGQST_ROM_TVPC_DOR && trajectory == 25 &&
			frame >= trajectory_start &&
			(frame - trajectory_start) % 3 == 1)
		{
			const unsigned key = (frame - trajectory_start) / 3;
			if (key < 64)
				printf("tvpc-key row=%u bit=%02X decoded=%02X/%02X/%02X/%02X/%02X/%02X/%02X/%02X\n",
					key / 8, 1U << (key & 7),
					core->machine.state.main_ram[0xac],
					core->machine.state.main_ram[0xad],
					core->machine.state.main_ram[0xae],
					core->machine.state.main_ram[0xaf],
					core->machine.state.main_ram[0xb0],
					core->machine.state.main_ram[0xb1],
					core->machine.state.main_ram[0xb2],
					core->machine.state.main_ram[0xb3]);
		}
		if (image.kind == DRGQST_ROM_EPO_HAMD && trajectory == 19 &&
			frame >= trajectory_start)
		{
			const unsigned period = trajectory_interval ?
				trajectory_interval : 2;
			const unsigned packet_index =
				(frame - trajectory_start) / period;
			if (packet_index < 64 &&
				(frame - trajectory_start) % period == 0)
				printf("hamd-packet=%02X decoded=%02X/%02X previous=%02X/%02X raw=%02X/%02X shift=%02X\n",
					packet_index * 2 + 1,
					core->machine.state.main_ram[0xa4],
					core->machine.state.main_ram[0xa5],
					core->machine.state.main_ram[0xa0],
					core->machine.state.main_ram[0xa1],
					core->machine.state.main_ram[0x9e],
					core->machine.state.main_ram[0x9f],
					core->machine.state.main_ram[0x9c]);
		}
		if (image.kind == DRGQST_ROM_EPO_HAMD && trajectory == 20 &&
			frame >= trajectory_start && frame < trajectory_start + 16)
			printf("hamd-repeat frame=%u decoded=%02X/%02X previous=%02X/%02X raw=%02X/%02X mask=%02X\n",
				frame,
				core->machine.state.main_ram[0xa4],
				core->machine.state.main_ram[0xa5],
				core->machine.state.main_ram[0xa0],
				core->machine.state.main_ram[0xa1],
				core->machine.state.main_ram[0x9e],
				core->machine.state.main_ram[0x9f],
				core->epo_hamd_packet_mask);
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
	if (image.kind == DRGQST_ROM_TAK_CHQ)
	{
		printf("pcm-samples=%llu nonzero=%llu peak=%u\n",
			(unsigned long long)pcm_samples,
			(unsigned long long)pcm_nonzero, pcm_peak);
	}
	{
		unsigned index;
		for (index = 0; index < sizeof(io_trace.counters) /
			sizeof(io_trace.counters[0]); ++index)
		{
			const io_probe_counter *counter = &io_trace.counters[index];
			if (counter->reads)
				printf("io-read %04X count=%llu nonzero=%llu last-pc=%06lX value=%02X last-nonzero=%06lX/%02X\n",
					counter->address, (unsigned long long)counter->reads,
					(unsigned long long)counter->nonzero,
					(unsigned long)counter->last_pc, counter->last_value,
					(unsigned long)counter->last_nonzero_pc,
					counter->last_nonzero_value);
		}
		for (index = 0; index < sizeof(io_trace.writes) /
			sizeof(io_trace.writes[0]); ++index)
		{
			const io_probe_counter *counter = &io_trace.writes[index];
			if (counter->reads)
				printf("ram-write %04X count=%llu nonzero=%llu last-pc=%06lX value=%02X last-nonzero=%06lX/%02X\n",
					counter->address, (unsigned long long)counter->reads,
					(unsigned long long)counter->nonzero,
					(unsigned long)counter->last_pc, counter->last_value,
					(unsigned long)counter->last_nonzero_pc,
					counter->last_nonzero_value);
		}
	}
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
		for (address = 0; address < eeprom_size; ++address)
		{
			const uint8_t value = image.kind == DRGQST_ROM_EPO_SDB ?
				core->machine.state.main_ram[
					XAVIX_PARALLEL_NVRAM_BASE + address] :
				core->machine.state.peripherals.eeprom.data[address];
			if (value != initial_eeprom[address])
			{
				if (changed < 64)
					printf("%s-change %03X %02X->%02X\n",
						image.kind == DRGQST_ROM_EPO_SDB ? "nvram" : "eeprom", address,
						initial_eeprom[address], value);
				++changed;
			}
		}
		printf("%s-diff=%u durable-loaded=%u\n",
			image.kind == DRGQST_ROM_EPO_SDB ? "nvram" : "eeprom", changed,
			(unsigned)durable_eeprom_loaded);
	}
	free(core);
	drgqst_rom_release(&image);
	return 0;
}
