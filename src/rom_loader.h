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
	XAVIX2_ROM_SIZE = 0x800000,
	BAN_NARU_ROM_SIZE = XAVIX2_ROM_SIZE,
	EPO_HAMD_ROM_SIZE = 0x800000,
	EPO_SDB_ROM_SIZE = 0x400000,
	EPO_BOWL_ROM_SIZE = 0x200000,
	TAK_CHQ_ROM_SIZE = 0x400000,
	EPO_EBOX_ROM_SIZE = 0x400000,
	EPO_ES2J_ROM_SIZE = 0x400000,
	EPO_HAMC_ROM_SIZE = 0x400000,
	TVPC_DOR_ROM_SIZE = 0x400000,
	TVPC_HAM_ROM_SIZE = 0x400000,
	TVPC_HK_ROM_SIZE = 0x400000,
	TOM_DPGM_ROM_SIZE = 0x400000
};

#define DRGQST_ROM_CRC32 UINT32_C(0x3d24413f)
#define BAN_ONEP_ROM_CRC32 UINT32_C(0xc5cb5a5f)
#define BAN_OMT_ROM_CRC32 UINT32_C(0x1c1dc6fb)
#define TTV_LOTR_ROM_CRC32 UINT32_C(0xa034ecd5)
#define TTV_SW_ROM_CRC32 UINT32_C(0x51cae5fd)
#define TTV_SWJ_ROM_CRC32 UINT32_C(0xa5c22ed0)
#define TTV_MX_ROM_CRC32 UINT32_C(0xe64bf1a1)
#define TOM_JUMP_ROM_CRC32 UINT32_C(0x20bf5c17)
#define EPO_SDB_ROM_CRC32 UINT32_C(0xa004a764)
#define EPO_BOWL_ROM_CRC32 UINT32_C(0xd34f8d9e)
#define TAK_CHQ_ROM_CRC32 UINT32_C(0xffd2eb95)
#define EPO_EBOX_ROM_CRC32 UINT32_C(0xe25ae4f5)
#define EPO_ES2J_ROM_CRC32 UINT32_C(0x840aecb1)
#define EPO_HAMC_ROM_CRC32 UINT32_C(0xb1177813)
#define BAN_NARU_ROM_CRC32 UINT32_C(0xe3465ad2)
#define BAN_BLDJ_ROM_CRC32 UINT32_C(0xaa865fe3)
#define BAN_DB2J_ROM_CRC32 UINT32_C(0x7362ac0d)
#define BAN_DBZ_ROM_CRC32 UINT32_C(0x7e535ea2)
#define TVPC_DOR_ROM_CRC32 UINT32_C(0x6f2edbb2)
#define TVPC_HAM_ROM_CRC32 UINT32_C(0x76e8c854)
#define TVPC_HK_ROM_CRC32 UINT32_C(0x87fc2f73)
#define TOM_DPGM_ROM_CRC32 UINT32_C(0x1dc181b3)

enum drgqst_rom_kind
{
	DRGQST_ROM_UNKNOWN = 0,
	DRGQST_ROM_DRAGON_QUEST,
	DRGQST_ROM_BAN_ONEP,
	DRGQST_ROM_BAN_OMT,
	DRGQST_ROM_TTV_LOTR,
	DRGQST_ROM_TTV_SW,
	DRGQST_ROM_TTV_SWJ,
	DRGQST_ROM_TTV_MX,
	DRGQST_ROM_TOM_JUMP,
	DRGQST_ROM_EPO_SDB,
	DRGQST_ROM_EPO_BOWL,
	/* Experimental XaviX 2 support. */
	DRGQST_ROM_BAN_NARU,
	DRGQST_ROM_BAN_BLDJ,
	DRGQST_ROM_BAN_DB2J,
	DRGQST_ROM_BAN_DBZ,
	/* Experimental original-generation XaviX support. */
	DRGQST_ROM_EPO_HAMD,
	DRGQST_ROM_TVPC_DOR,
	/* Keep newly recognized images append-only for diagnostic stability. */
	DRGQST_ROM_TAK_CHQ,
	DRGQST_ROM_EPO_EBOX,
	DRGQST_ROM_EPO_ES2J,
	DRGQST_ROM_EPO_HAMC,
	DRGQST_ROM_TVPC_HAM,
	DRGQST_ROM_TVPC_HK,
	DRGQST_ROM_TOM_DPGM
};

typedef struct drgqst_rom_image
{
	uint8_t *data;
	size_t size;
	uint32_t crc32;
	enum drgqst_rom_kind kind;
} drgqst_rom_image;

const char *drgqst_rom_short_name(enum drgqst_rom_kind kind);
int drgqst_rom_is_xavix2(enum drgqst_rom_kind kind);
int drgqst_rom_is_tvpc(enum drgqst_rom_kind kind);

int drgqst_rom_load_zip(
	const wchar_t *path,
	drgqst_rom_image *image,
	wchar_t *error,
	size_t error_length);

void drgqst_rom_release(drgqst_rom_image *image);

#endif
