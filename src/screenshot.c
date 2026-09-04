// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#ifndef COBJMACROS
#define COBJMACROS
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>

#include "screenshot.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

enum
{
	MAXIMUM_PATH_CHARACTERS = MAX_PATH,
	MAXIMUM_FILENAME_ATTEMPTS = 1000
};

static void clear_error(wchar_t *error, size_t error_length)
{
	if (error && error_length)
		error[0] = L'\0';
}

static void set_error(wchar_t *error, size_t error_length,
	const wchar_t *format, ...)
{
	va_list arguments;

	if (!error || !error_length)
		return;
	va_start(arguments, format);
	_vsnwprintf(error, error_length, format, arguments);
	va_end(arguments);
	error[error_length - 1] = L'\0';
}

static int join_path(const wchar_t *directory, const wchar_t *name,
	wchar_t output[MAXIMUM_PATH_CHARACTERS], wchar_t *error,
	size_t error_length)
{
	size_t directory_length;
	size_t name_length;
	int separator;

	if (!directory || !directory[0] || !name || !name[0])
	{
		set_error(error, error_length, L"The screenshot path is not valid.");
		return 0;
	}
	directory_length = wcslen(directory);
	name_length = wcslen(name);
	separator = directory[directory_length - 1] != L'\\' &&
		directory[directory_length - 1] != L'/';
	if (directory_length + (size_t)separator + name_length >=
		MAXIMUM_PATH_CHARACTERS)
	{
		set_error(error, error_length, L"The screenshot path is too long.");
		return 0;
	}
	memcpy(output, directory, directory_length * sizeof(*output));
	if (separator)
		output[directory_length++] = L'\\';
	memcpy(output + directory_length, name,
		(name_length + 1) * sizeof(*output));
	return 1;
}

static int ensure_directory(const wchar_t *directory, wchar_t *error,
	size_t error_length)
{
	DWORD windows_error;
	DWORD attributes;

	if (CreateDirectoryW(directory, NULL))
		return 1;
	windows_error = GetLastError();
	if (windows_error == ERROR_ALREADY_EXISTS)
	{
		attributes = GetFileAttributesW(directory);
		if (attributes != INVALID_FILE_ATTRIBUTES &&
			(attributes & FILE_ATTRIBUTE_DIRECTORY))
			return 1;
	}
	set_error(error, error_length,
		L"The snap directory could not be created (Windows error %lu).",
		windows_error);
	return 0;
}

static int choose_output_path(const wchar_t *snap_directory,
	const wchar_t *prefix, wchar_t output[MAXIMUM_PATH_CHARACTERS],
	wchar_t *error, size_t error_length)
{
	SYSTEMTIME time;
	unsigned attempt;

	GetLocalTime(&time);
	for (attempt = 0; attempt < MAXIMUM_FILENAME_ATTEMPTS; ++attempt)
	{
		wchar_t filename[96];
		HANDLE file;
		DWORD windows_error;

		if (!attempt)
		{
			_snwprintf(filename, sizeof(filename) / sizeof(filename[0]),
				L"%ls-%04u%02u%02u-%02u%02u%02u-%03u.png",
				prefix, time.wYear, time.wMonth, time.wDay, time.wHour,
				time.wMinute, time.wSecond, time.wMilliseconds);
		}
		else
		{
			_snwprintf(filename, sizeof(filename) / sizeof(filename[0]),
				L"%ls-%04u%02u%02u-%02u%02u%02u-%03u-%03u.png",
				prefix, time.wYear, time.wMonth, time.wDay, time.wHour,
				time.wMinute, time.wSecond, time.wMilliseconds, attempt);
		}
		filename[sizeof(filename) / sizeof(filename[0]) - 1] = L'\0';
		if (!join_path(snap_directory, filename, output, error,
			error_length))
			return 0;
		file = CreateFileW(output, GENERIC_WRITE, 0, NULL, CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL, NULL);
		if (file != INVALID_HANDLE_VALUE)
		{
			CloseHandle(file);
			return 1;
		}
		windows_error = GetLastError();
		if (windows_error == ERROR_FILE_EXISTS ||
			windows_error == ERROR_ALREADY_EXISTS)
			continue;
		set_error(error, error_length,
			L"The screenshot file could not be reserved (Windows error %lu).",
			windows_error);
		return 0;
	}
	set_error(error, error_length,
		L"A unique screenshot filename could not be created.");
	return 0;
}

