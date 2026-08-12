// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "rom_loader.h"
#include "sha1.h"

#include "miniz.h"

#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

enum
{
	MAXIMUM_ZIP_SIZE = 64 * 1024 * 1024,
	EPO_HAMD_U2_SIZE = 0x100000,
	EPO_HAMD_U3_SIZE = 0x200000,
	EPO_HAMD_U3_OFFSET = 0x400000
};

#define EPO_HAMD_U2_CRC32 UINT32_C(0x6c2d9d98)
#define EPO_HAMD_U3_CRC32 UINT32_C(0xe437c8d0)

static const uint8_t EPO_HAMD_U2_SHA1[20] = {
	0x89, 0xa8, 0xe6, 0xd2, 0x36, 0xea, 0x3d, 0xad, 0xb8, 0x82,
	0xe3, 0xec, 0xf1, 0x2e, 0x41, 0xbd, 0x50, 0x22, 0x27, 0x10
};

static const uint8_t EPO_HAMD_U3_SHA1[20] = {
	0xf5, 0x7c, 0x54, 0xa7, 0x3e, 0xd3, 0x88, 0x26, 0xf4, 0xb9,
	0x86, 0x10, 0xa0, 0xaa, 0x1f, 0x15, 0xcf, 0x95, 0x61, 0x4d
};

typedef struct supported_rom
{
	enum drgqst_rom_kind kind;
	size_t size;
	uint32_t crc32;
	uint8_t sha1[20];
} supported_rom;

