// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "rom_loader.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "check failed at line %d: %s\n", \
				__LINE__, #condition); \
			return 1; \
		} \
	} while (0)

int main(void)
{
	CHECK(TAK_CHQ_ROM_SIZE == 4194304);
	CHECK(TAK_CHQ_ROM_CRC32 == UINT32_C(0xffd2eb95));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_TAK_CHQ), "tak_chq"));
	CHECK(!drgqst_rom_is_xavix2(DRGQST_ROM_TAK_CHQ));
	CHECK(EPO_EBOX_ROM_SIZE == 4194304);
	CHECK(EPO_EBOX_ROM_CRC32 == UINT32_C(0xe25ae4f5));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_EPO_EBOX), "epo_ebox"));
	CHECK(!drgqst_rom_is_xavix2(DRGQST_ROM_EPO_EBOX));
	CHECK(EPO_ES2J_ROM_SIZE == 4194304);
	CHECK(EPO_ES2J_ROM_CRC32 == UINT32_C(0x840aecb1));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_EPO_ES2J), "epo_es2j"));
	CHECK(!drgqst_rom_is_xavix2(DRGQST_ROM_EPO_ES2J));
	CHECK(EPO_HAMC_ROM_SIZE == 4194304);
	CHECK(EPO_HAMC_ROM_CRC32 == UINT32_C(0xb1177813));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_EPO_HAMC), "epo_hamc"));
	CHECK(!drgqst_rom_is_xavix2(DRGQST_ROM_EPO_HAMC));
	CHECK(TVPC_HAM_ROM_SIZE == 4194304);
	CHECK(TVPC_HAM_ROM_CRC32 == UINT32_C(0x76e8c854));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_TVPC_HAM), "tvpc_ham"));
	CHECK(!drgqst_rom_is_xavix2(DRGQST_ROM_TVPC_HAM));
	CHECK(drgqst_rom_is_tvpc(DRGQST_ROM_TVPC_HAM));
	CHECK(TVPC_HK_ROM_SIZE == 4194304);
	CHECK(TVPC_HK_ROM_CRC32 == UINT32_C(0x87fc2f73));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_TVPC_HK), "tvpc_hk"));
	CHECK(!drgqst_rom_is_xavix2(DRGQST_ROM_TVPC_HK));
	CHECK(drgqst_rom_is_tvpc(DRGQST_ROM_TVPC_HK));
	CHECK(drgqst_rom_is_tvpc(DRGQST_ROM_TVPC_DOR));
	CHECK(!drgqst_rom_is_tvpc(DRGQST_ROM_EPO_HAMC));
	CHECK(TOM_DPGM_ROM_SIZE == 4194304);
	CHECK(TOM_DPGM_ROM_CRC32 == UINT32_C(0x1dc181b3));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_TOM_DPGM), "tom_dpgm"));
	CHECK(!drgqst_rom_is_xavix2(DRGQST_ROM_TOM_DPGM));
	CHECK(!drgqst_rom_is_tvpc(DRGQST_ROM_TOM_DPGM));
	CHECK(EPO_MINI_ROM_SIZE == 4194304);
	CHECK(EPO_MINI_ROM_CRC32 == UINT32_C(0x2adb01ee));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_EPO_MINI), "epo_mini"));
	CHECK(!drgqst_rom_is_xavix2(DRGQST_ROM_EPO_MINI));
	CHECK(!drgqst_rom_is_tvpc(DRGQST_ROM_EPO_MINI));
	CHECK(EPO_DAB2J_ROM_CRC32 == UINT32_C(0xe3d12ee6));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_EPO_DAB2J), "epo_dab2j"));
	CHECK(drgqst_rom_is_xavix2(DRGQST_ROM_EPO_DAB2J));
	CHECK(EPO_DTCJ_ROM_CRC32 == UINT32_C(0x64c2aabb));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_EPO_DTCJ), "epo_dtcj"));
	CHECK(drgqst_rom_is_xavix2(DRGQST_ROM_EPO_DTCJ));
	CHECK(EPO_PABJ_ROM_CRC32 == UINT32_C(0xac46991c));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_EPO_PABJ), "epo_pabj"));
	CHECK(drgqst_rom_is_xavix2(DRGQST_ROM_EPO_PABJ));
	CHECK(EPO_SSK2_ROM_CRC32 == UINT32_C(0xd5902e48));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_EPO_SSK2), "epo_ssk2"));
	CHECK(drgqst_rom_is_xavix2(DRGQST_ROM_EPO_SSK2));
	CHECK(EPO_SSKJ_ROM_CRC32 == UINT32_C(0x3344b2fc));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_EPO_SSKJ), "epo_sskj"));
	CHECK(drgqst_rom_is_xavix2(DRGQST_ROM_EPO_SSKJ));
	CHECK(RAD_MTRK_ROM_SIZE == 4194304 && RAD_MTRK_ROM_CRC32 == UINT32_C(0xdccda0a7));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_RAD_MTRK), "rad_mtrk"));
	CHECK(RAD_SNOW_ROM_SIZE == 1048576 && RAD_SNOW_ROM_CRC32 == UINT32_C(0x593e40b3));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_RAD_SNOW), "rad_snow"));
	CHECK(RAD_SSX_ROM_CRC32 == UINT32_C(0x108e19a6));
	CHECK(RAD_SBW_ROM_CRC32 == UINT32_C(0x640c1473));
	CHECK(TAK_GIN_ROM_SIZE == 2097152 && TAK_GIN_ROM_CRC32 == UINT32_C(0x79fdeae3));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_TAK_GIN), "tak_gin"));
	CHECK(TCARNAVI_ROM_CRC32 == UINT32_C(0xf4e693fb));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_TCARNAVI), "tcarnavi"));
	CHECK(TOMTHR_ROM_CRC32 == UINT32_C(0xa7e8dc74));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_TOMTHR), "tomthr"));
	CHECK(!drgqst_rom_is_xavix2(DRGQST_ROM_TOMTHR));
	CHECK(EPO_CROK_ROM_SIZE == 4194304 &&
		EPO_CROK_ROM_CRC32 == UINT32_C(0xa801779b));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_EPO_CROK), "epo_crok"));
	CHECK(TAK_ZUBA_ROM_SIZE == 4194304 &&
		TAK_ZUBA_ROM_CRC32 == UINT32_C(0x6d60c8d2));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_TAK_ZUBA), "tak_zuba"));
	CHECK(DUELMAST_ROM_SIZE == 2097152 &&
		DUELMAST_ROM_CRC32 == UINT32_C(0x2f11fcd7));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_DUELMAST), "duelmast"));
	CHECK(EPO_GOLF_ROM_SIZE == 4194304 &&
		EPO_GOLF_ROM_CRC32 == UINT32_C(0xd1f231cf));
	CHECK(!strcmp(drgqst_rom_short_name(DRGQST_ROM_EPO_GOLF), "epo_golf"));	puts("rom_loader_metadata_test: all tests passed");
	return 0;
}
