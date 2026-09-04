// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#ifndef XAVIXEMU_SCREENSHOT_H
#define XAVIXEMU_SCREENSHOT_H

#include <windows.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int xavix_screenshot_save_png(
	HDC source,
	const RECT *source_rectangle,
	const wchar_t *base_directory,
	wchar_t *saved_path,
	size_t saved_path_length,
	wchar_t *error,
	size_t error_length);

int xavix_screenshot_save_png_named(
	HDC source,
	const RECT *source_rectangle,
	const wchar_t *base_directory,
	const wchar_t *file_prefix,
	wchar_t *saved_path,
	size_t saved_path_length,
	wchar_t *error,
	size_t error_length);

#ifdef __cplusplus
}
#endif

#endif