static const supported_rom SUPPORTED_ROMS[] = {
	{
		DRGQST_ROM_DRAGON_QUEST,
		DRGQST_ROM_SIZE,
		DRGQST_ROM_CRC32,
		{
			0x16, 0x77, 0xe8, 0x1c, 0xed, 0xcf, 0x34, 0x9d, 0xe7, 0xbf,
			0x09, 0x1a, 0x23, 0x2d, 0xc8, 0x2c, 0x64, 0x24, 0xef, 0xba
		}
	},
	{
		DRGQST_ROM_BAN_ONEP,
		DRGQST_ROM_SIZE,
		BAN_ONEP_ROM_CRC32,
		{
			0xdb, 0x85, 0xf6, 0xcc, 0x48, 0xd7, 0x7c, 0x5a, 0x49, 0x67,
			0xb9, 0xb8, 0xe2, 0x99, 0x91, 0x67, 0xe3, 0xdf, 0xc8, 0xc8
		}
	},
	{
		DRGQST_ROM_BAN_OMT,
		BAN_OMT_ROM_SIZE,
		BAN_OMT_ROM_CRC32,
		{
			0xd0, 0xcf, 0x13, 0x45, 0xb7, 0x65, 0xd6, 0x6c, 0xa9, 0xa0,
			0x87, 0x0e, 0xe6, 0xd0, 0xe3, 0xcc, 0xd8, 0x4a, 0x8c, 0x0b
		}
	},
	{
		DRGQST_ROM_TTV_LOTR,
		TTV_ROM_SIZE,
		TTV_LOTR_ROM_CRC32,
		{
			0x26, 0x4a, 0x9d, 0x43, 0x27, 0xaf, 0x0a, 0x07, 0x58, 0x41,
			0xad, 0x61, 0x29, 0xdb, 0x67, 0xd8, 0x2c, 0xf7, 0x41, 0xf1
		}
	},
	{
		DRGQST_ROM_TTV_SW,
		TTV_ROM_SIZE,
		TTV_SW_ROM_CRC32,
		{
			0x1e, 0xd8, 0xd5, 0x56, 0xf3, 0x1b, 0x41, 0x82, 0x25, 0x9c,
			0xa8, 0xc7, 0x66, 0xd6, 0x0c, 0x82, 0x4d, 0x8d, 0x97, 0x44
		}
	},
	{
		DRGQST_ROM_TTV_SWJ,
		TTV_ROM_SIZE,
		TTV_SWJ_ROM_CRC32,
		{
			0x40, 0x6f, 0x0b, 0xcc, 0xb0, 0x1c, 0xd4, 0xa2, 0x6f, 0xe4,
			0xa5, 0x67, 0x5d, 0x7e, 0xbe, 0xcc, 0x78, 0xc5, 0x81, 0x47
		}
	},
	{
		DRGQST_ROM_TTV_MX,
		TTV_ROM_SIZE,
		TTV_MX_ROM_CRC32,
		{
			0x13, 0x7f, 0x97, 0xd7, 0xd8, 0x57, 0x69, 0x7a, 0x13, 0xe0,
			0xc8, 0x98, 0x45, 0x09, 0x99, 0x4d, 0xc7, 0xbc, 0x5f, 0xc5
		}
	},
	{
		DRGQST_ROM_TOM_JUMP,
		TTV_ROM_SIZE,
		TOM_JUMP_ROM_CRC32,
		{
			0xbc, 0xa7, 0x53, 0x5b, 0xaa, 0x6a, 0x54, 0xad, 0x3e, 0xe0,
			0x92, 0x9b, 0xd3, 0xb7, 0x4a, 0x22, 0xcb, 0x51, 0x39, 0xda
		}
	},
	{
		DRGQST_ROM_EPO_SDB,
		EPO_SDB_ROM_SIZE,
		EPO_SDB_ROM_CRC32,
		{
			0x47, 0xa9, 0x68, 0x22, 0xd4, 0xd7, 0xd6, 0xa0, 0xf6, 0xbe,
			0x5c, 0xd7, 0x29, 0xc3, 0x74, 0x7d, 0xba, 0xb6, 0x59, 0x79
		}
	},
	{
		DRGQST_ROM_EPO_BOWL,
		EPO_BOWL_ROM_SIZE,
		EPO_BOWL_ROM_CRC32,
		{
			0xeb, 0xe3, 0x79, 0x21, 0x72, 0xdc, 0x43, 0x90, 0x4b, 0x92,
			0x26, 0xbe, 0xb2, 0x7f, 0x1d, 0xa8, 0x9d, 0x23, 0x88, 0xcc
		}
	},
	{
		DRGQST_ROM_TAK_CHQ,
		TAK_CHQ_ROM_SIZE,
		TAK_CHQ_ROM_CRC32,
		{
			0xa3, 0x08, 0x84, 0xda, 0x55, 0x54, 0x48, 0x3e, 0xbf, 0xd0,
			0x00, 0x9c, 0xf5, 0xdd, 0x17, 0x68, 0xbe, 0x8a, 0x99, 0xcb
		}
	},
	{
		DRGQST_ROM_EPO_EBOX,
		EPO_EBOX_ROM_SIZE,
		EPO_EBOX_ROM_CRC32,
		{
			0x7f, 0x7b, 0x61, 0x3f, 0x0a, 0xb8, 0xf4, 0x3f, 0x5c, 0xad,
			0x0d, 0x13, 0xde, 0x53, 0x89, 0x21, 0xe7, 0x7c, 0xae, 0x9c
		}
	},
	{
		DRGQST_ROM_BAN_NARU,
		BAN_NARU_ROM_SIZE,
		BAN_NARU_ROM_CRC32,
		{
			0x13, 0xe3, 0xd2, 0xde, 0x5d, 0x5a, 0x08, 0x46, 0x35, 0xca,
			0xb1, 0x58, 0xf3, 0x63, 0x9a, 0x1e, 0xa7, 0x32, 0x65, 0xdc
		}
	},
	{
		DRGQST_ROM_BAN_BLDJ,
		XAVIX2_ROM_SIZE,
		BAN_BLDJ_ROM_CRC32,
		{
			0x2f, 0x5f, 0x48, 0x09, 0xa0, 0x7a, 0x2f, 0x56, 0x71, 0xf8,
			0x1a, 0xa2, 0x2e, 0x37, 0x9c, 0x11, 0xc4, 0x39, 0x43, 0xa0
		}
	},
	{
		DRGQST_ROM_BAN_DB2J,
		XAVIX2_ROM_SIZE,
		BAN_DB2J_ROM_CRC32,
		{
			0xf1, 0x88, 0x04, 0x70, 0xf0, 0xdb, 0x56, 0x13, 0x5d, 0x9b,
			0xc8, 0x8d, 0x71, 0x93, 0xd0, 0x37, 0xac, 0x49, 0xb9, 0x96
		}
	},
	{
		DRGQST_ROM_BAN_DBZ,
		XAVIX2_ROM_SIZE,
		BAN_DBZ_ROM_CRC32,
		{
			0x6c, 0x74, 0x6a, 0xf7, 0x63, 0x27, 0x3b, 0xd9, 0xe4, 0x79,
			0x29, 0xc3, 0xba, 0x85, 0x7c, 0x7a, 0xf5, 0x63, 0xbf, 0x79
		}
	},
	{
		DRGQST_ROM_TVPC_DOR,
		TVPC_DOR_ROM_SIZE,
		TVPC_DOR_ROM_CRC32,
		{
			0x98, 0xfa, 0x86, 0xf8, 0x5e, 0x00, 0xaa, 0x40, 0xe7, 0xa5,
			0x85, 0xff, 0x0b, 0xc9, 0x30, 0xcb, 0x5c, 0xa8, 0x83, 0x62
		}
	}
};

