// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "persistence.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static unsigned failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

static int buffer_is_filled(const uint8_t *buffer, size_t size, uint8_t value)
{
	size_t index;
	for (index = 0; index < size; index++)
	{
		if (buffer[index] != value)
			return 0;
	}
	return 1;
}

static int create_test_root(wchar_t path[MAX_PATH])
{
	wchar_t temporary_directory[MAX_PATH];
	DWORD length;

	length = GetTempPathW(MAX_PATH, temporary_directory);
	if (!length || length >= MAX_PATH)
		return 0;
	if (!GetTempFileNameW(temporary_directory, L"dqp", 0, path))
		return 0;
	if (!DeleteFileW(path))
		return 0;
	return !!CreateDirectoryW(path, NULL);
}

static int change_file_byte(const wchar_t *path, LONGLONG offset)
{
	HANDLE file;
	LARGE_INTEGER position;
	uint8_t byte;
	DWORD transferred;
	int success = 0;

	file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return 0;
	position.QuadPart = offset;
	if (!SetFilePointerEx(file, position, NULL, FILE_BEGIN))
		goto cleanup;
	if (!ReadFile(file, &byte, 1, &transferred, NULL) || transferred != 1)
		goto cleanup;
	byte ^= 0x5a;
	if (!SetFilePointerEx(file, position, NULL, FILE_BEGIN))
		goto cleanup;
	if (!WriteFile(file, &byte, 1, &transferred, NULL) || transferred != 1)
		goto cleanup;
	if (!FlushFileBuffers(file))
		goto cleanup;
	success = 1;

cleanup:
	CloseHandle(file);
	return success;
}

static int truncate_last_byte(const wchar_t *path)
{
	HANDLE file;
	LARGE_INTEGER size;
	int success = 0;

	file = CreateFileW(path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return 0;
	if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0)
		goto cleanup;
	size.QuadPart--;
	if (!SetFilePointerEx(file, size, NULL, FILE_BEGIN) || !SetEndOfFile(file) || !FlushFileBuffers(file))
		goto cleanup;
	success = 1;

cleanup:
	CloseHandle(file);
	return success;
}

static int read_file_prefix(const wchar_t *path, uint8_t *data, DWORD size)
{
	HANDLE file;
	DWORD received = 0;
	int success;

	file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return 0;
	success = ReadFile(file, data, size, &received, NULL) && received == size;
	CloseHandle(file);
	return success;
}

static int has_temporary_files(const wchar_t *directory)
{
	static const wchar_t pattern[] = L"\\*.tmp-*";
	wchar_t search[MAX_PATH];
	WIN32_FIND_DATAW data;
	HANDLE find;
	size_t directory_length = wcslen(directory);

	if (directory_length + sizeof(pattern) / sizeof(pattern[0]) > MAX_PATH)
		return 1;
	memcpy(search, directory, directory_length * sizeof(*search));
	memcpy(search + directory_length, pattern, sizeof(pattern));
	find = FindFirstFileW(search, &data);
	if (find == INVALID_HANDLE_VALUE)
		return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : 1;
	FindClose(find);
	return 1;
}

static void remove_test_tree(const wchar_t *root)
{
	wchar_t error[256];
	wchar_t eeprom_path[MAX_PATH];
	wchar_t state_path[MAX_PATH];
	wchar_t ban_eeprom_path[MAX_PATH];
	wchar_t ban_state_path[MAX_PATH];
	wchar_t omt_eeprom_path[MAX_PATH];
	wchar_t omt_state_path[MAX_PATH];
	wchar_t lotr_eeprom_path[MAX_PATH];
	wchar_t lotr_state_path[MAX_PATH];
	wchar_t sw_eeprom_path[MAX_PATH];
	wchar_t sw_state_path[MAX_PATH];
	wchar_t swj_eeprom_path[MAX_PATH];
	wchar_t swj_state_path[MAX_PATH];
	wchar_t hamd_state_path[MAX_PATH];
	wchar_t dor_eeprom_path[MAX_PATH];
	wchar_t dor_state_path[MAX_PATH];
	wchar_t directory[MAX_PATH];

	if (drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_EEPROM,
		eeprom_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])))
		DeleteFileW(eeprom_path);
	if (drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_RUNTIME_STATE,
		state_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])))
		DeleteFileW(state_path);
	if (drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_BAN_ONEP_EEPROM,
		ban_eeprom_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])))
		DeleteFileW(ban_eeprom_path);
	if (drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_BAN_ONEP_RUNTIME_STATE,
		ban_state_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])))
		DeleteFileW(ban_state_path);
	if (drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_BAN_OMT_EEPROM,
		omt_eeprom_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])))
		DeleteFileW(omt_eeprom_path);
	if (drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_BAN_OMT_RUNTIME_STATE,
		omt_state_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])))
		DeleteFileW(omt_state_path);
	if (drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_EPO_HAMD_RUNTIME_STATE,
		hamd_state_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])))
		DeleteFileW(hamd_state_path);
	if (drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_TVPC_DOR_EEPROM,
		dor_eeprom_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])))
		DeleteFileW(dor_eeprom_path);
	if (drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_TVPC_DOR_RUNTIME_STATE,
		dor_state_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])))
		DeleteFileW(dor_state_path);
	if (drgqst_persistence_get_directory(root, directory, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])))
	{
		if (wcscmp(directory, root))
			RemoveDirectoryW(directory);
	}
	RemoveDirectoryW(root);
}

static void test_default_directory(void)
{
	wchar_t directory[32767];
	wchar_t error[256];
	static const wchar_t suffix[] = L"\\DrgqstPlayer";
	size_t directory_length;
	size_t suffix_length = sizeof(suffix) / sizeof(suffix[0]) - 1;

	if (!drgqst_persistence_get_directory(NULL, directory,
		sizeof(directory) / sizeof(directory[0]), error, sizeof(error) / sizeof(error[0])))
	{
		CHECK(0 && "drgqst_persistence_get_directory");
		return;
	}
	directory_length = wcslen(directory);
	CHECK(directory_length >= suffix_length);
	if (directory_length >= suffix_length)
		CHECK(!wcscmp(directory + directory_length - suffix_length, suffix));
}

