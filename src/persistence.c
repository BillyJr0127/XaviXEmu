// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "persistence.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

enum
{
	PERSISTENCE_VERSION = 1,
	/* This standalone target does not currently carry a longPathAware manifest. */
	MAXIMUM_PATH_CHARACTERS = MAX_PATH,
	TEMPORARY_FILE_ATTEMPTS = 64
};

static const uint8_t PERSISTENCE_MAGIC[8] = { 'D', 'R', 'G', 'Q', 'S', 'A', 'V', 'E' };
static const wchar_t APPLICATION_DIRECTORY[] = L"DrgqstPlayer";
static const wchar_t EEPROM_FILENAME[] = L"eeprom.sav";
static const wchar_t STATE_FILENAME[] = L"runtime-state.sav";
static const wchar_t BAN_ONEP_EEPROM_FILENAME[] = L"ban_onep-eeprom.sav";
static const wchar_t BAN_ONEP_STATE_FILENAME[] = L"ban_onep-runtime-state.sav";
static const wchar_t BAN_OMT_EEPROM_FILENAME[] = L"ban_omt-eeprom.sav";
static const wchar_t BAN_OMT_STATE_FILENAME[] = L"ban_omt-runtime-state.sav";
static const wchar_t TTV_LOTR_EEPROM_FILENAME[] = L"ttv_lotr-eeprom.sav";
static const wchar_t TTV_LOTR_STATE_FILENAME[] = L"ttv_lotr-runtime-state.sav";
static const wchar_t TTV_SW_EEPROM_FILENAME[] = L"ttv_sw-eeprom.sav";
static const wchar_t TTV_SW_STATE_FILENAME[] = L"ttv_sw-runtime-state.sav";
static const wchar_t TTV_SWJ_EEPROM_FILENAME[] = L"ttv_swj-eeprom.sav";
static const wchar_t TTV_SWJ_STATE_FILENAME[] = L"ttv_swj-runtime-state.sav";
static const wchar_t EPO_HAMD_STATE_FILENAME[] = L"epo_hamd-runtime-state.sav";
static const wchar_t TVPC_DOR_EEPROM_FILENAME[] = L"tvpc_dor-eeprom.sav";
static const wchar_t TVPC_DOR_STATE_FILENAME[] = L"tvpc_dor-runtime-state.sav";
static volatile LONG temporary_counter;