const char *drgqst_rom_short_name(enum drgqst_rom_kind kind)
{
	switch (kind)
	{
	case DRGQST_ROM_DRAGON_QUEST:
		return "drgqst";
	case DRGQST_ROM_BAN_ONEP:
		return "ban_onep";
	case DRGQST_ROM_BAN_OMT:
		return "ban_omt";
	case DRGQST_ROM_TTV_LOTR:
		return "ttv_lotr";
	case DRGQST_ROM_TTV_SW:
		return "ttv_sw";
	case DRGQST_ROM_TTV_SWJ:
		return "ttv_swj";
	case DRGQST_ROM_TTV_MX:
		return "ttv_mx";
	case DRGQST_ROM_TOM_JUMP:
		return "tom_jump";
	case DRGQST_ROM_EPO_SDB:
		return "epo_sdb";
	case DRGQST_ROM_EPO_BOWL:
		return "epo_bowl";
	case DRGQST_ROM_TAK_CHQ:
		return "tak_chq";
	case DRGQST_ROM_EPO_EBOX:
		return "epo_ebox";
	case DRGQST_ROM_BAN_NARU:
		return "ban_naru";
	case DRGQST_ROM_BAN_BLDJ:
		return "ban_bldj";
	case DRGQST_ROM_BAN_DB2J:
		return "ban_db2j";
	case DRGQST_ROM_BAN_DBZ:
		return "ban_dbz";
	case DRGQST_ROM_EPO_HAMD:
		return "epo_hamd";
	case DRGQST_ROM_TVPC_DOR:
		return "tvpc_dor";
	case DRGQST_ROM_UNKNOWN:
	default:
		return "unknown";
	}
}

int drgqst_rom_is_xavix2(enum drgqst_rom_kind kind)
{
	return kind == DRGQST_ROM_BAN_NARU ||
		kind == DRGQST_ROM_BAN_BLDJ ||
		kind == DRGQST_ROM_BAN_DB2J ||
		kind == DRGQST_ROM_BAN_DBZ;
}

static void set_error(wchar_t *error, size_t error_length, const wchar_t *format, ...)
{
	va_list arguments;

	if (!error || !error_length)
		return;

	va_start(arguments, format);
	_vsnwprintf(error, error_length, format, arguments);
	va_end(arguments);
	error[error_length - 1] = L'\0';
}