static HBITMAP create_cropped_bitmap(HDC source, const RECT *rectangle,
	int *width, int *height, wchar_t *error, size_t error_length)
{
	BITMAPINFO bitmap_info;
	HDC destination;
	HBITMAP bitmap;
	HGDIOBJ old_bitmap;
	void *pixels = NULL;

	*width = rectangle->right - rectangle->left;
	*height = rectangle->bottom - rectangle->top;
	if (*width <= 0 || *height <= 0)
	{
		set_error(error, error_length,
			L"The screenshot dimensions are not valid.");
		return NULL;
	}
	memset(&bitmap_info, 0, sizeof(bitmap_info));
	bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
	bitmap_info.bmiHeader.biWidth = *width;
	bitmap_info.bmiHeader.biHeight = -*height;
	bitmap_info.bmiHeader.biPlanes = 1;
	bitmap_info.bmiHeader.biBitCount = 32;
	bitmap_info.bmiHeader.biCompression = BI_RGB;
	bitmap = CreateDIBSection(source, &bitmap_info, DIB_RGB_COLORS,
		&pixels, NULL, 0);
	if (!bitmap || !pixels)
	{
		set_error(error, error_length,
			L"The screenshot bitmap could not be created (Windows error %lu).",
			GetLastError());
		if (bitmap)
			DeleteObject(bitmap);
		return NULL;
	}
	destination = CreateCompatibleDC(source);
	if (!destination)
	{
		set_error(error, error_length,
			L"The screenshot drawing context could not be created (Windows error %lu).",
			GetLastError());
		DeleteObject(bitmap);
		return NULL;
	}
	old_bitmap = SelectObject(destination, bitmap);
	if (!old_bitmap || old_bitmap == HGDI_ERROR ||
		!BitBlt(destination, 0, 0, *width, *height, source,
			rectangle->left, rectangle->top, SRCCOPY))
	{
		set_error(error, error_length,
			L"The displayed image could not be copied (Windows error %lu).",
			GetLastError());
		if (old_bitmap && old_bitmap != HGDI_ERROR)
			SelectObject(destination, old_bitmap);
		DeleteDC(destination);
		DeleteObject(bitmap);
		return NULL;
	}
	GdiFlush();
	SelectObject(destination, old_bitmap);
	DeleteDC(destination);
	return bitmap;
}