static void test_persistence(void)
{
	wchar_t root[MAX_PATH];
	wchar_t directory[MAX_PATH];
	wchar_t eeprom_path[MAX_PATH];
	wchar_t state_path[MAX_PATH];
	wchar_t ban_eeprom_path[MAX_PATH];
	wchar_t ban_state_path[MAX_PATH];
	wchar_t omt_eeprom_path[MAX_PATH];
	wchar_t omt_state_path[MAX_PATH];
	wchar_t lotr_eeprom_path[MAX_PATH];
	wchar_t lotr_state_path[MAX_PATH];
	wchar_t sw_eeprom_path[MAX_PATH];
	wchar_t sw_state_path[MAX_PATH];
	wchar_t swj_eeprom_path[MAX_PATH];
	wchar_t swj_state_path[MAX_PATH];
	wchar_t hamd_state_path[MAX_PATH];
	wchar_t dor_eeprom_path[MAX_PATH];
	wchar_t dor_state_path[MAX_PATH];
	wchar_t error[512];
	uint8_t rom_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE];
	uint8_t wrong_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE];
	uint8_t eeprom[DRGQST_PERSISTENCE_EEPROM_SIZE];
	uint8_t eeprom_output[DRGQST_PERSISTENCE_EEPROM_SIZE];
	uint8_t eeprom24c16[DRGQST_PERSISTENCE_EEPROM24C16_SIZE];
	uint8_t eeprom24c16_output[DRGQST_PERSISTENCE_EEPROM24C16_SIZE];
	uint8_t state[733];
	uint8_t second_state[733];
	uint8_t state_output[733];
	static const uint8_t crc_vector[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
	uint8_t header[DRGQST_PERSISTENCE_HEADER_SIZE];
	size_t loaded_size = 0;
	size_t index;

	if (!create_test_root(root))
	{
		CHECK(0 && "create_test_root");
		return;
	}
	for (index = 0; index < sizeof(rom_sha1); index++)
		rom_sha1[index] = (uint8_t)(index * 7 + 3);
	memcpy(wrong_sha1, rom_sha1, sizeof(wrong_sha1));
	wrong_sha1[9] ^= 0x80;
	for (index = 0; index < sizeof(eeprom); index++)
		eeprom[index] = (uint8_t)(index ^ (index >> 4));
	for (index = 0; index < sizeof(eeprom24c16); index++)
		eeprom24c16[index] = (uint8_t)(index * 5 + (index >> 7));
	for (index = 0; index < sizeof(state); index++)
	{
		state[index] = (uint8_t)(index * 13 + 0x31);
		second_state[index] = (uint8_t)(state[index] ^ 0xa5);
	}

	if (!drgqst_persistence_get_directory(root, directory, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])) ||
		!drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_EEPROM,
		eeprom_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])) ||
		!drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_RUNTIME_STATE,
		state_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])) ||
		!drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_BAN_ONEP_EEPROM,
		ban_eeprom_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])) ||
		!drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_BAN_ONEP_RUNTIME_STATE,
		ban_state_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])) ||
		!drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_BAN_OMT_EEPROM,
		omt_eeprom_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])) ||
		!drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_BAN_OMT_RUNTIME_STATE,
		omt_state_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])) ||
		!drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_TTV_LOTR_EEPROM,
		lotr_eeprom_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])) ||
		!drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_TTV_LOTR_RUNTIME_STATE,
		lotr_state_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])) ||
		!drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_TTV_SW_EEPROM,
		sw_eeprom_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])) ||
		!drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_TTV_SW_RUNTIME_STATE,
		sw_state_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])) ||
		!drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_TTV_SWJ_EEPROM,
		swj_eeprom_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])) ||
		!drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_TTV_SWJ_RUNTIME_STATE,
		swj_state_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])) ||
		!drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_EPO_HAMD_RUNTIME_STATE,
		hamd_state_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])) ||
		!drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_TVPC_DOR_EEPROM,
		dor_eeprom_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])) ||
		!drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_TVPC_DOR_RUNTIME_STATE,
		dor_state_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])))
	{
		CHECK(0 && "resolve persistence paths");
		remove_test_tree(root);
		return;
	}
	CHECK(wcscmp(eeprom_path, state_path) != 0);
	CHECK(!wcscmp(directory, root));
	CHECK(!wcscmp(eeprom_path + wcslen(root), L"\\eeprom.sav"));
	CHECK(!wcscmp(state_path + wcslen(root), L"\\runtime-state.sav"));
	CHECK(!wcscmp(ban_eeprom_path + wcslen(root), L"\\ban_onep-eeprom.sav"));
	CHECK(!wcscmp(ban_state_path + wcslen(root), L"\\ban_onep-runtime-state.sav"));
	CHECK(!wcscmp(omt_eeprom_path + wcslen(root), L"\\ban_omt-eeprom.sav"));
	CHECK(!wcscmp(omt_state_path + wcslen(root), L"\\ban_omt-runtime-state.sav"));
	CHECK(!wcscmp(lotr_eeprom_path + wcslen(root), L"\\ttv_lotr-eeprom.sav"));
	CHECK(!wcscmp(lotr_state_path + wcslen(root), L"\\ttv_lotr-runtime-state.sav"));
	CHECK(!wcscmp(sw_eeprom_path + wcslen(root), L"\\ttv_sw-eeprom.sav"));
	CHECK(!wcscmp(sw_state_path + wcslen(root), L"\\ttv_sw-runtime-state.sav"));
	CHECK(!wcscmp(swj_eeprom_path + wcslen(root), L"\\ttv_swj-eeprom.sav"));
	CHECK(!wcscmp(swj_state_path + wcslen(root), L"\\ttv_swj-runtime-state.sav"));
	CHECK(!wcscmp(hamd_state_path + wcslen(root), L"\\epo_hamd-runtime-state.sav"));
	CHECK(!wcscmp(dor_eeprom_path + wcslen(root), L"\\tvpc_dor-eeprom.sav"));
	CHECK(!wcscmp(dor_state_path + wcslen(root), L"\\tvpc_dor-runtime-state.sav"));
	CHECK(wcscmp(eeprom_path, ban_eeprom_path) != 0);
	CHECK(wcscmp(state_path, ban_state_path) != 0);
	CHECK(wcscmp(ban_eeprom_path, omt_eeprom_path) != 0);
	CHECK(wcscmp(ban_state_path, omt_state_path) != 0);

	CHECK(!drgqst_persistence_save(root, DRGQST_PERSISTENCE_EEPROM, rom_sha1,
		eeprom, sizeof(eeprom) - 1, error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root, DRGQST_PERSISTENCE_EEPROM, rom_sha1,
		eeprom, sizeof(eeprom), error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root, DRGQST_PERSISTENCE_RUNTIME_STATE, rom_sha1,
		crc_vector, sizeof(crc_vector), error, sizeof(error) / sizeof(error[0])));
	CHECK(read_file_prefix(state_path, header, sizeof(header)));
	CHECK(!memcmp(header, "DRGQSAVE", 8));
	CHECK(header[8] == 1 && header[9] == 0);
	CHECK(header[10] == DRGQST_PERSISTENCE_RUNTIME_STATE && header[11] == 0);
	CHECK(!memcmp(header + 12, rom_sha1, sizeof(rom_sha1)));
	CHECK(header[32] == sizeof(crc_vector) && header[33] == 0 && header[34] == 0 && header[35] == 0);
	CHECK(header[36] == 0x26 && header[37] == 0x39 && header[38] == 0xf4 && header[39] == 0xcb);
	CHECK(drgqst_persistence_save(root, DRGQST_PERSISTENCE_RUNTIME_STATE, rom_sha1,
		state, sizeof(state), error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root, DRGQST_PERSISTENCE_BAN_OMT_EEPROM,
		rom_sha1, eeprom, sizeof(eeprom), error,
		sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root, DRGQST_PERSISTENCE_BAN_OMT_RUNTIME_STATE,
		rom_sha1, second_state, sizeof(second_state), error,
		sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root, DRGQST_PERSISTENCE_EPO_HAMD_RUNTIME_STATE,
		rom_sha1, state, sizeof(state), error,
		sizeof(error) / sizeof(error[0])));
	CHECK(!drgqst_persistence_save(root, DRGQST_PERSISTENCE_TVPC_DOR_EEPROM,
		rom_sha1, eeprom24c16, sizeof(eeprom24c16) - 1, error,
		sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root, DRGQST_PERSISTENCE_TVPC_DOR_EEPROM,
		rom_sha1, eeprom24c16, sizeof(eeprom24c16), error,
		sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root, DRGQST_PERSISTENCE_TVPC_DOR_RUNTIME_STATE,
		rom_sha1, second_state, sizeof(second_state), error,
		sizeof(error) / sizeof(error[0])));
	CHECK(GetFileAttributesW(eeprom_path) != INVALID_FILE_ATTRIBUTES);
	CHECK(GetFileAttributesW(state_path) != INVALID_FILE_ATTRIBUTES);
	CHECK(GetFileAttributesW(omt_eeprom_path) != INVALID_FILE_ATTRIBUTES);
	CHECK(GetFileAttributesW(omt_state_path) != INVALID_FILE_ATTRIBUTES);
	CHECK(GetFileAttributesW(hamd_state_path) != INVALID_FILE_ATTRIBUTES);
	CHECK(GetFileAttributesW(dor_eeprom_path) != INVALID_FILE_ATTRIBUTES);
	CHECK(GetFileAttributesW(dor_state_path) != INVALID_FILE_ATTRIBUTES);
	CHECK(!(GetFileAttributesW(eeprom_path) & FILE_ATTRIBUTE_TEMPORARY));
	CHECK(!(GetFileAttributesW(state_path) & FILE_ATTRIBUTE_TEMPORARY));
	CHECK(!has_temporary_files(directory));

	memset(eeprom_output, 0, sizeof(eeprom_output));
	CHECK(drgqst_persistence_load(root, DRGQST_PERSISTENCE_EEPROM, rom_sha1,
		eeprom_output, sizeof(eeprom_output), &loaded_size, error, sizeof(error) / sizeof(error[0])));
	CHECK(loaded_size == sizeof(eeprom));
	CHECK(!memcmp(eeprom, eeprom_output, sizeof(eeprom)));
	memset(state_output, 0, sizeof(state_output));
	CHECK(drgqst_persistence_load(root, DRGQST_PERSISTENCE_RUNTIME_STATE, rom_sha1,
		state_output, sizeof(state_output), &loaded_size, error, sizeof(error) / sizeof(error[0])));
	CHECK(loaded_size == sizeof(state));
	CHECK(!memcmp(state, state_output, sizeof(state)));
	memset(state_output, 0, sizeof(state_output));
	CHECK(drgqst_persistence_load(root, DRGQST_PERSISTENCE_BAN_OMT_RUNTIME_STATE,
		rom_sha1, state_output, sizeof(state_output), &loaded_size, error,
		sizeof(error) / sizeof(error[0])));
	CHECK(loaded_size == sizeof(second_state));
	CHECK(!memcmp(second_state, state_output, sizeof(second_state)));
	memset(eeprom24c16_output, 0, sizeof(eeprom24c16_output));
	CHECK(drgqst_persistence_load(root, DRGQST_PERSISTENCE_TVPC_DOR_EEPROM,
		rom_sha1, eeprom24c16_output, sizeof(eeprom24c16_output), &loaded_size,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(loaded_size == sizeof(eeprom24c16));
	CHECK(!memcmp(eeprom24c16, eeprom24c16_output, sizeof(eeprom24c16)));
	memset(state_output, 0xcc, sizeof(state_output));
	loaded_size = 99;
	CHECK(!drgqst_persistence_load(root, DRGQST_PERSISTENCE_RUNTIME_STATE, rom_sha1,
		state_output, sizeof(state_output) - 1, &loaded_size, error, sizeof(error) / sizeof(error[0])));
	CHECK(loaded_size == 0);
	CHECK(buffer_is_filled(state_output, sizeof(state_output), 0xcc));

	/* Replacing the state file must not modify the EEPROM file. */
	CHECK(drgqst_persistence_save(root, DRGQST_PERSISTENCE_RUNTIME_STATE, rom_sha1,
		second_state, sizeof(second_state), error, sizeof(error) / sizeof(error[0])));
	memset(eeprom_output, 0, sizeof(eeprom_output));
	CHECK(drgqst_persistence_load(root, DRGQST_PERSISTENCE_EEPROM, rom_sha1,
		eeprom_output, sizeof(eeprom_output), &loaded_size, error, sizeof(error) / sizeof(error[0])));
	CHECK(!memcmp(eeprom, eeprom_output, sizeof(eeprom)));
	memset(state_output, 0, sizeof(state_output));
	CHECK(drgqst_persistence_load(root, DRGQST_PERSISTENCE_RUNTIME_STATE, rom_sha1,
		state_output, sizeof(state_output), &loaded_size, error, sizeof(error) / sizeof(error[0])));
	CHECK(!memcmp(second_state, state_output, sizeof(second_state)));

	/* A failed atomic rename must preserve the old state and remove its temp. */
	{
		HANDLE locked_state = CreateFileW(
			state_path,
			GENERIC_READ,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL);
		CHECK(locked_state != INVALID_HANDLE_VALUE);
		if (locked_state != INVALID_HANDLE_VALUE)
		{
			CHECK(!drgqst_persistence_save(root, DRGQST_PERSISTENCE_RUNTIME_STATE, rom_sha1,
				state, sizeof(state), error, sizeof(error) / sizeof(error[0])));
			CloseHandle(locked_state);
			memset(state_output, 0, sizeof(state_output));
			CHECK(drgqst_persistence_load(root, DRGQST_PERSISTENCE_RUNTIME_STATE, rom_sha1,
				state_output, sizeof(state_output), &loaded_size, error, sizeof(error) / sizeof(error[0])));
			CHECK(!memcmp(second_state, state_output, sizeof(second_state)));
			CHECK(!has_temporary_files(directory));
		}
	}

	/* A ROM mismatch must not expose any payload bytes. */
	memset(eeprom_output, 0xcc, sizeof(eeprom_output));
	loaded_size = 99;
	CHECK(!drgqst_persistence_load(root, DRGQST_PERSISTENCE_EEPROM, wrong_sha1,
		eeprom_output, sizeof(eeprom_output), &loaded_size, error, sizeof(error) / sizeof(error[0])));
	CHECK(loaded_size == 0);
	CHECK(buffer_is_filled(eeprom_output, sizeof(eeprom_output), 0xcc));

	/* Header corruption is rejected before exposing any payload bytes. */
	CHECK(change_file_byte(state_path, 0));
	memset(state_output, 0xcc, sizeof(state_output));
	loaded_size = 99;
	CHECK(!drgqst_persistence_load(root, DRGQST_PERSISTENCE_RUNTIME_STATE, rom_sha1,
		state_output, sizeof(state_output), &loaded_size, error, sizeof(error) / sizeof(error[0])));
	CHECK(loaded_size == 0);
	CHECK(buffer_is_filled(state_output, sizeof(state_output), 0xcc));
	CHECK(drgqst_persistence_save(root, DRGQST_PERSISTENCE_RUNTIME_STATE, rom_sha1,
		second_state, sizeof(second_state), error, sizeof(error) / sizeof(error[0])));

	/* Corrupting the payload must fail CRC validation without partial output. */
	CHECK(change_file_byte(state_path, DRGQST_PERSISTENCE_HEADER_SIZE + 17));
	memset(state_output, 0xcc, sizeof(state_output));
	loaded_size = 99;
	CHECK(!drgqst_persistence_load(root, DRGQST_PERSISTENCE_RUNTIME_STATE, rom_sha1,
		state_output, sizeof(state_output), &loaded_size, error, sizeof(error) / sizeof(error[0])));
	CHECK(loaded_size == 0);
	CHECK(buffer_is_filled(state_output, sizeof(state_output), 0xcc));

	/* Restore, truncate one byte, and verify exact file-size validation. */
	CHECK(drgqst_persistence_save(root, DRGQST_PERSISTENCE_RUNTIME_STATE, rom_sha1,
		state, sizeof(state), error, sizeof(error) / sizeof(error[0])));
	CHECK(truncate_last_byte(state_path));
	memset(state_output, 0xcc, sizeof(state_output));
	loaded_size = 99;
	CHECK(!drgqst_persistence_load(root, DRGQST_PERSISTENCE_RUNTIME_STATE, rom_sha1,
		state_output, sizeof(state_output), &loaded_size, error, sizeof(error) / sizeof(error[0])));
	CHECK(loaded_size == 0);
	CHECK(buffer_is_filled(state_output, sizeof(state_output), 0xcc));

	remove_test_tree(root);
}

static void test_xavix2000_24c04_persistence_kinds(void)
{
	wchar_t root[MAX_PATH];
	wchar_t mx_eeprom_path[MAX_PATH] = { 0 };
	wchar_t mx_state_path[MAX_PATH] = { 0 };
	wchar_t jump_eeprom_path[MAX_PATH] = { 0 };
	wchar_t jump_state_path[MAX_PATH] = { 0 };
	wchar_t bowl_eeprom_path[MAX_PATH] = { 0 };
	wchar_t bowl_state_path[MAX_PATH] = { 0 };
	wchar_t tak_eeprom_path[MAX_PATH] = { 0 };
	wchar_t tak_state_path[MAX_PATH] = { 0 };
	wchar_t error[512];
	uint8_t rom_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] = { 0 };
	uint8_t eeprom[DRGQST_PERSISTENCE_EEPROM_SIZE];
	uint8_t output[DRGQST_PERSISTENCE_EEPROM_SIZE];
	uint8_t state[3] = { 0x58, 0x32, 0x4b };
	size_t output_size = 0;

	if (!create_test_root(root))
	{
		CHECK(0 && "create 24C04 persistence root");
		return;
	}
	memset(eeprom, 0xff, sizeof(eeprom));
	rom_sha1[0] = 0x24;
	rom_sha1[1] = 0xc0;
	rom_sha1[2] = 0x04;
	CHECK(drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_TTV_MX_EEPROM,
		mx_eeprom_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_TTV_MX_RUNTIME_STATE, mx_state_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_TOM_JUMP_EEPROM,
		jump_eeprom_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_TOM_JUMP_RUNTIME_STATE, jump_state_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_EPO_BOWL_EEPROM,
		bowl_eeprom_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_EPO_BOWL_RUNTIME_STATE, bowl_state_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_get_path(root, DRGQST_PERSISTENCE_TAK_CHQ_EEPROM,
		tak_eeprom_path, MAX_PATH, error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_TAK_CHQ_RUNTIME_STATE, tak_state_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(wcsstr(mx_eeprom_path, L"\\ttv_mx-eeprom.sav") != NULL);
	CHECK(wcsstr(mx_state_path, L"\\ttv_mx-runtime-state.sav") != NULL);
	CHECK(wcsstr(jump_eeprom_path, L"\\tom_jump-eeprom.sav") != NULL);
	CHECK(wcsstr(jump_state_path, L"\\tom_jump-runtime-state.sav") != NULL);
	CHECK(wcsstr(bowl_eeprom_path, L"\\epo_bowl-eeprom.sav") != NULL);
	CHECK(wcsstr(bowl_state_path, L"\\epo_bowl-runtime-state.sav") != NULL);
	CHECK(wcsstr(tak_eeprom_path, L"\\tak_chq-eeprom.sav") != NULL);
	CHECK(wcsstr(tak_state_path, L"\\tak_chq-runtime-state.sav") != NULL);
	CHECK(wcscmp(mx_eeprom_path, jump_eeprom_path) != 0);
	CHECK(wcscmp(mx_eeprom_path, bowl_eeprom_path) != 0);
	CHECK(wcscmp(jump_eeprom_path, bowl_eeprom_path) != 0);
	CHECK(wcscmp(tak_eeprom_path, mx_eeprom_path) != 0);
	CHECK(wcscmp(tak_eeprom_path, jump_eeprom_path) != 0);
	CHECK(wcscmp(tak_eeprom_path, bowl_eeprom_path) != 0);
	CHECK(drgqst_persistence_save(root, DRGQST_PERSISTENCE_TTV_MX_EEPROM,
		rom_sha1, eeprom, sizeof(eeprom), error,
		sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_TOM_JUMP_RUNTIME_STATE, rom_sha1,
		state, sizeof(state), error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_load(root, DRGQST_PERSISTENCE_TTV_MX_EEPROM,
		rom_sha1, output, sizeof(output), &output_size, error,
		sizeof(error) / sizeof(error[0])));
	CHECK(output_size == sizeof(eeprom));
	CHECK(!memcmp(output, eeprom, sizeof(eeprom)));
	eeprom[0] = 0xb0;
	CHECK(drgqst_persistence_save(root, DRGQST_PERSISTENCE_EPO_BOWL_EEPROM,
		rom_sha1, eeprom, sizeof(eeprom), error,
		sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_EPO_BOWL_RUNTIME_STATE, rom_sha1,
		state, sizeof(state), error, sizeof(error) / sizeof(error[0])));
	memset(output, 0, sizeof(output));
	output_size = 0;
	CHECK(drgqst_persistence_load(root, DRGQST_PERSISTENCE_EPO_BOWL_EEPROM,
		rom_sha1, output, sizeof(output), &output_size, error,
		sizeof(error) / sizeof(error[0])));
	CHECK(output_size == sizeof(eeprom));
	CHECK(!memcmp(output, eeprom, sizeof(eeprom)));
	eeprom[0] = 0x51;
	CHECK(drgqst_persistence_save(root, DRGQST_PERSISTENCE_TAK_CHQ_EEPROM,
		rom_sha1, eeprom, sizeof(eeprom), error,
		sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_TAK_CHQ_RUNTIME_STATE, rom_sha1,
		state, sizeof(state), error, sizeof(error) / sizeof(error[0])));
	memset(output, 0, sizeof(output));
	output_size = 0;
	CHECK(drgqst_persistence_load(root, DRGQST_PERSISTENCE_TAK_CHQ_EEPROM,
		rom_sha1, output, sizeof(output), &output_size, error,
		sizeof(error) / sizeof(error[0])));
	CHECK(output_size == sizeof(eeprom));
	CHECK(!memcmp(output, eeprom, sizeof(eeprom)));
	/* A copied file must not cross-load under another game's kind even when
	 * the payload size and ROM digest supplied by the caller match. */
	CHECK(CopyFileW(tak_eeprom_path, mx_eeprom_path, FALSE));
	memset(output, 0xcc, sizeof(output));
	output_size = 99;
	CHECK(!drgqst_persistence_load(root, DRGQST_PERSISTENCE_TTV_MX_EEPROM,
		rom_sha1, output, sizeof(output), &output_size, error,
		sizeof(error) / sizeof(error[0])));
	CHECK(output_size == 0);
	CHECK(buffer_is_filled(output, sizeof(output), 0xcc));

	if (*mx_eeprom_path)
		DeleteFileW(mx_eeprom_path);
	if (*mx_state_path)
		DeleteFileW(mx_state_path);
	if (*jump_eeprom_path)
		DeleteFileW(jump_eeprom_path);
	if (*jump_state_path)
		DeleteFileW(jump_state_path);
	if (*bowl_eeprom_path)
		DeleteFileW(bowl_eeprom_path);
	if (*bowl_state_path)
		DeleteFileW(bowl_state_path);
	if (*tak_eeprom_path)
		DeleteFileW(tak_eeprom_path);
	if (*tak_state_path)
		DeleteFileW(tak_state_path);
	RemoveDirectoryW(root);
}

static void test_epo_sdb_parallel_nvram_persistence(void)
{
	wchar_t root[MAX_PATH];
	wchar_t nvram_path[MAX_PATH] = { 0 };
	wchar_t state_path[MAX_PATH] = { 0 };
	wchar_t error[512];
	uint8_t rom_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] = { 0 };
	uint8_t nvram[DRGQST_PERSISTENCE_PARALLEL_NVRAM_SIZE];
	uint8_t output[DRGQST_PERSISTENCE_PARALLEL_NVRAM_SIZE];
	uint8_t state[4] = { 0x53, 0x44, 0x42, 0x01 };
	size_t output_size = 0;
	size_t index;

	if (!create_test_root(root))
	{
		CHECK(0 && "create epo_sdb persistence root");
		return;
	}
	for (index = 0; index < sizeof(nvram); ++index)
		nvram[index] = (uint8_t)(index ^ (index >> 5));
	rom_sha1[0] = 0x47;
	rom_sha1[1] = 0xa9;
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_EPO_SDB_NVRAM, nvram_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_EPO_SDB_RUNTIME_STATE, state_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(wcsstr(nvram_path, L"\\epo_sdb-nvram.sav") != NULL);
	CHECK(wcsstr(state_path, L"\\epo_sdb-runtime-state.sav") != NULL);
	CHECK(!drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_EPO_SDB_NVRAM, rom_sha1, nvram,
		sizeof(nvram) - 1, error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_EPO_SDB_NVRAM, rom_sha1, nvram,
		sizeof(nvram), error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_EPO_SDB_RUNTIME_STATE, rom_sha1, state,
		sizeof(state), error, sizeof(error) / sizeof(error[0])));
	memset(output, 0, sizeof(output));
	CHECK(drgqst_persistence_load(root,
		DRGQST_PERSISTENCE_EPO_SDB_NVRAM, rom_sha1, output,
		sizeof(output), &output_size, error,
		sizeof(error) / sizeof(error[0])));
	CHECK(output_size == sizeof(nvram));
	CHECK(!memcmp(output, nvram, sizeof(nvram)));

	if (*nvram_path)
		DeleteFileW(nvram_path);
	if (*state_path)
		DeleteFileW(state_path);
	RemoveDirectoryW(root);
}

static void test_epo_ebox_parallel_nvram_persistence(void)
{
	wchar_t root[MAX_PATH];
	wchar_t nvram_path[MAX_PATH] = { 0 };
	wchar_t state_path[MAX_PATH] = { 0 };
	wchar_t error[512];
	uint8_t rom_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] = { 0 };
	uint8_t nvram[DRGQST_PERSISTENCE_PARALLEL_NVRAM_SIZE];
	uint8_t output[DRGQST_PERSISTENCE_PARALLEL_NVRAM_SIZE];
	uint8_t state[4] = { 0x45, 0x42, 0x4f, 0x58 };
	size_t output_size = 0;
	size_t index;

	if (!create_test_root(root))
	{
		CHECK(0 && "create epo_ebox persistence root");
		return;
	}
	for (index = 0; index < sizeof(nvram); ++index)
		nvram[index] = (uint8_t)(0xa5 ^ index ^ (index >> 3));
	rom_sha1[0] = 0x7f;
	rom_sha1[1] = 0x7b;
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_EPO_EBOX_NVRAM, nvram_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_EPO_EBOX_RUNTIME_STATE, state_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(wcsstr(nvram_path, L"\\epo_ebox-nvram.sav") != NULL);
	CHECK(wcsstr(state_path, L"\\epo_ebox-runtime-state.sav") != NULL);
	CHECK(!drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_EPO_EBOX_NVRAM, rom_sha1, nvram,
		sizeof(nvram) - 1, error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_EPO_EBOX_NVRAM, rom_sha1, nvram,
		sizeof(nvram), error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_EPO_EBOX_RUNTIME_STATE, rom_sha1, state,
		sizeof(state), error, sizeof(error) / sizeof(error[0])));
	memset(output, 0, sizeof(output));
	CHECK(drgqst_persistence_load(root,
		DRGQST_PERSISTENCE_EPO_EBOX_NVRAM, rom_sha1, output,
		sizeof(output), &output_size, error,
		sizeof(error) / sizeof(error[0])));
	CHECK(output_size == sizeof(nvram));
	CHECK(!memcmp(output, nvram, sizeof(nvram)));

	if (*nvram_path)
		DeleteFileW(nvram_path);
	if (*state_path)
		DeleteFileW(state_path);
	RemoveDirectoryW(root);
}

