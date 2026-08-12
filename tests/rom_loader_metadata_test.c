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
	puts("rom_loader_metadata_test: all tests passed");
	return 0;
}
