// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "game_library.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static const xavix_game_metadata GAME_METADATA[] = {
	{ DRGQST_ROM_DRAGON_QUEST, L"Kenshin Dragon Quest: Yomigaerishi Densetsu no Ken (Japan)", L"2003", L"Square Enix / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_BAN_ONEP, L"Let's! TV Play Taikan Grand Prix: From TV Animation One Piece (Japan)", L"2004", L"Bandai / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_BAN_OMT, L"Let's! TV Play Taikan Onmyou Taisenki: Mezase Saikyou Toushinshi (Japan)", L"2005", L"Bandai / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_TTV_LOTR, L"Lord of the Rings: Warrior of Middle-Earth", L"2005", L"Tiger / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_TTV_SW, L"Star Wars: Lightsaber Battle Game - Saga Edition", L"2005", L"Tiger / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_TTV_SWJ, L"Star Wars: Lightsaber Battle Game (Japan)", L"2005", L"Tomy / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_TTV_MX, L"MX Dirt Rebel", L"2005", L"Tiger / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_TOM_JUMP, L"IDATEN Jump: Gekisou IDATEN Battle (Japan)", L"2005", L"Tomy / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_EPO_SDB, L"Super Dash Ball (Japan)", L"2004", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_EPO_BOWL, L"Excite Bowling (Japan)", L"2002", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_BAN_NARU, L"Let's! TV Play Taikan Ninja Taizen (Japan)", L"2006", L"Bandai / SSD", XAVIX_GAME_PLATFORM_XAVIX2 },
	{ DRGQST_ROM_BAN_BLDJ, L"Let's! TV Play Taikan Blue Dragon (Japan)", L"2006", L"Bandai / SSD", XAVIX_GAME_PLATFORM_XAVIX2 },
	{ DRGQST_ROM_BAN_DB2J, L"Let's! TV Play Taikan Dragon Ball Z Kamehameha 2 (Japan)", L"2006", L"Bandai / SSD", XAVIX_GAME_PLATFORM_XAVIX2 },
	{ DRGQST_ROM_BAN_DBZ, L"Let's! TV Play Taikan Dragon Ball Z Kamehameha (Trial, Japan)", L"2005", L"Bandai / SSD", XAVIX_GAME_PLATFORM_XAVIX2 },
	{ DRGQST_ROM_EPO_HAMD, L"Ham-chans: Ham Ham Challenge! Atsumare Ham-chans! (Japan)", L"2001", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX },
	{ DRGQST_ROM_TVPC_DOR, L"TV-PC Doraemon (Japan)", L"2003", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX },
	{ DRGQST_ROM_TAK_CHQ, L"Choro-Q Byun Byun Racer (Japan)", L"2003", L"Takara / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_EPO_EBOX, L"Excite Boxing (Japan)", L"2002", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_EPO_ES2J, L"Card Scan! Excite Stage Soccer 2 (Japan)", L"2006", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_EPO_HAMC, L"Tottoko Hamtaro: Ham Ham Dai Circus (Japan)", L"2002", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_TVPC_HAM, L"TV-PC Tottoko Hamtaro (Japan)", L"2003", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX },
	{ DRGQST_ROM_TVPC_HK, L"TV-PC Hello Kitty (Japan)", L"2004", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX },
	{ DRGQST_ROM_TOM_DPGM, L"Disney Princess: Kirakira Mahou no Lesson (Japan)", L"2004", L"Tomy / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_EPO_MINI, L"Mini Moni Party! (Japan)", L"2003", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_EPO_DAB2J, L"Doraemon: AIUEO Zukan (Japan)", L"2000s", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX2 },
	{ DRGQST_ROM_EPO_DTCJ, L"Doraemon Taikan Take-copter! Sora Tobu Daibouken (Japan)", L"2006", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX2 },
	{ DRGQST_ROM_EPO_PABJ, L"Pooh-san: ABC AIUEO (Japan)", L"2007", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX2 },
	{ DRGQST_ROM_EPO_SSK2, L"Kyuukyoku! Kinniku Stadium! Sasuke (Japan)", L"2008", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX2 },
	{ DRGQST_ROM_EPO_SSKJ, L"Sasuke & Kinniku Battle!! Sportsman No.1 Ketteisen (Japan)", L"2006", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX2 },
	{ DRGQST_ROM_RAD_MTRK, L"Play TV Monster Truck (NTSC)", L"2003", L"Radica / SSD", XAVIX_GAME_PLATFORM_XAVIX },
	{ DRGQST_ROM_RAD_SNOW, L"Play TV Snowboarder (Blue) (NTSC)", L"2001", L"Radica / SSD", XAVIX_GAME_PLATFORM_XAVIX },
	{ DRGQST_ROM_RAD_SSX, L"Play TV SSX Snowboarder (NTSC)", L"2004", L"Radica / SSD", XAVIX_GAME_PLATFORM_XAVIX },
	{ DRGQST_ROM_RAD_SBW, L"Play TV Snowboarder (White) (NTSC)", L"2006", L"Radica / SSD", XAVIX_GAME_PLATFORM_XAVIX },
	{ DRGQST_ROM_TAK_GIN, L"Bakushin Sno-Bo: Gingin Boarders (Japan)", L"2001", L"Takara / SSD", XAVIX_GAME_PLATFORM_XAVIX },
	{ DRGQST_ROM_TCARNAVI, L"Tomica Carnavi Drive (Japan)", L"2003", L"Tomy / SSD", XAVIX_GAME_PLATFORM_XAVIX },
	{ DRGQST_ROM_TOMTHR, L"Asobitai Hyper Rescue: Boku wa Kyuujotai! (Japan)", L"2006", L"Takara Tomy / SSD", XAVIX_GAME_PLATFORM_XAVIX },
	{ DRGQST_ROM_EPO_CROK, L"Croket! Itada Kinka! Banker Battle!! (Japan)", L"2003", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX },
	{ DRGQST_ROM_TAK_ZUBA, L"Zuba Zuba Blade (Japan)", L"2002", L"Takara / SSD", XAVIX_GAME_PLATFORM_XAVIX },
	{ DRGQST_ROM_DUELMAST, L"Duel Masters: Duel Station (Japan)", L"2003", L"Takara / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 },
	{ DRGQST_ROM_EPO_GOLF, L"Super Shot! Excite Golf (Japan)", L"2003", L"Epoch / SSD", XAVIX_GAME_PLATFORM_XAVIX2000 }
};

static void set_error(wchar_t *error, size_t length, const wchar_t *message)
{
	if (!error || !length)
		return;
	wcsncpy(error, message, length - 1);
	error[length - 1] = L'\0';
}

const xavix_game_metadata *xavix_game_metadata_for_kind(
	enum drgqst_rom_kind kind)
{
	size_t i;
	for (i = 0; i < sizeof(GAME_METADATA) / sizeof(GAME_METADATA[0]); ++i)
		if (GAME_METADATA[i].kind == kind)
			return &GAME_METADATA[i];
	return NULL;
}

enum xavix_game_support_status xavix_game_support_status_for_kind(
	enum drgqst_rom_kind kind)
{
	switch (kind)
	{
	case DRGQST_ROM_DRAGON_QUEST:
	case DRGQST_ROM_BAN_ONEP:
	case DRGQST_ROM_BAN_OMT:
	case DRGQST_ROM_TTV_LOTR:
	case DRGQST_ROM_TTV_SW:
	case DRGQST_ROM_TTV_SWJ:
	case DRGQST_ROM_BAN_NARU:
	case DRGQST_ROM_BAN_BLDJ:
		return XAVIX_GAME_SUPPORT_FULLY_PLAYABLE;
	case DRGQST_ROM_BAN_DB2J:
	case DRGQST_ROM_BAN_DBZ:
		return XAVIX_GAME_SUPPORT_PLAYABLE;
	case DRGQST_ROM_EPO_SSK2:
	case DRGQST_ROM_EPO_SSKJ:
	case DRGQST_ROM_UNKNOWN:
		return XAVIX_GAME_SUPPORT_NOT_WORKING;
	default:
		return XAVIX_GAME_SUPPORT_INITIAL;
	}
}

const wchar_t *xavix_game_platform_name(enum xavix_game_platform platform)
{
	switch (platform)
	{
	case XAVIX_GAME_PLATFORM_XAVIX2: return L"XaviX 2";
	case XAVIX_GAME_PLATFORM_XAVIX2000: return L"XaviX 2000";
	case XAVIX_GAME_PLATFORM_XAVIX:
	default: return L"XaviX";
	}
}

static int join_path(const wchar_t *left, const wchar_t *right,
	wchar_t output[MAX_PATH])
{
	size_t left_length = wcslen(left);
	size_t right_length = wcslen(right);
	int separator = left_length && left[left_length - 1] != L'\\';
	if (left_length + separator + right_length + 1 > MAX_PATH)
		return 0;
	memcpy(output, left, left_length * sizeof(*output));
	if (separator) output[left_length++] = L'\\';
	memcpy(output + left_length, right,
		(right_length + 1) * sizeof(*output));
	return 1;
}

static int has_zip_extension(const wchar_t *name)
{
	const wchar_t *dot = wcsrchr(name, L'.');
	return dot && _wcsicmp(dot, L".zip") == 0;
}

static void find_thumbnail(const wchar_t *snap_directory,
	const char *short_name, wchar_t output[MAX_PATH])
{
	WIN32_FIND_DATAW data;
	HANDLE search;
	wchar_t prefix[64];
	wchar_t candidate[MAX_PATH];
	wchar_t pattern[MAX_PATH];
	wchar_t exact[MAX_PATH];
	size_t i;

	output[0] = L'\0';
	for (i = 0; short_name[i] && i + 1 < sizeof(prefix) / sizeof(prefix[0]); ++i)
		prefix[i] = (wchar_t)(unsigned char)short_name[i];
	prefix[i] = L'\0';
	_snwprintf(candidate, sizeof(candidate) / sizeof(candidate[0]),
		L"%ls.png", prefix);
	candidate[sizeof(candidate) / sizeof(candidate[0]) - 1] = L'\0';
	if (join_path(snap_directory, candidate, exact) &&
		GetFileAttributesW(exact) != INVALID_FILE_ATTRIBUTES)
	{
		wcsncpy(output, exact, MAX_PATH - 1);
		output[MAX_PATH - 1] = L'\0';
		return;
	}
	_snwprintf(candidate, sizeof(candidate) / sizeof(candidate[0]),
		L"%ls-*.png", prefix);
	candidate[sizeof(candidate) / sizeof(candidate[0]) - 1] = L'\0';
	if (!join_path(snap_directory, candidate, pattern))
		return;
	search = FindFirstFileW(pattern, &data);
	if (search == INVALID_HANDLE_VALUE)
		return;
	do
	{
		if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
			(!output[0] || _wcsicmp(data.cFileName,
				wcsrchr(output, L'\\') + 1) < 0) &&
			join_path(snap_directory, data.cFileName, candidate))
		{
			wcsncpy(output, candidate, MAX_PATH - 1);
			output[MAX_PATH - 1] = L'\0';
		}
	} while (FindNextFileW(search, &data));
	FindClose(search);
}

static void scan_directory(const wchar_t *directory,
	const wchar_t *snap_directory, xavix_game_library *library)
{
	WIN32_FIND_DATAW data;
	HANDLE search;
	wchar_t pattern[MAX_PATH];
	wchar_t path[MAX_PATH];

	if (library->count >= XAVIX_GAME_LIBRARY_MAX_ENTRIES ||
		!join_path(directory, L"*", pattern))
		return;
	search = FindFirstFileW(pattern, &data);
	if (search == INVALID_HANDLE_VALUE)
		return;
	do
	{
		drgqst_rom_image image;
		wchar_t load_error[128];
		xavix_game_library_entry *entry;
		const xavix_game_metadata *metadata;
		if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..") ||
			!join_path(directory, data.cFileName, path))
			continue;
		if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
				scan_directory(path, snap_directory, library);
			continue;
		}
		if (!has_zip_extension(data.cFileName) ||
			library->count >= XAVIX_GAME_LIBRARY_MAX_ENTRIES)
			continue;
		memset(&image, 0, sizeof(image));
		if (!drgqst_rom_load_zip(path, &image, load_error,
			sizeof(load_error) / sizeof(load_error[0])))
			continue;
		metadata = xavix_game_metadata_for_kind(image.kind);
		drgqst_rom_release(&image);
		if (!metadata)
			continue;
		entry = &library->entries[library->count++];
		memset(entry, 0, sizeof(*entry));
		entry->metadata = metadata;
		wcsncpy(entry->path, path, MAX_PATH - 1);
		wcsncpy(entry->file_name, data.cFileName, MAX_PATH - 1);
		find_thumbnail(snap_directory,
			drgqst_rom_short_name(metadata->kind), entry->thumbnail);
	} while (FindNextFileW(search, &data));
	FindClose(search);
}