static void test_epo_es2j_runtime_state_persistence(void)
{
	wchar_t root[MAX_PATH];
	wchar_t state_path[MAX_PATH] = { 0 };
	wchar_t error[512];
	uint8_t rom_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] = { 0 };
	uint8_t state[5] = { 0x45, 0x53, 0x32, 0x4a, 0x01 };
	uint8_t output[sizeof(state)] = { 0 };
	size_t output_size = 0;

	if (!create_test_root(root))
	{
		CHECK(0 && "create epo_es2j persistence root");
		return;
	}
	rom_sha1[0] = 0xad;
	rom_sha1[1] = 0x52;
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_EPO_ES2J_RUNTIME_STATE, state_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(wcsstr(state_path, L"\\epo_es2j-runtime-state.sav") != NULL);
	CHECK(!drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_EPO_ES2J_RUNTIME_STATE, rom_sha1, state, 0,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_EPO_ES2J_RUNTIME_STATE, rom_sha1, state,
		sizeof(state), error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_load(root,
		DRGQST_PERSISTENCE_EPO_ES2J_RUNTIME_STATE, rom_sha1, output,
		sizeof(output), &output_size, error,
		sizeof(error) / sizeof(error[0])));
	CHECK(output_size == sizeof(state));
	CHECK(!memcmp(output, state, sizeof(state)));

	if (*state_path)
		DeleteFileW(state_path);
	RemoveDirectoryW(root);
}

