// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "game_library.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
	return 1; } } while (0)

static int contains_kind(const xavix_game_library *library,
	enum drgqst_rom_kind kind)
{
	size_t i;
	for (i = 0; i < library->count; ++i)
		if (library->entries[i].metadata->kind == kind)
			return 1;
	return 0;
}

int main(int argc, char **argv)
{
	const xavix_game_metadata *metadata;
	xavix_game_library library;
	wchar_t directory[MAX_PATH];
	wchar_t error[384];

	metadata = xavix_game_metadata_for_kind(DRGQST_ROM_RAD_MTRK);
	CHECK(metadata && metadata->platform == XAVIX_GAME_PLATFORM_XAVIX);
	CHECK(metadata && !wcscmp(metadata->release, L"2003"));
	CHECK(wcsstr(metadata->title, L"Monster Truck") != NULL);
	metadata = xavix_game_metadata_for_kind(DRGQST_ROM_RAD_SSX);
	CHECK(metadata && wcsstr(metadata->title, L"SSX") != NULL);
	metadata = xavix_game_metadata_for_kind(DRGQST_ROM_TCARNAVI);
	CHECK(metadata && wcsstr(metadata->title, L"Carnavi") != NULL);	metadata = xavix_game_metadata_for_kind(DRGQST_ROM_DUELMAST);
	CHECK(metadata && metadata->platform == XAVIX_GAME_PLATFORM_XAVIX2000);
	CHECK(wcsstr(metadata->title, L"Duel Station") != NULL);
	metadata = xavix_game_metadata_for_kind(DRGQST_ROM_EPO_CROK);
	CHECK(metadata && metadata->platform == XAVIX_GAME_PLATFORM_XAVIX);
	CHECK(!wcscmp(xavix_game_platform_name(XAVIX_GAME_PLATFORM_XAVIX2),
		L"XaviX 2"));
	CHECK(xavix_game_support_status_for_kind(DRGQST_ROM_DRAGON_QUEST) ==
		XAVIX_GAME_SUPPORT_FULLY_PLAYABLE);
	CHECK(xavix_game_support_status_for_kind(DRGQST_ROM_BAN_NARU) ==
		XAVIX_GAME_SUPPORT_FULLY_PLAYABLE);
	CHECK(xavix_game_support_status_for_kind(DRGQST_ROM_BAN_DB2J) ==
		XAVIX_GAME_SUPPORT_PLAYABLE);
	CHECK(xavix_game_support_status_for_kind(DRGQST_ROM_BAN_DBZ) ==
		XAVIX_GAME_SUPPORT_PLAYABLE);
	CHECK(xavix_game_support_status_for_kind(DRGQST_ROM_TTV_MX) ==
		XAVIX_GAME_SUPPORT_INITIAL);
	CHECK(xavix_game_support_status_for_kind(DRGQST_ROM_EPO_SSK2) ==
		XAVIX_GAME_SUPPORT_NOT_WORKING);
	memset(&library, 0, sizeof(library));
	library.count = 3;
	library.entries[0].metadata = xavix_game_metadata_for_kind(
		DRGQST_ROM_EPO_SSK2);
	library.entries[1].metadata = xavix_game_metadata_for_kind(
		DRGQST_ROM_TTV_MX);
	library.entries[2].metadata = xavix_game_metadata_for_kind(
		DRGQST_ROM_DRAGON_QUEST);
	xavix_game_library_sort_entries(&library, XAVIX_GAME_SORT_STATUS);
	CHECK(library.entries[0].metadata->kind == DRGQST_ROM_DRAGON_QUEST);
	CHECK(library.entries[1].metadata->kind == DRGQST_ROM_TTV_MX);
	CHECK(library.entries[2].metadata->kind == DRGQST_ROM_EPO_SSK2);
	if (argc < 2)
	{
		puts("game_library_test: metadata passed");
		return 0;
	}
	CHECK(MultiByteToWideChar(CP_ACP, 0, argv[1], -1, directory,
		MAX_PATH) != 0);
	CHECK(xavix_game_library_scan(directory, L".", &library, error,
		sizeof(error) / sizeof(error[0])));
	if (contains_kind(&library, DRGQST_ROM_TOMTHR) ||
		contains_kind(&library, DRGQST_ROM_EPO_CROK))
	{
		CHECK(contains_kind(&library, DRGQST_ROM_TOMTHR));
		CHECK(contains_kind(&library, DRGQST_ROM_TAK_GIN));
		CHECK(contains_kind(&library, DRGQST_ROM_RAD_MTRK));
		CHECK(contains_kind(&library, DRGQST_ROM_RAD_SNOW));
		CHECK(contains_kind(&library, DRGQST_ROM_RAD_SSX));
		CHECK(contains_kind(&library, DRGQST_ROM_RAD_SBW));
		CHECK(contains_kind(&library, DRGQST_ROM_TCARNAVI));
		CHECK(contains_kind(&library, DRGQST_ROM_EPO_CROK));
		CHECK(contains_kind(&library, DRGQST_ROM_TAK_ZUBA));
	}
	if (contains_kind(&library, DRGQST_ROM_DUELMAST) ||
		contains_kind(&library, DRGQST_ROM_EPO_GOLF))
	{
		CHECK(contains_kind(&library, DRGQST_ROM_DUELMAST));
		CHECK(contains_kind(&library, DRGQST_ROM_EPO_GOLF));
	}	printf("game_library_test: scanned %u supported games\n",
		(unsigned)library.count);
	return 0;
}