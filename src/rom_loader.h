// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#ifndef DRGQST_PLAYER_ROM_LOADER_H
#define DRGQST_PLAYER_ROM_LOADER_H

#include <stddef.h>
#include <stdint.h>

enum
{
	DRGQST_ROM_SIZE = 0x800000,
	BAN_OMT_ROM_SIZE = 0x400000,
	TTV_ROM_SIZE = 0x800000,
	BAN_NARU_ROM_SIZE = 0x800000,
	EPO_HAMD_ROM_SIZE = 0x800000,
	TVPC_DOR_ROM_SIZE = 0x400000
};

#define DRGQST_ROM_CRC32 UINT32_C(0x3d24413f)
#define BAN_ONEP_ROM_CRC32 UINT32_C(0xc5cb5a5f)
#define BAN_OMT_ROM_CRC32 UINT32_C(0x1c1dc6fb)
#define TTV_LOTR_ROM_CRC32 UINT32_C(0xa034ecd5)
#define TTV_SW_ROM_CRC32 UINT32_C(0x51cae5fd)
#define TTV_SWJ_ROM_CRC32 UINT32_C(0xa5c22ed0)
#define BAN_NARU_ROM_CRC32 UINT32_C(0xe3465ad2)
#define TVPC_DOR_ROM_CRC32 UINT32_C(0x6f2edbb2)

enum drgqst_rom_kind
{
	DRGQST_ROM_UNKNOWN = 0,
	DRGQST_ROM_DRAGON_QUEST,
	DRGQST_ROM_BAN_ONEP,
	DRGQST_ROM_BAN_OMT,
	DRGQST_ROM_TTV_LOTR,
	DRGQST_ROM_TTV_SW,
	DRGQST_ROM_TTV_SWJ,
	/* Experimental XaviX 2 support. */
	DRGQST_ROM_BAN_NARU,
	/* Experimental original-generation XaviX support. */
	DRGQST_ROM_EPO_HAMD,
	DRGQST_ROM_TVPC_DOR
};

typedef struct drgqst_rom_image
{
	uint8_t *data;
	size_t size;
	uint32_t crc32;
	enum drgqst_rom_kind kind;
} drgqst_rom_image;

const char *drgqst_rom_short_name(enum drgqst_rom_kind kind);

int drgqst_rom_load_zip(
	const wchar_t *path,
	drgqst_rom_image *image,
	wchar_t *error,
	size_t error_length);

void drgqst_rom_release(drgqst_rom_image *image);

#endif