static void test_epo_hamc_runtime_state_persistence(void)
{
	wchar_t root[MAX_PATH];
	wchar_t state_path[MAX_PATH] = { 0 };
	wchar_t error[512];
	uint8_t rom_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] = { 0 };
	uint8_t state[5] = { 0x48, 0x41, 0x4d, 0x43, 0x01 };
	uint8_t output[sizeof(state)] = { 0 };
	size_t output_size = 0;

	if (!create_test_root(root))
	{
		CHECK(0 && "create epo_hamc persistence root");
		return;
	}
	rom_sha1[0] = 0xed;
	rom_sha1[1] = 0x01;
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_EPO_HAMC_RUNTIME_STATE, state_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(wcsstr(state_path, L"\\epo_hamc-runtime-state.sav") != NULL);
	CHECK(!drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_EPO_HAMC_RUNTIME_STATE, rom_sha1, state, 0,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_EPO_HAMC_RUNTIME_STATE, rom_sha1, state,
		sizeof(state), error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_load(root,
		DRGQST_PERSISTENCE_EPO_HAMC_RUNTIME_STATE, rom_sha1, output,
		sizeof(output), &output_size, error,
		sizeof(error) / sizeof(error[0])));
	CHECK(output_size == sizeof(state));
	CHECK(!memcmp(output, state, sizeof(state)));

	if (*state_path)
		DeleteFileW(state_path);
	RemoveDirectoryW(root);
}