static enum xavix_game_library_sort ACTIVE_SORT;

static int compare_entries(const xavix_game_library_entry *left,
	const xavix_game_library_entry *right)
{
	int result;
	switch (ACTIVE_SORT)
	{
	case XAVIX_GAME_SORT_STATUS:
		result = xavix_game_support_status_for_kind(
			left->metadata->kind) - xavix_game_support_status_for_kind(
			right->metadata->kind);
		break;
	case XAVIX_GAME_SORT_RELEASE:
		result = _wcsicmp(left->metadata->release, right->metadata->release);
		break;
	case XAVIX_GAME_SORT_PLATFORM:
		result = left->metadata->platform - right->metadata->platform;
		break;
	case XAVIX_GAME_SORT_MAKER:
		result = _wcsicmp(left->metadata->maker, right->metadata->maker);
		break;
	case XAVIX_GAME_SORT_FILE:
		result = _wcsicmp(left->file_name, right->file_name);
		break;
	case XAVIX_GAME_SORT_TITLE:
	default:
		result = _wcsicmp(left->metadata->title, right->metadata->title);
		break;
	}
	return result ? result : _wcsicmp(left->metadata->title,
		right->metadata->title);
}

void xavix_game_library_sort_entries(xavix_game_library *library,
	enum xavix_game_library_sort sort)
{
	size_t i;
	if (!library)
		return;
	ACTIVE_SORT = sort;
	for (i = 1; i < library->count; ++i)
	{
		xavix_game_library_entry current = library->entries[i];
		size_t position = i;
		while (position && compare_entries(&current,
			&library->entries[position - 1]) < 0)
		{
			library->entries[position] = library->entries[position - 1];
			--position;
		}
		library->entries[position] = current;
	}
}

int xavix_game_library_scan(const wchar_t *rom_directory,
	const wchar_t *snap_directory, xavix_game_library *library,
	wchar_t *error, size_t error_length)
{
	DWORD attributes;
	if (!library || !rom_directory || !rom_directory[0] || !snap_directory)
	{
		set_error(error, error_length, L"No ROM directory was selected.");
		return 0;
	}
	attributes = GetFileAttributesW(rom_directory);
	if (attributes == INVALID_FILE_ATTRIBUTES ||
		!(attributes & FILE_ATTRIBUTE_DIRECTORY))
	{
		set_error(error, error_length, L"The selected ROM directory is unavailable.");
		return 0;
	}
	memset(library, 0, sizeof(*library));
	scan_directory(rom_directory, snap_directory, library);
	xavix_game_library_sort_entries(library, XAVIX_GAME_SORT_TITLE);
	set_error(error, error_length, L"");
	return 1;
}