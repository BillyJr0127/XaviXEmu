// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#ifndef XAVIXEMU_GAME_LIBRARY_H
#define XAVIXEMU_GAME_LIBRARY_H

#include "rom_loader.h"
#include <windows.h>

#include <stddef.h>
#include <wchar.h>

#define XAVIX_GAME_LIBRARY_MAX_ENTRIES 512

enum xavix_game_platform
{
	XAVIX_GAME_PLATFORM_XAVIX,
	XAVIX_GAME_PLATFORM_XAVIX2000,
	XAVIX_GAME_PLATFORM_XAVIX2
};

enum xavix_game_support_status
{
	XAVIX_GAME_SUPPORT_FULLY_PLAYABLE,
	XAVIX_GAME_SUPPORT_PLAYABLE,
	XAVIX_GAME_SUPPORT_INITIAL,
	XAVIX_GAME_SUPPORT_NOT_WORKING
};

enum xavix_game_library_sort
{
	XAVIX_GAME_SORT_TITLE,
	XAVIX_GAME_SORT_STATUS,
	XAVIX_GAME_SORT_RELEASE,
	XAVIX_GAME_SORT_PLATFORM,
	XAVIX_GAME_SORT_MAKER,
	XAVIX_GAME_SORT_FILE
};

typedef struct xavix_game_metadata
{
	enum drgqst_rom_kind kind;
	const wchar_t *title;
	const wchar_t *release;
	const wchar_t *maker;
	enum xavix_game_platform platform;
} xavix_game_metadata;

typedef struct xavix_game_library_entry
{
	const xavix_game_metadata *metadata;
	wchar_t path[MAX_PATH];
	wchar_t file_name[MAX_PATH];
	wchar_t thumbnail[MAX_PATH];
} xavix_game_library_entry;

typedef struct xavix_game_library
{
	xavix_game_library_entry entries[XAVIX_GAME_LIBRARY_MAX_ENTRIES];
	size_t count;
} xavix_game_library;

const xavix_game_metadata *xavix_game_metadata_for_kind(
	enum drgqst_rom_kind kind);
enum xavix_game_support_status xavix_game_support_status_for_kind(
	enum drgqst_rom_kind kind);
const wchar_t *xavix_game_platform_name(enum xavix_game_platform platform);
int xavix_game_library_scan(const wchar_t *rom_directory,
	const wchar_t *snap_directory, xavix_game_library *library,
	wchar_t *error, size_t error_length);
void xavix_game_library_sort_entries(xavix_game_library *library,
	enum xavix_game_library_sort sort);

#endif