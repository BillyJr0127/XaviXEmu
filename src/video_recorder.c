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
#include <vfw.h>

#include "video_recorder.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

enum
{
	VIDEO_FRAME_RATE = 60,
	AUDIO_SAMPLE_RATE = 48000,
	AUDIO_CHANNELS = 2,
	AUDIO_BITS = 16,
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

static void copy_path(wchar_t *output, size_t output_length,
	const wchar_t *path)
{
	if (!output || !output_length)
		return;
	if (!path)
		path = L"";
	wcsncpy(output, path, output_length - 1);
	output[output_length - 1] = L'\0';
}

static int choose_output_path(const wchar_t *directory,
	wchar_t output[MAX_PATH], wchar_t *error, size_t error_length)
{
	SYSTEMTIME time;
	size_t directory_length;
	unsigned attempt;
	int separator;

	if (!directory || !directory[0])
	{
		set_error(error, error_length, L"The recording directory is not valid.");
		return 0;
	}
	directory_length = wcslen(directory);
	separator = directory[directory_length - 1] != L'\\' &&
		directory[directory_length - 1] != L'/';
	GetLocalTime(&time);
	for (attempt = 0; attempt < MAXIMUM_FILENAME_ATTEMPTS; ++attempt)
	{
		wchar_t filename[96];
		size_t output_length = directory_length;
		int result;

		if (!attempt)
			result = _snwprintf(filename,
				sizeof(filename) / sizeof(filename[0]),
				L"XaviXEmu-%04u%02u%02u-%02u%02u%02u-%03u.avi",
				time.wYear, time.wMonth, time.wDay, time.wHour,
				time.wMinute, time.wSecond, time.wMilliseconds);
		else
			result = _snwprintf(filename,
				sizeof(filename) / sizeof(filename[0]),
				L"XaviXEmu-%04u%02u%02u-%02u%02u%02u-%03u-%03u.avi",
				time.wYear, time.wMonth, time.wDay, time.wHour,
				time.wMinute, time.wSecond, time.wMilliseconds, attempt);
		if (result < 0 || (size_t)result >=
			sizeof(filename) / sizeof(filename[0]) ||
			directory_length + (size_t)separator + (size_t)result >= MAX_PATH)
		{
			set_error(error, error_length, L"The recording path is too long.");
			return 0;
		}
		memcpy(output, directory, output_length * sizeof(*output));
		if (separator)
			output[output_length++] = L'\\';
		memcpy(output + output_length, filename,
			((size_t)result + 1) * sizeof(*output));
		if (GetFileAttributesW(output) == INVALID_FILE_ATTRIBUTES &&
			(GetLastError() == ERROR_FILE_NOT_FOUND ||
			 GetLastError() == ERROR_PATH_NOT_FOUND))
			return 1;
	}
	set_error(error, error_length,
		L"A unique AVI filename could not be created.");
	return 0;
}

static void release_recorder(xavix_video_recorder *recorder,
	int delete_partial)
{
	wchar_t partial_path[MAX_PATH];

	if (!recorder)
		return;
	copy_path(partial_path, sizeof(partial_path) / sizeof(partial_path[0]),
		recorder->path);
	if (recorder->audio_stream)
		AVIStreamRelease((PAVISTREAM)recorder->audio_stream);
	if (recorder->video_stream)
		AVIStreamRelease((PAVISTREAM)recorder->video_stream);
	if (recorder->file)
		AVIFileRelease((PAVIFILE)recorder->file);
	if (recorder->imaging_factory)
		IWICImagingFactory_Release(
			(IWICImagingFactory *)recorder->imaging_factory);
	if (recorder->avi_initialized)
		AVIFileExit();
	if (recorder->com_uninitialize)
		CoUninitialize();
	memset(recorder, 0, sizeof(*recorder));
	if (delete_partial && partial_path[0])
		DeleteFileW(partial_path);
}

void xavix_video_recorder_init(xavix_video_recorder *recorder)
{
	if (recorder)
		memset(recorder, 0, sizeof(*recorder));
}

int xavix_video_recorder_active(const xavix_video_recorder *recorder)
{
	return recorder && recorder->file && recorder->video_stream &&
		recorder->audio_stream;
}

int xavix_video_recorder_start(xavix_video_recorder *recorder,
	const wchar_t *directory, unsigned width, unsigned height,
	wchar_t *saved_path, size_t saved_path_length,
	wchar_t *error, size_t error_length)
{
	AVISTREAMINFOW stream_info;
	BITMAPINFOHEADER bitmap_format;
	WAVEFORMATEX wave_format;
	PAVIFILE file = NULL;
	PAVISTREAM video = NULL;
	PAVISTREAM audio = NULL;
	IWICImagingFactory *factory = NULL;
	HRESULT result;
	HRESULT initialize_result;

	clear_error(error, error_length);
	copy_path(saved_path, saved_path_length, L"");
	if (!recorder || xavix_video_recorder_active(recorder))
	{
		set_error(error, error_length, L"A recording is already active.");
		return 0;
	}
	if (!width || !height || width > INT_MAX || height > INT_MAX ||
		(uint64_t)width * height * 4 > UINT_MAX)
	{
		set_error(error, error_length, L"The recording dimensions are invalid.");
		return 0;
	}
	memset(recorder, 0, sizeof(*recorder));
	if (!choose_output_path(directory, recorder->path, error, error_length))
		return 0;

	initialize_result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	if (SUCCEEDED(initialize_result))
		recorder->com_uninitialize = 1;
	else if (initialize_result != RPC_E_CHANGED_MODE)
	{
		set_error(error, error_length,
			L"Windows imaging initialization failed (0x%08lx).",
			(unsigned long)initialize_result);
		recorder->path[0] = L'\0';
		return 0;
	}
	result = CoCreateInstance(&CLSID_WICImagingFactory, NULL,
		CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&factory);
	if (FAILED(result))
	{
		set_error(error, error_length,
			L"The JPEG encoder is not available (0x%08lx).",
			(unsigned long)result);
		release_recorder(recorder, 1);
		return 0;
	}
	recorder->imaging_factory = factory;

	AVIFileInit();
	recorder->avi_initialized = 1;
	result = AVIFileOpenW(&file, recorder->path,
		OF_CREATE | OF_WRITE, NULL);
	if (FAILED(result))
	{
		set_error(error, error_length,
			L"The AVI file could not be created (0x%08lx).",
			(unsigned long)result);
		release_recorder(recorder, 1);
		return 0;
	}
	recorder->file = file;

	memset(&stream_info, 0, sizeof(stream_info));
	stream_info.fccType = streamtypeVIDEO;
	stream_info.fccHandler = mmioFOURCC('M', 'J', 'P', 'G');
	stream_info.dwScale = 1;
	stream_info.dwRate = VIDEO_FRAME_RATE;
	stream_info.dwSuggestedBufferSize = width * height;
	stream_info.dwQuality = (DWORD)-1;
	SetRect(&stream_info.rcFrame, 0, 0, (int)width, (int)height);
	result = AVIFileCreateStreamW(file, &video, &stream_info);
	if (FAILED(result))
	{
		set_error(error, error_length,
			L"The AVI video stream could not be created (0x%08lx).",
			(unsigned long)result);
		release_recorder(recorder, 1);
		return 0;
	}
	recorder->video_stream = video;
	memset(&bitmap_format, 0, sizeof(bitmap_format));
	bitmap_format.biSize = sizeof(bitmap_format);
	bitmap_format.biWidth = (LONG)width;
	bitmap_format.biHeight = (LONG)height;
	bitmap_format.biPlanes = 1;
	bitmap_format.biBitCount = 24;
	bitmap_format.biCompression = mmioFOURCC('M', 'J', 'P', 'G');
	bitmap_format.biSizeImage = width * height * 3;
	result = AVIStreamSetFormat(video, 0, &bitmap_format,
		sizeof(bitmap_format));
	if (FAILED(result))
	{
		set_error(error, error_length,
			L"The MJPEG stream format was rejected (0x%08lx).",
			(unsigned long)result);
		release_recorder(recorder, 1);
		return 0;
	}

	memset(&wave_format, 0, sizeof(wave_format));
	wave_format.wFormatTag = WAVE_FORMAT_PCM;
	wave_format.nChannels = AUDIO_CHANNELS;
	wave_format.nSamplesPerSec = AUDIO_SAMPLE_RATE;
	wave_format.wBitsPerSample = AUDIO_BITS;
	wave_format.nBlockAlign =
		(AUDIO_CHANNELS * AUDIO_BITS) / 8;
	wave_format.nAvgBytesPerSec =
		wave_format.nSamplesPerSec * wave_format.nBlockAlign;
	memset(&stream_info, 0, sizeof(stream_info));
	stream_info.fccType = streamtypeAUDIO;
	stream_info.dwScale = wave_format.nBlockAlign;
	stream_info.dwRate = wave_format.nAvgBytesPerSec;
	stream_info.dwSampleSize = wave_format.nBlockAlign;
	stream_info.dwQuality = (DWORD)-1;
	result = AVIFileCreateStreamW(file, &audio, &stream_info);
	if (FAILED(result))
	{
		set_error(error, error_length,
			L"The AVI audio stream could not be created (0x%08lx).",
			(unsigned long)result);
		release_recorder(recorder, 1);
		return 0;
	}
	recorder->audio_stream = audio;
	result = AVIStreamSetFormat(audio, 0, &wave_format,
		sizeof(wave_format));
	if (FAILED(result))
	{
		set_error(error, error_length,
			L"The PCM audio stream format was rejected (0x%08lx).",
			(unsigned long)result);
		release_recorder(recorder, 1);
		return 0;
	}

	recorder->width = width;
	recorder->height = height;
	copy_path(saved_path, saved_path_length, recorder->path);
	return 1;
}

static int encode_jpeg(xavix_video_recorder *recorder,
	const uint32_t *pixels, unsigned pixel_stride,
	IStream **encoded_stream, ULARGE_INTEGER *encoded_size,
	wchar_t *error, size_t error_length)
{
	IWICImagingFactory *factory =
		(IWICImagingFactory *)recorder->imaging_factory;
	IWICBitmap *source = NULL;
	IWICBitmapEncoder *encoder = NULL;
	IWICBitmapFrameEncode *frame = NULL;
	IPropertyBag2 *properties = NULL;
	IStream *stream = NULL;
	WICPixelFormatGUID pixel_format = GUID_WICPixelFormat24bppBGR;
	STATSTG statistics;
	HRESULT result;
	int success = 0;

	result = CreateStreamOnHGlobal(NULL, TRUE, &stream);
	if (SUCCEEDED(result))
		result = IWICImagingFactory_CreateBitmapFromMemory(factory,
			recorder->width, recorder->height,
			&GUID_WICPixelFormat32bppBGR, pixel_stride * 4,
			(unsigned)(((uint64_t)(recorder->height - 1) * pixel_stride +
				recorder->width) * 4), (BYTE *)pixels, &source);
	if (SUCCEEDED(result))
		result = IWICImagingFactory_CreateEncoder(factory,
			&GUID_ContainerFormatJpeg, NULL, &encoder);
	if (SUCCEEDED(result))
		result = IWICBitmapEncoder_Initialize(encoder, stream,
			WICBitmapEncoderNoCache);
	if (SUCCEEDED(result))
		result = IWICBitmapEncoder_CreateNewFrame(encoder, &frame,
			&properties);
	if (SUCCEEDED(result) && properties)
	{
		PROPBAG2 option;
		VARIANT value;
		memset(&option, 0, sizeof(option));
		option.pstrName = L"ImageQuality";
		VariantInit(&value);
		value.vt = VT_R4;
		value.fltVal = 0.85f;
		(void)IPropertyBag2_Write(properties, 1, &option, &value);
		VariantClear(&value);
	}
	if (SUCCEEDED(result))
		result = IWICBitmapFrameEncode_Initialize(frame, properties);
	if (SUCCEEDED(result))
		result = IWICBitmapFrameEncode_SetSize(frame,
			recorder->width, recorder->height);
	if (SUCCEEDED(result))
		result = IWICBitmapFrameEncode_SetPixelFormat(frame, &pixel_format);
	if (SUCCEEDED(result))
		result = IWICBitmapFrameEncode_WriteSource(frame,
			(IWICBitmapSource *)source, NULL);
	if (SUCCEEDED(result))
		result = IWICBitmapFrameEncode_Commit(frame);
	if (SUCCEEDED(result))
		result = IWICBitmapEncoder_Commit(encoder);
	memset(&statistics, 0, sizeof(statistics));
	if (SUCCEEDED(result))
		result = IStream_Stat(stream, &statistics, STATFLAG_NONAME);
	if (SUCCEEDED(result) && statistics.cbSize.QuadPart > 0 &&
		statistics.cbSize.QuadPart <= LONG_MAX)
	{
		*encoded_stream = stream;
		*encoded_size = statistics.cbSize;
		stream = NULL;
		success = 1;
	}
	else if (SUCCEEDED(result))
		result = E_FAIL;
	if (!success)
		set_error(error, error_length,
			L"A video frame could not be JPEG-compressed (0x%08lx).",
			(unsigned long)result);
	if (properties) IPropertyBag2_Release(properties);
	if (frame) IWICBitmapFrameEncode_Release(frame);
	if (encoder) IWICBitmapEncoder_Release(encoder);
	if (source) IWICBitmap_Release(source);
	if (stream) IStream_Release(stream);
	return success;
}

int xavix_video_recorder_write_frame(xavix_video_recorder *recorder,
	const uint32_t *pixels, unsigned pixel_stride,
	const int16_t *interleaved_stereo, size_t audio_frames,
	wchar_t *error, size_t error_length)
{
	IStream *encoded_stream = NULL;
	ULARGE_INTEGER encoded_size;
	HGLOBAL memory = NULL;
	void *encoded = NULL;
	HRESULT result;
	int success = 0;

	clear_error(error, error_length);
	if (!xavix_video_recorder_active(recorder) || !pixels ||
		pixel_stride < recorder->width || pixel_stride > UINT_MAX / 4 ||
		((uint64_t)(recorder->height - 1) * pixel_stride +
			recorder->width) * 4 > UINT_MAX ||
		!interleaved_stereo || audio_frames > LONG_MAX / 4 ||
		recorder->video_frame >= LONG_MAX ||
		recorder->audio_frame > (uint32_t)LONG_MAX - audio_frames)
	{
		set_error(error, error_length, L"The recording frame is not valid.");
		return 0;
	}
	if (!encode_jpeg(recorder, pixels, pixel_stride, &encoded_stream,
		&encoded_size, error, error_length))
		return 0;
	result = GetHGlobalFromStream(encoded_stream, &memory);
	if (SUCCEEDED(result))
		encoded = GlobalLock(memory);
	if (!encoded)
	{
		set_error(error, error_length,
			L"The compressed video frame could not be accessed.");
		goto done;
	}
	result = AVIStreamWrite((PAVISTREAM)recorder->video_stream,
		(LONG)recorder->video_frame, 1, encoded,
		(LONG)encoded_size.QuadPart, AVIIF_KEYFRAME, NULL, NULL);
	GlobalUnlock(memory);
	encoded = NULL;
	if (FAILED(result))
	{
		set_error(error, error_length,
			L"The video frame could not be written (0x%08lx).",
			(unsigned long)result);
		goto done;
	}
	result = AVIStreamWrite((PAVISTREAM)recorder->audio_stream,
		(LONG)recorder->audio_frame, (LONG)audio_frames,
		(LPVOID)interleaved_stereo,
		(LONG)(audio_frames * AUDIO_CHANNELS * sizeof(int16_t)),
		0, NULL, NULL);
	if (FAILED(result))
	{
		set_error(error, error_length,
			L"The audio frame could not be written (0x%08lx).",
			(unsigned long)result);
		goto done;
	}
	recorder->video_frame++;
	recorder->audio_frame += (uint32_t)audio_frames;
	success = 1;

done:
	if (encoded)
		GlobalUnlock(memory);
	if (encoded_stream)
		IStream_Release(encoded_stream);
	return success;
}

int xavix_video_recorder_stop(xavix_video_recorder *recorder,
	wchar_t *saved_path, size_t saved_path_length,
	wchar_t *error, size_t error_length)
{
	wchar_t path[MAX_PATH];

	clear_error(error, error_length);
	copy_path(saved_path, saved_path_length, L"");
	if (!xavix_video_recorder_active(recorder))
	{
		set_error(error, error_length, L"No recording is active.");
		return 0;
	}
	copy_path(path, sizeof(path) / sizeof(path[0]), recorder->path);
	release_recorder(recorder, 0);
	copy_path(saved_path, saved_path_length, path);
	return 1;
}