static void test_tvpc_pair_persistence(void)
{
	static const uint8_t ham_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
	{
		0x59, 0x98, 0xc0, 0x32, 0x92, 0xa1, 0x61, 0x07, 0xd0, 0xd7,
		0xae, 0x00, 0xf7, 0x76, 0x77, 0x58, 0x26, 0x80, 0xf3, 0x23
	};
	static const uint8_t hk_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
	{
		0x29, 0xa2, 0x84, 0xb9, 0x07, 0xab, 0xec, 0x17, 0x5d, 0x42,
		0x89, 0xd2, 0x90, 0x49, 0x0a, 0xf1, 0x7a, 0x2a, 0x96, 0x3f
	};
	wchar_t root[MAX_PATH];
	wchar_t ham_eeprom_path[MAX_PATH] = { 0 };
	wchar_t ham_state_path[MAX_PATH] = { 0 };
	wchar_t hk_eeprom_path[MAX_PATH] = { 0 };
	wchar_t hk_state_path[MAX_PATH] = { 0 };
	wchar_t error[512];
	uint8_t ham_eeprom[DRGQST_PERSISTENCE_EEPROM24C16_SIZE];
	uint8_t hk_eeprom[DRGQST_PERSISTENCE_EEPROM24C16_SIZE];
	uint8_t output[DRGQST_PERSISTENCE_EEPROM24C16_SIZE];
	uint8_t ham_state[5] = { 'H', 'A', 'M', 0x00, 0x01 };
	uint8_t hk_state[5] = { 'H', 'K', 0x00, 0x02, 0x03 };
	size_t output_size = 0;
	size_t index;

	if (!create_test_root(root))
	{
		CHECK(0 && "create TV-PC persistence root");
		return;
	}
	for (index = 0; index < sizeof(ham_eeprom); ++index)
	{
		ham_eeprom[index] = (uint8_t)(index * 3 + 0x19);
		hk_eeprom[index] = (uint8_t)(index * 5 + 0x27);
	}
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_TVPC_HAM_EEPROM, ham_eeprom_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_TVPC_HAM_RUNTIME_STATE, ham_state_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_TVPC_HK_EEPROM, hk_eeprom_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_TVPC_HK_RUNTIME_STATE, hk_state_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(wcsstr(ham_eeprom_path, L"\\tvpc_ham-eeprom.sav") != NULL);
	CHECK(wcsstr(ham_state_path, L"\\tvpc_ham-runtime-state.sav") != NULL);
	CHECK(wcsstr(hk_eeprom_path, L"\\tvpc_hk-eeprom.sav") != NULL);
	CHECK(wcsstr(hk_state_path, L"\\tvpc_hk-runtime-state.sav") != NULL);
	CHECK(wcscmp(ham_eeprom_path, hk_eeprom_path) != 0);
	CHECK(wcscmp(ham_state_path, hk_state_path) != 0);

	CHECK(!drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_TVPC_HAM_EEPROM, ham_sha1, ham_eeprom,
		sizeof(ham_eeprom) - 1, error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_TVPC_HAM_EEPROM, ham_sha1, ham_eeprom,
		sizeof(ham_eeprom), error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_TVPC_HAM_RUNTIME_STATE, ham_sha1, ham_state,
		sizeof(ham_state), error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_TVPC_HK_EEPROM, hk_sha1, hk_eeprom,
		sizeof(hk_eeprom), error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_TVPC_HK_RUNTIME_STATE, hk_sha1, hk_state,
		sizeof(hk_state), error, sizeof(error) / sizeof(error[0])));

	memset(output, 0, sizeof(output));
	CHECK(drgqst_persistence_load(root,
		DRGQST_PERSISTENCE_TVPC_HAM_EEPROM, ham_sha1, output,
		sizeof(output), &output_size, error, sizeof(error) / sizeof(error[0])));
	CHECK(output_size == sizeof(ham_eeprom));
	CHECK(!memcmp(output, ham_eeprom, sizeof(ham_eeprom)));
	memset(output, 0, sizeof(output));
	CHECK(drgqst_persistence_load(root,
		DRGQST_PERSISTENCE_TVPC_HK_EEPROM, hk_sha1, output,
		sizeof(output), &output_size, error, sizeof(error) / sizeof(error[0])));
	CHECK(output_size == sizeof(hk_eeprom));
	CHECK(!memcmp(output, hk_eeprom, sizeof(hk_eeprom)));
	output_size = 99;
	CHECK(!drgqst_persistence_load(root,
		DRGQST_PERSISTENCE_TVPC_HAM_EEPROM, hk_sha1, output,
		sizeof(output), &output_size, error, sizeof(error) / sizeof(error[0])));
	CHECK(output_size == 0);
	memset(output, 0, sizeof(output));
	CHECK(drgqst_persistence_load(root,
		DRGQST_PERSISTENCE_TVPC_HAM_RUNTIME_STATE, ham_sha1, output,
		sizeof(output), &output_size, error, sizeof(error) / sizeof(error[0])));
	CHECK(output_size == sizeof(ham_state));
	CHECK(!memcmp(output, ham_state, sizeof(ham_state)));
	memset(output, 0, sizeof(output));
	CHECK(drgqst_persistence_load(root,
		DRGQST_PERSISTENCE_TVPC_HK_RUNTIME_STATE, hk_sha1, output,
		sizeof(output), &output_size, error, sizeof(error) / sizeof(error[0])));
	CHECK(output_size == sizeof(hk_state));
	CHECK(!memcmp(output, hk_state, sizeof(hk_state)));

	if (*ham_eeprom_path)
		DeleteFileW(ham_eeprom_path);
	if (*ham_state_path)
		DeleteFileW(ham_state_path);
	if (*hk_eeprom_path)
		DeleteFileW(hk_eeprom_path);
	if (*hk_state_path)
		DeleteFileW(hk_state_path);
	RemoveDirectoryW(root);
}