static void *read_entire_file(const wchar_t *path, size_t *length, wchar_t *error, size_t error_length)
{
	HANDLE file;
	LARGE_INTEGER file_size;
	uint8_t *buffer;
	DWORD total = 0;

	file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		set_error(error, error_length, L"The selected ZIP could not be opened (Windows error %lu).", GetLastError());
		return NULL;
	}

	if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart <= 0 || file_size.QuadPart > MAXIMUM_ZIP_SIZE)
	{
		set_error(error, error_length, L"The selected file is not a supported ZIP size.");
		CloseHandle(file);
		return NULL;
	}

	buffer = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)file_size.QuadPart);
	if (!buffer)
	{
		set_error(error, error_length, L"There is not enough memory to read the selected ZIP.");
		CloseHandle(file);
		return NULL;
	}

	while (total < (DWORD)file_size.QuadPart)
	{
		DWORD received = 0;
		DWORD remaining = (DWORD)file_size.QuadPart - total;

		if (!ReadFile(file, buffer + total, remaining, &received, NULL) || !received)
		{
			set_error(error, error_length, L"The selected ZIP could not be read completely.");
			HeapFree(GetProcessHeap(), 0, buffer);
			CloseHandle(file);
			return NULL;
		}
		total += received;
	}

	CloseHandle(file);
	*length = (size_t)file_size.QuadPart;
	return buffer;
}

static int sha1_matches(
	const uint8_t *data,
	size_t length,
	const uint8_t expected[20],
	uint8_t digest[20])
{
	sha1_calculate(data, length, digest);
	return memcmp(digest, expected, 20) == 0;
}

void drgqst_rom_release(drgqst_rom_image *image)
{
	if (!image)
		return;

	if (image->data)
		HeapFree(GetProcessHeap(), 0, image->data);
	memset(image, 0, sizeof(*image));
}