static void clear_error(wchar_t *error, size_t error_length)
{
	if (error && error_length)
		error[0] = L'\0';
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

static void append_cleanup_error(wchar_t *error, size_t error_length, DWORD windows_error)
{
	size_t used;

	if (!error || !error_length)
		return;
	used = wcslen(error);
	if (used >= error_length - 1)
		return;
	_snwprintf(
		error + used,
		error_length - used,
		L" Temporary-file cleanup also failed (Windows error %lu).",
		windows_error);
	error[error_length - 1] = L'\0';
}

static const wchar_t *filename_for_kind(enum drgqst_persistence_kind kind)
{
	switch (kind)
	{
	case DRGQST_PERSISTENCE_EEPROM:
		return EEPROM_FILENAME;
	case DRGQST_PERSISTENCE_RUNTIME_STATE:
		return STATE_FILENAME;
	case DRGQST_PERSISTENCE_BAN_ONEP_EEPROM:
		return BAN_ONEP_EEPROM_FILENAME;
	case DRGQST_PERSISTENCE_BAN_ONEP_RUNTIME_STATE:
		return BAN_ONEP_STATE_FILENAME;
	case DRGQST_PERSISTENCE_BAN_OMT_EEPROM:
		return BAN_OMT_EEPROM_FILENAME;
	case DRGQST_PERSISTENCE_BAN_OMT_RUNTIME_STATE:
		return BAN_OMT_STATE_FILENAME;
	case DRGQST_PERSISTENCE_TTV_LOTR_EEPROM:
		return TTV_LOTR_EEPROM_FILENAME;
	case DRGQST_PERSISTENCE_TTV_LOTR_RUNTIME_STATE:
		return TTV_LOTR_STATE_FILENAME;
	case DRGQST_PERSISTENCE_TTV_SW_EEPROM:
		return TTV_SW_EEPROM_FILENAME;
	case DRGQST_PERSISTENCE_TTV_SW_RUNTIME_STATE:
		return TTV_SW_STATE_FILENAME;
	case DRGQST_PERSISTENCE_TTV_SWJ_EEPROM:
		return TTV_SWJ_EEPROM_FILENAME;
	case DRGQST_PERSISTENCE_TTV_SWJ_RUNTIME_STATE:
		return TTV_SWJ_STATE_FILENAME;
	case DRGQST_PERSISTENCE_EPO_HAMD_RUNTIME_STATE:
		return EPO_HAMD_STATE_FILENAME;
	case DRGQST_PERSISTENCE_TVPC_DOR_EEPROM:
		return TVPC_DOR_EEPROM_FILENAME;
	case DRGQST_PERSISTENCE_TVPC_DOR_RUNTIME_STATE:
		return TVPC_DOR_STATE_FILENAME;
	default:
		return NULL;
	}
}

static int validate_payload_size(
	enum drgqst_persistence_kind kind,
	size_t payload_size,
	wchar_t *error,
	size_t error_length)
{
	if (kind == DRGQST_PERSISTENCE_EEPROM ||
		kind == DRGQST_PERSISTENCE_BAN_ONEP_EEPROM ||
		kind == DRGQST_PERSISTENCE_BAN_OMT_EEPROM ||
		kind == DRGQST_PERSISTENCE_TTV_LOTR_EEPROM ||
		kind == DRGQST_PERSISTENCE_TTV_SW_EEPROM ||
		kind == DRGQST_PERSISTENCE_TTV_SWJ_EEPROM)
	{
		if (payload_size != DRGQST_PERSISTENCE_EEPROM_SIZE)
		{
			set_error(error, error_length, L"EEPROM saves must contain exactly 1024 bytes.");
			return 0;
		}
		return 1;
	}
	if (kind == DRGQST_PERSISTENCE_TVPC_DOR_EEPROM)
	{
		if (payload_size != DRGQST_PERSISTENCE_EEPROM24C16_SIZE)
		{
			set_error(error, error_length, L"24C16 EEPROM saves must contain exactly 2048 bytes.");
			return 0;
		}
		return 1;
	}

	if (kind == DRGQST_PERSISTENCE_RUNTIME_STATE ||
		kind == DRGQST_PERSISTENCE_BAN_ONEP_RUNTIME_STATE ||
		kind == DRGQST_PERSISTENCE_BAN_OMT_RUNTIME_STATE ||
		kind == DRGQST_PERSISTENCE_TTV_LOTR_RUNTIME_STATE ||
		kind == DRGQST_PERSISTENCE_TTV_SW_RUNTIME_STATE ||
		kind == DRGQST_PERSISTENCE_TTV_SWJ_RUNTIME_STATE ||
		kind == DRGQST_PERSISTENCE_EPO_HAMD_RUNTIME_STATE ||
		kind == DRGQST_PERSISTENCE_TVPC_DOR_RUNTIME_STATE)
	{
		if (!payload_size || payload_size > DRGQST_PERSISTENCE_MAX_STATE_SIZE)
		{
			set_error(error, error_length, L"The runtime state size is not supported.");
			return 0;
		}
		return 1;
	}

	set_error(error, error_length, L"The persistence data kind is not supported.");
	return 0;
}

static int copy_base_directory(
	const wchar_t *override,
	wchar_t **base,
	size_t *base_length,
	wchar_t *error,
	size_t error_length)
{
	wchar_t *copy;
	size_t length;

	if (override)
	{
		length = wcslen(override);
		if (!length || length >= MAXIMUM_PATH_CHARACTERS)
		{
			set_error(error, error_length, L"The supplied save-data base directory is not valid.");
			return 0;
		}

		copy = HeapAlloc(GetProcessHeap(), 0, (length + 1) * sizeof(*copy));
		if (!copy)
		{
			set_error(error, error_length, L"There is not enough memory to resolve the save-data directory.");
			return 0;
		}
		memcpy(copy, override, (length + 1) * sizeof(*copy));
	}
	else
	{
		DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", NULL, 0);
		DWORD received;

		if (!required || required > MAXIMUM_PATH_CHARACTERS)
		{
			set_error(error, error_length, L"LOCALAPPDATA is not available.");
			return 0;
		}

		copy = HeapAlloc(GetProcessHeap(), 0, (size_t)required * sizeof(*copy));
		if (!copy)
		{
			set_error(error, error_length, L"There is not enough memory to resolve the save-data directory.");
			return 0;
		}

		received = GetEnvironmentVariableW(L"LOCALAPPDATA", copy, required);
		if (!received || received >= required)
		{
			set_error(error, error_length, L"LOCALAPPDATA could not be read.");
			HeapFree(GetProcessHeap(), 0, copy);
			return 0;
		}
		length = received;
	}

	while (length > 1 &&
		!(length == 3 && copy[1] == L':') &&
		(copy[length - 1] == L'\\' || copy[length - 1] == L'/'))
		length--;
	if (!length)
	{
		set_error(error, error_length, L"The save-data base directory is empty.");
		HeapFree(GetProcessHeap(), 0, copy);
		return 0;
	}
	copy[length] = L'\0';
	*base = copy;
	*base_length = length;
	return 1;
}

static int resolve_directory(
	const wchar_t *directory_override,
	wchar_t **directory,
	wchar_t *error,
	size_t error_length)
{
	wchar_t *base = NULL;
	wchar_t *result;
	size_t base_length = 0;
	const size_t application_length = sizeof(APPLICATION_DIRECTORY) / sizeof(APPLICATION_DIRECTORY[0]) - 1;
	size_t result_length;

	if (!copy_base_directory(directory_override, &base, &base_length, error,
		error_length))
		return 0;
	if (directory_override)
	{
		*directory = base;
		return 1;
	}

	if (base_length > MAXIMUM_PATH_CHARACTERS - application_length - 2)
	{
		set_error(error, error_length, L"The save-data directory path is too long.");
		HeapFree(GetProcessHeap(), 0, base);
		return 0;
	}
	result_length = base_length + 1 + application_length;
	result = HeapAlloc(GetProcessHeap(), 0, (result_length + 1) * sizeof(*result));
	if (!result)
	{
		set_error(error, error_length, L"There is not enough memory to resolve the save-data directory.");
		HeapFree(GetProcessHeap(), 0, base);
		return 0;
	}

	memcpy(result, base, base_length * sizeof(*result));
	result[base_length] = L'\\';
	memcpy(result + base_length + 1, APPLICATION_DIRECTORY, (application_length + 1) * sizeof(*result));
	HeapFree(GetProcessHeap(), 0, base);
	*directory = result;
	return 1;
}

static int build_file_path(
	const wchar_t *directory,
	enum drgqst_persistence_kind kind,
	wchar_t **path,
	wchar_t *error,
	size_t error_length)
{
	const wchar_t *filename = filename_for_kind(kind);
	size_t directory_length;
	size_t filename_length;
	size_t path_length;
	wchar_t *result;

	if (!filename)
	{
		set_error(error, error_length, L"The persistence data kind is not supported.");
		return 0;
	}
	directory_length = wcslen(directory);
	filename_length = wcslen(filename);
	if (directory_length > MAXIMUM_PATH_CHARACTERS - filename_length - 2)
	{
		set_error(error, error_length, L"The save-data file path is too long.");
		return 0;
	}
	path_length = directory_length + 1 + filename_length;
	result = HeapAlloc(GetProcessHeap(), 0, (path_length + 1) * sizeof(*result));
	if (!result)
	{
		set_error(error, error_length, L"There is not enough memory to resolve the save-data file.");
		return 0;
	}

	memcpy(result, directory, directory_length * sizeof(*result));
	result[directory_length] = L'\\';
	memcpy(result + directory_length + 1, filename, (filename_length + 1) * sizeof(*result));
	*path = result;
	return 1;
}

static int resolve_paths(
	const wchar_t *directory_override,
	enum drgqst_persistence_kind kind,
	wchar_t **directory,
	wchar_t **path,
	wchar_t *error,
	size_t error_length)
{
	if (!filename_for_kind(kind))
	{
		set_error(error, error_length, L"The persistence data kind is not supported.");
		return 0;
	}
	if (!resolve_directory(directory_override, directory, error, error_length))
		return 0;
	if (!build_file_path(*directory, kind, path, error, error_length))
	{
		HeapFree(GetProcessHeap(), 0, *directory);
		*directory = NULL;
		return 0;
	}
	return 1;
}

static int copy_resolved_path(
	const wchar_t *resolved,
	wchar_t *output,
	size_t output_length,
	wchar_t *error,
	size_t error_length)
{
	const size_t required = wcslen(resolved) + 1;
	if (!output || output_length < required)
	{
		set_error(error, error_length, L"The supplied path buffer is too small.");
		return 0;
	}
	memcpy(output, resolved, required * sizeof(*output));
	return 1;
}

int drgqst_persistence_get_directory(
	const wchar_t *directory_override,
	wchar_t *output,
	size_t output_length,
	wchar_t *error,
	size_t error_length)
{
	wchar_t *directory = NULL;
	int success;

	clear_error(error, error_length);
	if (!resolve_directory(directory_override, &directory, error, error_length))
		return 0;
	success = copy_resolved_path(directory, output, output_length, error, error_length);
	HeapFree(GetProcessHeap(), 0, directory);
	return success;
}

int drgqst_persistence_get_path(
	const wchar_t *directory_override,
	enum drgqst_persistence_kind kind,
	wchar_t *output,
	size_t output_length,
	wchar_t *error,
	size_t error_length)
{
	wchar_t *directory = NULL;
	wchar_t *path = NULL;
	int success;

	clear_error(error, error_length);
	if (!resolve_paths(directory_override, kind, &directory, &path, error,
		error_length))
		return 0;
	success = copy_resolved_path(path, output, output_length, error, error_length);
	HeapFree(GetProcessHeap(), 0, path);
	HeapFree(GetProcessHeap(), 0, directory);
	return success;
}

static uint32_t persistence_crc32(const uint8_t *data, size_t size)
{
	uint32_t crc = UINT32_C(0xffffffff);
	size_t index;

	for (index = 0; index < size; index++)
	{
		unsigned bit;
		crc ^= data[index];
		for (bit = 0; bit < 8; bit++)
		{
			const uint32_t mask = (uint32_t)-(int32_t)(crc & 1);
			crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
		}
	}
	return ~crc;
}

static void write_u16_le(uint8_t *output, uint16_t value)
{
	output[0] = (uint8_t)value;
	output[1] = (uint8_t)(value >> 8);
}

static void write_u32_le(uint8_t *output, uint32_t value)
{
	output[0] = (uint8_t)value;
	output[1] = (uint8_t)(value >> 8);
	output[2] = (uint8_t)(value >> 16);
	output[3] = (uint8_t)(value >> 24);
}

static uint16_t read_u16_le(const uint8_t *input)
{
	return (uint16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *input)
{
	return (uint32_t)input[0] | ((uint32_t)input[1] << 8) | ((uint32_t)input[2] << 16) |
		((uint32_t)input[3] << 24);
}

static void encode_header(
	uint8_t header[DRGQST_PERSISTENCE_HEADER_SIZE],
	enum drgqst_persistence_kind kind,
	const uint8_t rom_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE],
	size_t payload_size,
	uint32_t crc32)
{
	memcpy(header, PERSISTENCE_MAGIC, sizeof(PERSISTENCE_MAGIC));
	write_u16_le(header + 8, PERSISTENCE_VERSION);
	write_u16_le(header + 10, (uint16_t)kind);
	memcpy(header + 12, rom_sha1, DRGQST_PERSISTENCE_ROM_SHA1_SIZE);
	write_u32_le(header + 32, (uint32_t)payload_size);
	write_u32_le(header + 36, crc32);
}

static int ensure_directory(const wchar_t *directory, wchar_t *error, size_t error_length)
{
	DWORD windows_error;
	DWORD attributes;

	if (CreateDirectoryW(directory, NULL))
		return 1;
	windows_error = GetLastError();
	if (windows_error == ERROR_ALREADY_EXISTS)
	{
		attributes = GetFileAttributesW(directory);
		if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
			return 1;
	}
	set_error(error, error_length, L"The save-data directory could not be created (Windows error %lu).", windows_error);
	return 0;
}

static void append_hexadecimal(wchar_t output[8], DWORD value)
{
	static const wchar_t digits[] = L"0123456789abcdef";
	unsigned index;
	for (index = 0; index < 8; index++)
	{
		const unsigned shift = (7 - index) * 4;
		output[index] = digits[(value >> shift) & 0x0f];
	}
}

static int create_temporary_file(
	const wchar_t *target_path,
	wchar_t **temporary_path,
	HANDLE *file,
	wchar_t *error,
	size_t error_length)
{
	static const wchar_t marker[] = L".tmp-";
	const size_t marker_length = sizeof(marker) / sizeof(marker[0]) - 1;
	const size_t target_length = wcslen(target_path);
	const size_t suffix_length = marker_length + 8 + 1 + 8;
	wchar_t *path;
	unsigned attempt;

	if (target_length > MAXIMUM_PATH_CHARACTERS - suffix_length - 1)
	{
		set_error(error, error_length, L"The temporary save-data path is too long.");
		return 0;
	}
	path = HeapAlloc(GetProcessHeap(), 0, (target_length + suffix_length + 1) * sizeof(*path));
	if (!path)
	{
		set_error(error, error_length, L"There is not enough memory to create a temporary save-data path.");
		return 0;
	}

	memcpy(path, target_path, target_length * sizeof(*path));
	memcpy(path + target_length, marker, marker_length * sizeof(*path));
	append_hexadecimal(path + target_length + marker_length, GetCurrentProcessId());
	path[target_length + marker_length + 8] = L'-';
	path[target_length + suffix_length] = L'\0';

	for (attempt = 0; attempt < TEMPORARY_FILE_ATTEMPTS; attempt++)
	{
		const DWORD serial = (DWORD)InterlockedIncrement(&temporary_counter);
		DWORD windows_error;
		append_hexadecimal(path + target_length + marker_length + 9, serial);
		*file = CreateFileW(
			path,
			GENERIC_WRITE,
			0,
			NULL,
			CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL,
			NULL);
		if (*file != INVALID_HANDLE_VALUE)
		{
			*temporary_path = path;
			return 1;
		}
		windows_error = GetLastError();
		if (windows_error != ERROR_FILE_EXISTS && windows_error != ERROR_ALREADY_EXISTS)
		{
			set_error(error, error_length, L"A temporary save-data file could not be created (Windows error %lu).", windows_error);
			HeapFree(GetProcessHeap(), 0, path);
			return 0;
		}
	}

	set_error(error, error_length, L"A unique temporary save-data file could not be created.");
	HeapFree(GetProcessHeap(), 0, path);
	return 0;
}

static int write_all(HANDLE file, const uint8_t *data, size_t size, DWORD *windows_error)
{
	while (size)
	{
		const DWORD requested = size > MAXDWORD ? MAXDWORD : (DWORD)size;
		DWORD written = 0;
		if (!WriteFile(file, data, requested, &written, NULL))
		{
			*windows_error = GetLastError();
			return 0;
		}
		if (!written)
		{
			*windows_error = ERROR_WRITE_FAULT;
			return 0;
		}
		data += written;
		size -= written;
	}
	return 1;
}

static int read_all(HANDLE file, uint8_t *data, size_t size, DWORD *windows_error)
{
	while (size)
	{
		const DWORD requested = size > MAXDWORD ? MAXDWORD : (DWORD)size;
		DWORD received = 0;
		if (!ReadFile(file, data, requested, &received, NULL))
		{
			*windows_error = GetLastError();
			return 0;
		}
		if (!received)
		{
			*windows_error = ERROR_HANDLE_EOF;
			return 0;
		}
		data += received;
		size -= received;
	}
	return 1;
}

int drgqst_persistence_save(
	const wchar_t *directory_override,
	enum drgqst_persistence_kind kind,
	const uint8_t rom_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE],
	const void *payload,
	size_t payload_size,
	wchar_t *error,
	size_t error_length)
{
	wchar_t *directory = NULL;
	wchar_t *target_path = NULL;
	wchar_t *temporary_path = NULL;
	HANDLE file = INVALID_HANDLE_VALUE;
	uint8_t header[DRGQST_PERSISTENCE_HEADER_SIZE];
	DWORD windows_error = ERROR_SUCCESS;
	int success = 0;

	clear_error(error, error_length);
	if (!rom_sha1 || !payload)
	{
		set_error(error, error_length, L"No persistence payload or ROM identity was supplied.");
		return 0;
	}
	if (!validate_payload_size(kind, payload_size, error, error_length))
		return 0;
	if (!resolve_paths(directory_override, kind, &directory, &target_path,
		error, error_length))
		return 0;
	if (!ensure_directory(directory, error, error_length))
		goto cleanup;
	if (!create_temporary_file(target_path, &temporary_path, &file, error, error_length))
		goto cleanup;

	encode_header(header, kind, rom_sha1, payload_size, persistence_crc32(payload, payload_size));
	if (!write_all(file, header, sizeof(header), &windows_error) ||
		!write_all(file, payload, payload_size, &windows_error))
	{
		set_error(error, error_length, L"The temporary save-data file could not be written (Windows error %lu).", windows_error);
		goto cleanup;
	}
	if (!FlushFileBuffers(file))
	{
		windows_error = GetLastError();
		set_error(error, error_length, L"The temporary save-data file could not be flushed (Windows error %lu).", windows_error);
		goto cleanup;
	}
	CloseHandle(file);
	file = INVALID_HANDLE_VALUE;

	if (!MoveFileExW(temporary_path, target_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		windows_error = GetLastError();
		set_error(error, error_length, L"The save-data file could not be replaced atomically (Windows error %lu).", windows_error);
		goto cleanup;
	}
	success = 1;

cleanup:
	if (file != INVALID_HANDLE_VALUE)
		CloseHandle(file);
	if (!success && temporary_path && !DeleteFileW(temporary_path))
		append_cleanup_error(error, error_length, GetLastError());
	if (temporary_path)
		HeapFree(GetProcessHeap(), 0, temporary_path);
	if (target_path)
		HeapFree(GetProcessHeap(), 0, target_path);
	if (directory)
		HeapFree(GetProcessHeap(), 0, directory);
	return success;
}

int drgqst_persistence_load(
	const wchar_t *directory_override,
	enum drgqst_persistence_kind kind,
	const uint8_t rom_sha1[DRGQST_PERSISTENCE_ROM_SHA1_SIZE],
	void *payload,
	size_t payload_capacity,
	size_t *payload_size,
	wchar_t *error,
	size_t error_length)
{
	wchar_t *directory = NULL;
	wchar_t *path = NULL;
	HANDLE file = INVALID_HANDLE_VALUE;
	LARGE_INTEGER file_size;
	uint8_t header[DRGQST_PERSISTENCE_HEADER_SIZE];
	uint8_t *temporary_payload = NULL;
	uint32_t encoded_payload_size;
	uint32_t encoded_crc32;
	DWORD windows_error = ERROR_SUCCESS;
	int success = 0;

	clear_error(error, error_length);
	if (payload_size)
		*payload_size = 0;
	if (!rom_sha1 || !payload || !payload_size)
	{
		set_error(error, error_length, L"No load buffer or ROM identity was supplied.");
		return 0;
	}
	if (!filename_for_kind(kind))
	{
		set_error(error, error_length, L"The persistence data kind is not supported.");
		return 0;
	}
	if (!resolve_paths(directory_override, kind, &directory, &path, error,
		error_length))
		return 0;

	file = CreateFileW(
		path,
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_DELETE,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (file == INVALID_HANDLE_VALUE)
	{
		windows_error = GetLastError();
		set_error(error, error_length, L"The save-data file could not be opened (Windows error %lu).", windows_error);
		goto cleanup;
	}
	if (!GetFileSizeEx(file, &file_size))
	{
		windows_error = GetLastError();
		set_error(error, error_length, L"The save-data file size could not be read (Windows error %lu).", windows_error);
		goto cleanup;
	}
	if (file_size.QuadPart < DRGQST_PERSISTENCE_HEADER_SIZE ||
		file_size.QuadPart > (LONGLONG)DRGQST_PERSISTENCE_HEADER_SIZE + DRGQST_PERSISTENCE_MAX_STATE_SIZE)
	{
		set_error(error, error_length, L"The save-data file size is not valid.");
		goto cleanup;
	}
	if (!read_all(file, header, sizeof(header), &windows_error))
	{
		set_error(error, error_length, L"The save-data header could not be read completely (Windows error %lu).", windows_error);
		goto cleanup;
	}

	encoded_payload_size = read_u32_le(header + 32);
	encoded_crc32 = read_u32_le(header + 36);
	if (memcmp(header, PERSISTENCE_MAGIC, sizeof(PERSISTENCE_MAGIC)) ||
		read_u16_le(header + 8) != PERSISTENCE_VERSION)
	{
		set_error(error, error_length, L"The save-data header is not recognized.");
		goto cleanup;
	}
	if (read_u16_le(header + 10) != (uint16_t)kind)
	{
		set_error(error, error_length, L"The save-data file contains the wrong data kind.");
		goto cleanup;
	}
	if (memcmp(header + 12, rom_sha1, DRGQST_PERSISTENCE_ROM_SHA1_SIZE))
	{
		set_error(error, error_length, L"The save data belongs to a different Dragon Quest ROM.");
		goto cleanup;
	}
	if (!validate_payload_size(kind, encoded_payload_size, error, error_length))
		goto cleanup;
	if (file_size.QuadPart != (LONGLONG)DRGQST_PERSISTENCE_HEADER_SIZE + encoded_payload_size)
	{
		set_error(error, error_length, L"The save-data file is truncated or has trailing data.");
		goto cleanup;
	}
	if (payload_capacity < encoded_payload_size)
	{
		set_error(error, error_length, L"The supplied load buffer is too small.");
		goto cleanup;
	}

	temporary_payload = HeapAlloc(GetProcessHeap(), 0, encoded_payload_size);
	if (!temporary_payload)
	{
		set_error(error, error_length, L"There is not enough memory to validate the save data.");
		goto cleanup;
	}
	if (!read_all(file, temporary_payload, encoded_payload_size, &windows_error))
	{
		set_error(error, error_length, L"The save-data payload could not be read completely (Windows error %lu).", windows_error);
		goto cleanup;
	}
	if (persistence_crc32(temporary_payload, encoded_payload_size) != encoded_crc32)
	{
		set_error(error, error_length, L"The save-data checksum is invalid.");
		goto cleanup;
	}

	memcpy(payload, temporary_payload, encoded_payload_size);
	*payload_size = encoded_payload_size;
	success = 1;

cleanup:
	if (temporary_payload)
		HeapFree(GetProcessHeap(), 0, temporary_payload);
	if (file != INVALID_HANDLE_VALUE)
		CloseHandle(file);
	if (path)
		HeapFree(GetProcessHeap(), 0, path);
	if (directory)
		HeapFree(GetProcessHeap(), 0, directory);
	return success;
}