static void test_tom_dpgm_persistence(void)
{
	static const uint8_t rom_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
	{
		0xfa, 0x30, 0x06, 0x9d, 0x17, 0x70, 0x5f, 0x27, 0xe4, 0xff,
		0x45, 0xe7, 0xf6, 0xcc, 0xf0, 0x69, 0x86, 0xe1, 0x38, 0xf3
	};
	wchar_t root[MAX_PATH];
	wchar_t eeprom_path[MAX_PATH] = { 0 };
	wchar_t state_path[MAX_PATH] = { 0 };
	wchar_t error[512];
	uint8_t eeprom[DRGQST_PERSISTENCE_EEPROM_SIZE];
	uint8_t output[DRGQST_PERSISTENCE_EEPROM_SIZE];
	uint8_t state[5] = { 'D', 'P', 'G', 'M', 1 };
	size_t output_size = 0;
	size_t index;

	if (!create_test_root(root))
	{
		CHECK(0 && "create tom_dpgm persistence root");
		return;
	}
	for (index = 0; index < sizeof(eeprom); ++index)
		eeprom[index] = (uint8_t)(index * 7 + 0x2d);
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_TOM_DPGM_EEPROM, eeprom_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_TOM_DPGM_RUNTIME_STATE, state_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(wcsstr(eeprom_path, L"\\tom_dpgm-eeprom.sav") != NULL);
	CHECK(wcsstr(state_path, L"\\tom_dpgm-runtime-state.sav") != NULL);
	CHECK(!drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_TOM_DPGM_EEPROM, rom_sha1, eeprom,
		sizeof(eeprom) - 1, error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_TOM_DPGM_EEPROM, rom_sha1, eeprom,
		sizeof(eeprom), error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_TOM_DPGM_RUNTIME_STATE, rom_sha1, state,
		sizeof(state), error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_load(root,
		DRGQST_PERSISTENCE_TOM_DPGM_EEPROM, rom_sha1, output,
		sizeof(output), &output_size, error,
		sizeof(error) / sizeof(error[0])));
	CHECK(output_size == sizeof(eeprom));
	CHECK(!memcmp(output, eeprom, sizeof(eeprom)));
	memset(output, 0, sizeof(output));
	CHECK(drgqst_persistence_load(root,
		DRGQST_PERSISTENCE_TOM_DPGM_RUNTIME_STATE, rom_sha1, output,
		sizeof(output), &output_size, error,
		sizeof(error) / sizeof(error[0])));
	CHECK(output_size == sizeof(state));
	CHECK(!memcmp(output, state, sizeof(state)));

	if (*eeprom_path)
		DeleteFileW(eeprom_path);
	if (*state_path)
		DeleteFileW(state_path);
	RemoveDirectoryW(root);
}