static int write_png(const wchar_t *path, HBITMAP bitmap, UINT width,
	UINT height, wchar_t *error, size_t error_length)
{
	IWICImagingFactory *factory = NULL;
	IWICBitmap *source = NULL;
	IWICStream *stream = NULL;
	IWICBitmapEncoder *encoder = NULL;
	IWICBitmapFrameEncode *frame = NULL;
	IPropertyBag2 *properties = NULL;
	WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGR;
	HRESULT initialize_result;
	HRESULT result;
	int uninitialize = 0;
	int success = 0;

	initialize_result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	if (SUCCEEDED(initialize_result))
		uninitialize = 1;
	else if (initialize_result != RPC_E_CHANGED_MODE)
	{
		set_error(error, error_length,
			L"Windows imaging could not be initialized (0x%08lx).",
			(unsigned long)initialize_result);
		return 0;
	}
	result = CoCreateInstance(&CLSID_WICImagingFactory, NULL,
		CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&factory);
	if (SUCCEEDED(result))
		result = IWICImagingFactory_CreateBitmapFromHBITMAP(factory,
			bitmap, NULL, WICBitmapIgnoreAlpha, &source);
	if (SUCCEEDED(result))
		result = IWICImagingFactory_CreateStream(factory, &stream);
	if (SUCCEEDED(result))
		result = IWICStream_InitializeFromFilename(stream, path,
			GENERIC_WRITE);
	if (SUCCEEDED(result))
		result = IWICImagingFactory_CreateEncoder(factory,
			&GUID_ContainerFormatPng, NULL, &encoder);
	if (SUCCEEDED(result))
		result = IWICBitmapEncoder_Initialize(encoder, (IStream *)stream,
			WICBitmapEncoderNoCache);
	if (SUCCEEDED(result))
		result = IWICBitmapEncoder_CreateNewFrame(encoder, &frame,
			&properties);
	if (SUCCEEDED(result))
		result = IWICBitmapFrameEncode_Initialize(frame, properties);
	if (SUCCEEDED(result))
		result = IWICBitmapFrameEncode_SetSize(frame, width, height);
	if (SUCCEEDED(result))
		result = IWICBitmapFrameEncode_SetPixelFormat(frame, &pixel_format);
	if (SUCCEEDED(result))
		result = IWICBitmapFrameEncode_WriteSource(frame,
			(IWICBitmapSource *)source, NULL);
	if (SUCCEEDED(result))
		result = IWICBitmapFrameEncode_Commit(frame);
	if (SUCCEEDED(result))
		result = IWICBitmapEncoder_Commit(encoder);
	if (SUCCEEDED(result))
		success = 1;
	else
		set_error(error, error_length,
			L"The PNG screenshot could not be written (0x%08lx).",
			(unsigned long)result);

	if (properties)
		IPropertyBag2_Release(properties);
	if (frame)
		IWICBitmapFrameEncode_Release(frame);
	if (encoder)
		IWICBitmapEncoder_Release(encoder);
	if (stream)
		IWICStream_Release(stream);
	if (source)
		IWICBitmap_Release(source);
	if (factory)
		IWICImagingFactory_Release(factory);
	if (uninitialize)
		CoUninitialize();
	return success;
}

int xavix_screenshot_save_png_named(HDC source, const RECT *source_rectangle,
	const wchar_t *base_directory, const wchar_t *file_prefix,
	wchar_t *saved_path, size_t saved_path_length,
	wchar_t *error, size_t error_length)
{
	wchar_t snap_directory[MAXIMUM_PATH_CHARACTERS];
	wchar_t path[MAXIMUM_PATH_CHARACTERS];
	HBITMAP bitmap;
	int width;
	int height;
	int success;

	clear_error(error, error_length);
	if (saved_path && saved_path_length)
		saved_path[0] = L'\0';
	if (!source || !source_rectangle || !base_directory ||
		!file_prefix || !file_prefix[0])
	{
		set_error(error, error_length,
			L"No screenshot source or destination was supplied.");
		return 0;
	}
	if (!join_path(base_directory, L"snap", snap_directory, error,
		error_length) ||
		!ensure_directory(snap_directory, error, error_length) ||
		!choose_output_path(snap_directory, file_prefix, path, error, error_length))
		return 0;
	bitmap = create_cropped_bitmap(source, source_rectangle, &width, &height,
		error, error_length);
	if (!bitmap)
	{
		DeleteFileW(path);
		return 0;
	}
	success = write_png(path, bitmap, (UINT)width, (UINT)height, error,
		error_length);
	DeleteObject(bitmap);
	if (!success)
	{
		DeleteFileW(path);
		return 0;
	}
	if (saved_path && saved_path_length)
	{
		const size_t required = wcslen(path) + 1;
		if (required > saved_path_length)
		{
			set_error(error, error_length,
				L"The screenshot path buffer is too small.");
			DeleteFileW(path);
			return 0;
		}
		memcpy(saved_path, path, required * sizeof(*saved_path));
	}
	return 1;
}

int xavix_screenshot_save_png(HDC source, const RECT *source_rectangle,
	const wchar_t *base_directory, wchar_t *saved_path,
	size_t saved_path_length, wchar_t *error, size_t error_length)
{
	return xavix_screenshot_save_png_named(source, source_rectangle,
		base_directory, L"XaviXEmu", saved_path, saved_path_length,
		error, error_length);
}