// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "screenshot.h"

#include <stdint.h>
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

static int create_test_root(wchar_t path[MAX_PATH])
{
	wchar_t temporary_directory[MAX_PATH];
	DWORD length = GetTempPathW(MAX_PATH, temporary_directory);

	if (!length || length >= MAX_PATH ||
		!GetTempFileNameW(temporary_directory, L"xss", 0, path) ||
		!DeleteFileW(path))
		return 0;
	return !!CreateDirectoryW(path, NULL);
}

static uint32_t read_be32(const uint8_t *data)
{
	return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
		((uint32_t)data[2] << 8) | data[3];
}

static int read_png_header(const wchar_t *path, uint8_t header[24])
{
	HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	DWORD received = 0;
	int success;

	if (file == INVALID_HANDLE_VALUE)
		return 0;
	success = ReadFile(file, header, 24, &received, NULL) && received == 24;
	CloseHandle(file);
	return success;
}

int main(void)
{
	wchar_t root[MAX_PATH];
	wchar_t first_path[MAX_PATH] = L"";
	wchar_t second_path[MAX_PATH] = L"";
	wchar_t snap_directory[MAX_PATH];
	wchar_t error[384];
	BITMAPINFO bitmap_info;
	HDC screen = NULL;
	HDC source = NULL;
	HBITMAP bitmap = NULL;
	HGDIOBJ old_bitmap = NULL;
	uint32_t *pixels = NULL;
	RECT crop = { 8, 9, 40, 33 };
	uint8_t header[24];
	unsigned index;

	if (!create_test_root(root))
	{
		CHECK(0 && "create_test_root");
		return EXIT_FAILURE;
	}
	memset(&bitmap_info, 0, sizeof(bitmap_info));
	bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
	bitmap_info.bmiHeader.biWidth = 64;
	bitmap_info.bmiHeader.biHeight = -48;
	bitmap_info.bmiHeader.biPlanes = 1;
	bitmap_info.bmiHeader.biBitCount = 32;
	bitmap_info.bmiHeader.biCompression = BI_RGB;
	screen = GetDC(NULL);
	source = CreateCompatibleDC(screen);
	bitmap = CreateDIBSection(screen, &bitmap_info, DIB_RGB_COLORS,
		(void **)&pixels, NULL, 0);
	if (screen)
		ReleaseDC(NULL, screen);
	CHECK(source != NULL);
	CHECK(bitmap != NULL);
	CHECK(pixels != NULL);
	if (source && bitmap && pixels)
	{
		old_bitmap = SelectObject(source, bitmap);
		CHECK(old_bitmap != NULL && old_bitmap != HGDI_ERROR);
		for (index = 0; index < 64U * 48U; ++index)
			pixels[index] = UINT32_C(0x002060a0) ^
				((uint32_t)(index & 0xff) << 8);
		CHECK(xavix_screenshot_save_png(source, &crop, root,
			first_path, MAX_PATH, error,
			sizeof(error) / sizeof(error[0])));
		CHECK(xavix_screenshot_save_png(source, &crop, root,
			second_path, MAX_PATH, error,
			sizeof(error) / sizeof(error[0])));
		CHECK(first_path[0] != L'\0');
		CHECK(second_path[0] != L'\0');
		CHECK(wcscmp(first_path, second_path) != 0);
		CHECK(read_png_header(first_path, header));
		if (read_png_header(first_path, header))
		{
			static const uint8_t png_signature[8] =
				{ 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
			CHECK(!memcmp(header, png_signature, sizeof(png_signature)));
			CHECK(read_be32(header + 16) == 32);
			CHECK(read_be32(header + 20) == 24);
		}
	}
	if (old_bitmap && old_bitmap != HGDI_ERROR)
		SelectObject(source, old_bitmap);
	if (bitmap)
		DeleteObject(bitmap);
	if (source)
		DeleteDC(source);
	if (first_path[0])
		DeleteFileW(first_path);
	if (second_path[0])
		DeleteFileW(second_path);
	_snwprintf(snap_directory,
		sizeof(snap_directory) / sizeof(snap_directory[0]),
		L"%ls\\snap", root);
	snap_directory[sizeof(snap_directory) / sizeof(snap_directory[0]) - 1] = L'\0';
	RemoveDirectoryW(snap_directory);
	RemoveDirectoryW(root);

	if (failures)
	{
		fprintf(stderr, "%u screenshot test(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	puts("screenshot: all tests passed");
	return EXIT_SUCCESS;
}