static void test_epo_mini_persistence(void)
{
	static const uint8_t rom_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] =
	{
		0x98, 0x72, 0x18, 0xb6, 0x79, 0x91, 0x95, 0xba, 0x15, 0xad,
		0xf3, 0x98, 0x85, 0xc1, 0xd1, 0x77, 0xc3, 0x81, 0xec, 0x26
	};
	wchar_t root[MAX_PATH];
	wchar_t eeprom_path[MAX_PATH] = { 0 };
	wchar_t state_path[MAX_PATH] = { 0 };
	wchar_t error[512];
	uint8_t eeprom[DRGQST_PERSISTENCE_EEPROM_SIZE] = { 0 };
	uint8_t output[DRGQST_PERSISTENCE_EEPROM_SIZE];
	uint8_t state[4] = { 'M', 'I', 'N', 'I' };
	size_t output_size = 0;

	if (!create_test_root(root))
	{
		CHECK(0 && "create epo_mini persistence root");
		return;
	}
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_EPO_MINI_EEPROM, eeprom_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_get_path(root,
		DRGQST_PERSISTENCE_EPO_MINI_RUNTIME_STATE, state_path, MAX_PATH,
		error, sizeof(error) / sizeof(error[0])));
	CHECK(wcsstr(eeprom_path, L"\\epo_mini-eeprom.sav") != NULL);
	CHECK(wcsstr(state_path, L"\\epo_mini-runtime-state.sav") != NULL);
	CHECK(!drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_EPO_MINI_EEPROM, rom_sha1, eeprom,
		sizeof(eeprom) - 1, error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_EPO_MINI_EEPROM, rom_sha1, eeprom,
		sizeof(eeprom), error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_save(root,
		DRGQST_PERSISTENCE_EPO_MINI_RUNTIME_STATE, rom_sha1, state,
		sizeof(state), error, sizeof(error) / sizeof(error[0])));
	CHECK(drgqst_persistence_load(root,
		DRGQST_PERSISTENCE_EPO_MINI_EEPROM, rom_sha1, output,
		sizeof(output), &output_size, error,
		sizeof(error) / sizeof(error[0])));
	CHECK(output_size == sizeof(eeprom));
	CHECK(!memcmp(output, eeprom, sizeof(eeprom)));
	memset(output, 0, sizeof(output));
	CHECK(drgqst_persistence_load(root,
		DRGQST_PERSISTENCE_EPO_MINI_RUNTIME_STATE, rom_sha1, output,
		sizeof(output), &output_size, error,
		sizeof(error) / sizeof(error[0])));
	CHECK(output_size == sizeof(state));
	CHECK(!memcmp(output, state, sizeof(state)));

	if (*eeprom_path)
		DeleteFileW(eeprom_path);
	if (*state_path)
		DeleteFileW(state_path);
	RemoveDirectoryW(root);
}

