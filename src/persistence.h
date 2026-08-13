// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#ifndef DRGQST_PLAYER_PERSISTENCE_H
#define DRGQST_PLAYER_PERSISTENCE_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	DRGQST_PERSISTENCE_ROM_SHA1_SIZE = 20,
	DRGQST_PERSISTENCE_EEPROM_SIZE = 1024,
	DRGQST_PERSISTENCE_EEPROM24C16_SIZE = 2048,
	DRGQST_PERSISTENCE_PARALLEL_NVRAM_SIZE = 4096,
	DRGQST_PERSISTENCE_HEADER_SIZE = 40,
	DRGQST_PERSISTENCE_MAX_STATE_SIZE = 128 * 1024 * 1024
};

enum drgqst_persistence_kind
{
	DRGQST_PERSISTENCE_EEPROM = 1,
	DRGQST_PERSISTENCE_RUNTIME_STATE = 2,
	DRGQST_PERSISTENCE_BAN_ONEP_EEPROM = 3,
	DRGQST_PERSISTENCE_BAN_ONEP_RUNTIME_STATE = 4,
	DRGQST_PERSISTENCE_BAN_OMT_EEPROM = 5,
	DRGQST_PERSISTENCE_BAN_OMT_RUNTIME_STATE = 6,
	DRGQST_PERSISTENCE_TTV_LOTR_EEPROM = 7,
	DRGQST_PERSISTENCE_TTV_LOTR_RUNTIME_STATE = 8,
	DRGQST_PERSISTENCE_TTV_SW_EEPROM = 9,
	DRGQST_PERSISTENCE_TTV_SW_RUNTIME_STATE = 10,
	DRGQST_PERSISTENCE_TTV_SWJ_EEPROM = 11,
	DRGQST_PERSISTENCE_TTV_SWJ_RUNTIME_STATE = 12,
	DRGQST_PERSISTENCE_EPO_HAMD_RUNTIME_STATE = 13,
	DRGQST_PERSISTENCE_TVPC_DOR_EEPROM = 14,
	DRGQST_PERSISTENCE_TVPC_DOR_RUNTIME_STATE = 15,
	DRGQST_PERSISTENCE_TTV_MX_EEPROM = 16,
	DRGQST_PERSISTENCE_TTV_MX_RUNTIME_STATE = 17,
	DRGQST_PERSISTENCE_TOM_JUMP_EEPROM = 18,
	DRGQST_PERSISTENCE_TOM_JUMP_RUNTIME_STATE = 19,
	DRGQST_PERSISTENCE_EPO_SDB_NVRAM = 20,
	DRGQST_PERSISTENCE_EPO_SDB_RUNTIME_STATE = 21,
	DRGQST_PERSISTENCE_EPO_BOWL_EEPROM = 22,
	DRGQST_PERSISTENCE_EPO_BOWL_RUNTIME_STATE = 23,
	DRGQST_PERSISTENCE_TAK_CHQ_EEPROM = 24,
	DRGQST_PERSISTENCE_TAK_CHQ_RUNTIME_STATE = 25,
	DRGQST_PERSISTENCE_EPO_EBOX_NVRAM = 26,
	DRGQST_PERSISTENCE_EPO_EBOX_RUNTIME_STATE = 27,
	DRGQST_PERSISTENCE_EPO_ES2J_RUNTIME_STATE = 28,
	DRGQST_PERSISTENCE_EPO_HAMC_RUNTIME_STATE = 29,
	DRGQST_PERSISTENCE_TVPC_HAM_EEPROM = 30,
	DRGQST_PERSISTENCE_TVPC_HAM_RUNTIME_STATE = 31,
	DRGQST_PERSISTENCE_TVPC_HK_EEPROM = 32,
	DRGQST_PERSISTENCE_TVPC_HK_RUNTIME_STATE = 33,
	DRGQST_PERSISTENCE_TOM_DPGM_EEPROM = 34,
	DRGQST_PERSISTENCE_TOM_DPGM_RUNTIME_STATE = 35,
	DRGQST_PERSISTENCE_EPO_MINI_EEPROM = 36,
	DRGQST_PERSISTENCE_EPO_MINI_RUNTIME_STATE = 37,
	DRGQST_PERSISTENCE_BAN_NARU_RUNTIME_STATE = 38,
	DRGQST_PERSISTENCE_BAN_BLDJ_RUNTIME_STATE = 39,
	DRGQST_PERSISTENCE_BAN_DB2J_RUNTIME_STATE = 40,
	DRGQST_PERSISTENCE_BAN_DBZ_RUNTIME_STATE = 41
};

/*
 * On-disk header (40 bytes, little-endian integers):
 *   0  magic "DRGQSAVE"         8  version u16
 *  10  kind u16                 12  ROM SHA-1[20]
 *  32  payload size u32         36  payload CRC32 u32
 */

/*
 * directory_override is a test/integration injection point.  When it is
 * NULL, files live under %LOCALAPPDATA%\DrgqstPlayer for compatibility with
 * older releases.  A non-NULL value is used as the exact directory, allowing
 * the application to place files beside its executable.  Paths must fit the
 * target's standard Win32 MAX_PATH configuration.
 */
int drgqst_persistence_get_directory(
	const wchar_t *directory_override,
	wchar_t *output,
	size_t output_length,
	wchar_t *error,
	size_t error_length);

int drgqst_persistence_get_path(
	const wchar_t *directory_override,
	enum drgqst_persistence_kind kind,
	wchar_t *output,
	size_t output_length,
	wchar_t *error,
	size_t error_length);

/*
 * Save accepts an opaque caller-owned blob.  EEPROM and parallel-NVRAM
 * payloads must match the capacity selected by their kind; runtime states are
 * opaque and independent of core structures.
 */
int drgqst_persistence_save(
	const wchar_t *directory_override,
	enum drgqst_persistence_kind kind,
	const uint8_t rom_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE],
	const void *payload,
	size_t payload_size,
	wchar_t *error,
	size_t error_length);

/*
 * Load reads into private memory, validates the complete header, ROM SHA-1,
 * exact file size and CRC32, and only then copies into the caller's buffer.
 * On failure payload and payload_size are left untouched/zero respectively.
 */
int drgqst_persistence_load(
	const wchar_t *directory_override,
	enum drgqst_persistence_kind kind,
	const uint8_t rom_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE],
	void *payload,
	size_t payload_capacity,
	size_t *payload_size,
	wchar_t *error,
	size_t error_length);

#ifdef __cplusplus
}
#endif

#endif