int drgqst_rom_load_zip(
	const wchar_t *path,
	drgqst_rom_image *image,
	wchar_t *error,
	size_t error_length)
{
	mz_zip_archive archive;
	void *zip_data;
	size_t zip_length = 0;
	mz_uint file_count;
	int selected_file = -1;
	int epo_hamd_u2_file = -1;
	int epo_hamd_u3_file = -1;
	const supported_rom *selected_rom = NULL;
	uint8_t *rom_data = NULL;
	uint32_t crc32;
	uint8_t sha1[20];
	mz_uint index;

	if (!path || !image)
	{
		set_error(error, error_length, L"No ROM ZIP was selected.");
		return 0;
	}

	zip_data = read_entire_file(path, &zip_length, error, error_length);
	if (!zip_data)
		return 0;

	memset(&archive, 0, sizeof(archive));
	if (!mz_zip_reader_init_mem(&archive, zip_data, zip_length, 0))
	{
		set_error(error, error_length, L"The selected file is not a readable ZIP archive.");
		HeapFree(GetProcessHeap(), 0, zip_data);
		return 0;
	}

	file_count = mz_zip_reader_get_num_files(&archive);
	for (index = 0; index < file_count; ++index)
	{
		mz_zip_archive_file_stat stat;

		if (!mz_zip_reader_file_stat(&archive, index, &stat) || stat.m_is_directory)
			continue;
		if (stat.m_uncomp_size == EPO_HAMD_U2_SIZE &&
			stat.m_crc32 == EPO_HAMD_U2_CRC32)
			epo_hamd_u2_file = (int)index;
		else if (stat.m_uncomp_size == EPO_HAMD_U3_SIZE &&
			stat.m_crc32 == EPO_HAMD_U3_CRC32)
			epo_hamd_u3_file = (int)index;
		unsigned candidate;
		for (candidate = 0; candidate < sizeof(SUPPORTED_ROMS) / sizeof(SUPPORTED_ROMS[0]); ++candidate)
		{
			if (stat.m_uncomp_size == SUPPORTED_ROMS[candidate].size &&
				stat.m_crc32 == SUPPORTED_ROMS[candidate].crc32)
			{
				selected_file = (int)index;
				selected_rom = &SUPPORTED_ROMS[candidate];
				break;
			}
		}
		if (selected_file >= 0)
			break;
	}

	if (selected_file < 0 &&
		(epo_hamd_u2_file < 0 || epo_hamd_u3_file < 0))
	{
		set_error(
			error,
			error_length,
			L"This ZIP does not contain a supported XaviX ROM." );
		mz_zip_reader_end(&archive);
		HeapFree(GetProcessHeap(), 0, zip_data);
		return 0;
	}
	if (selected_file < 0)
	{
		uint8_t component_sha1[20];
		rom_data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
			EPO_HAMD_ROM_SIZE);
		if (!rom_data)
		{
			set_error(error, error_length,
				L"There is not enough memory to assemble the ROM set.");
			mz_zip_reader_end(&archive);
			HeapFree(GetProcessHeap(), 0, zip_data);
			return 0;
		}
		if (!mz_zip_reader_extract_to_mem(&archive,
			(mz_uint)epo_hamd_u2_file, rom_data, EPO_HAMD_U2_SIZE, 0) ||
			!sha1_matches(rom_data, EPO_HAMD_U2_SIZE,
				EPO_HAMD_U2_SHA1, component_sha1) ||
			!mz_zip_reader_extract_to_mem(&archive,
				(mz_uint)epo_hamd_u3_file,
				rom_data + EPO_HAMD_U3_OFFSET, EPO_HAMD_U3_SIZE, 0) ||
			!sha1_matches(rom_data + EPO_HAMD_U3_OFFSET,
				EPO_HAMD_U3_SIZE, EPO_HAMD_U3_SHA1, component_sha1))
		{
			set_error(error, error_length,
				L"The multi-chip ROM set failed extraction or SHA1 verification.");
			HeapFree(GetProcessHeap(), 0, rom_data);
			mz_zip_reader_end(&archive);
			HeapFree(GetProcessHeap(), 0, zip_data);
			return 0;
		}
		crc32 = (uint32_t)mz_crc32(MZ_CRC32_INIT, rom_data,
			EPO_HAMD_ROM_SIZE);
		mz_zip_reader_end(&archive);
		HeapFree(GetProcessHeap(), 0, zip_data);
		drgqst_rom_release(image);
		image->data = rom_data;
		image->size = EPO_HAMD_ROM_SIZE;
		image->crc32 = crc32;
		image->kind = DRGQST_ROM_EPO_HAMD;
		if (error && error_length)
			error[0] = L'\0';
		return 1;
	}

	rom_data = HeapAlloc(GetProcessHeap(), 0, selected_rom->size);
	if (!rom_data)
	{
		set_error(error, error_length, L"There is not enough memory to extract the ROM.");
		mz_zip_reader_end(&archive);
		HeapFree(GetProcessHeap(), 0, zip_data);
		return 0;
	}

	if (!mz_zip_reader_extract_to_mem(&archive, (mz_uint)selected_file,
		rom_data, selected_rom->size, 0))
	{
		set_error(error, error_length, L"The ROM entry is damaged or could not be decompressed.");
		HeapFree(GetProcessHeap(), 0, rom_data);
		mz_zip_reader_end(&archive);
		HeapFree(GetProcessHeap(), 0, zip_data);
		return 0;
	}

	crc32 = (uint32_t)mz_crc32(MZ_CRC32_INIT, rom_data, selected_rom->size);
	if (!selected_rom || crc32 != selected_rom->crc32)
	{
		set_error(error, error_length, L"The extracted ROM failed its CRC32 check.");
		HeapFree(GetProcessHeap(), 0, rom_data);
		mz_zip_reader_end(&archive);
		HeapFree(GetProcessHeap(), 0, zip_data);
		return 0;
	}
	if (!sha1_matches(rom_data, selected_rom->size, selected_rom->sha1, sha1))
	{
		set_error(
			error,
			error_length,
			L"The extracted ROM failed its SHA1 check (%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X).",
			sha1[0], sha1[1], sha1[2], sha1[3], sha1[4], sha1[5], sha1[6], sha1[7], sha1[8], sha1[9],
			sha1[10], sha1[11], sha1[12], sha1[13], sha1[14], sha1[15], sha1[16], sha1[17], sha1[18], sha1[19]);
		HeapFree(GetProcessHeap(), 0, rom_data);
		mz_zip_reader_end(&archive);
		HeapFree(GetProcessHeap(), 0, zip_data);
		return 0;
	}

	mz_zip_reader_end(&archive);
	HeapFree(GetProcessHeap(), 0, zip_data);

	drgqst_rom_release(image);
	image->data = rom_data;
	image->size = selected_rom->size;
	image->crc32 = crc32;
	image->kind = selected_rom->kind;
	if (error && error_length)
		error[0] = L'\0';
	return 1;
}