static void test_xavix2_runtime_state_persistence(void)
{
	static const enum drgqst_persistence_kind kinds[] =
	{
		DRGQST_PERSISTENCE_BAN_NARU_RUNTIME_STATE,
		DRGQST_PERSISTENCE_BAN_BLDJ_RUNTIME_STATE,
		DRGQST_PERSISTENCE_BAN_DB2J_RUNTIME_STATE,
		DRGQST_PERSISTENCE_BAN_DBZ_RUNTIME_STATE
	};
	static const wchar_t *const filenames[] =
	{
		L"\\ban_naru-runtime-state.sav",
		L"\\ban_bldj-runtime-state.sav",
		L"\\ban_db2j-runtime-state.sav",
		L"\\ban_dbz-runtime-state.sav"
	};
	wchar_t root[MAX_PATH];
	wchar_t paths[4][MAX_PATH] = { { 0 } };
	wchar_t error[512];
	uint8_t sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE] = { 0 };
	uint8_t state[4] = { 'X', '2', 'S', 0 };
	uint8_t output[sizeof(state)] = { 0 };
	size_t output_size;
	size_t index;

	if (!create_test_root(root))
	{
		CHECK(0 && "create XaviX2 persistence root");
		return;
	}
	for (index = 0; index < sizeof(kinds) / sizeof(kinds[0]); ++index)
	{
		sha1[0] = (uint8_t)(index + 1);
		state[3] = (uint8_t)index;
		CHECK(drgqst_persistence_get_path(root, kinds[index], paths[index],
			MAX_PATH, error, sizeof(error) / sizeof(error[0])));
		CHECK(wcsstr(paths[index], filenames[index]) != NULL);
		CHECK(drgqst_persistence_save(root, kinds[index], sha1, state,
			sizeof(state), error, sizeof(error) / sizeof(error[0])));
		memset(output, 0, sizeof(output));
		output_size = 0;
		CHECK(drgqst_persistence_load(root, kinds[index], sha1, output,
			sizeof(output), &output_size, error,
			sizeof(error) / sizeof(error[0])));
		CHECK(output_size == sizeof(state));
		CHECK(!memcmp(output, state, sizeof(state)));
	}
	for (index = 0; index < sizeof(paths) / sizeof(paths[0]); ++index)
		if (*paths[index])
			DeleteFileW(paths[index]);
	RemoveDirectoryW(root);
}

int main(void)
{
	test_default_directory();
	test_persistence();
	test_xavix2000_24c04_persistence_kinds();
	test_epo_sdb_parallel_nvram_persistence();
	test_epo_ebox_parallel_nvram_persistence();
	test_epo_es2j_runtime_state_persistence();
	test_epo_hamc_runtime_state_persistence();
	test_tvpc_pair_persistence();
	test_tom_dpgm_persistence();
	test_epo_mini_persistence();
	test_xavix2_runtime_state_persistence();

	if (failures)
	{
		fprintf(stderr, "%u persistence test(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	puts("persistence: all tests passed");
	return EXIT_SUCCESS;
}
