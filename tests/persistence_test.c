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

int main(void)
{
	test_default_directory();
	test_persistence();

	if (failures)
	{
		fprintf(stderr, "%u persistence test(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	puts("persistence: all tests passed");
	return EXIT_SUCCESS;
}
