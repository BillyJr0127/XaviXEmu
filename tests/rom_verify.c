// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "rom_loader.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

int main(int argument_count, char **arguments)
{
	wchar_t path[32768];
	wchar_t error[384];
	drgqst_rom_image image = { 0 };
	int length;

	if (argument_count != 2)
	{
		fprintf(stderr, "usage: rom-verify <zip>\n");
		return 64;
	}

	length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, arguments[1], -1, path, sizeof(path) / sizeof(path[0]));
	if (!length)
	{
		fprintf(stderr, "path is not valid UTF-8\n");
		return 65;
	}

	if (!drgqst_rom_load_zip(path, &image, error, sizeof(error) / sizeof(error[0])))
	{
		fwprintf(stderr, L"%ls\n", error);
		return 2;
	}

	printf("game=%s size=%zu crc32=%08X\n",
		drgqst_rom_short_name(image.kind), image.size, image.crc32);
	drgqst_rom_release(&image);
	return 0;
}
