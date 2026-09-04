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
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

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
	xavix_video_format format, wchar_t output[MAX_PATH], wchar_t *error,
	size_t error_length)
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
				L"XaviXEmu-%04u%02u%02u-%02u%02u%02u-%03u.%ls",
				time.wYear, time.wMonth, time.wDay, time.wHour,
				time.wMinute, time.wSecond, time.wMilliseconds,
				format == XAVIX_VIDEO_FORMAT_MP4 ? L"mp4" : L"avi");
		else
			result = _snwprintf(filename,
				sizeof(filename) / sizeof(filename[0]),
				L"XaviXEmu-%04u%02u%02u-%02u%02u%02u-%03u-%03u.%ls",
				time.wYear, time.wMonth, time.wDay, time.wHour,
				time.wMinute, time.wSecond, time.wMilliseconds, attempt,
				format == XAVIX_VIDEO_FORMAT_MP4 ? L"mp4" : L"avi");
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
		L"A unique recording filename could not be created.");
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
	if (recorder->sink_writer)
		IMFSinkWriter_Release((IMFSinkWriter *)recorder->sink_writer);
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
	if (recorder->mf_initialized)
		MFShutdown();
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
	return recorder && ((recorder->format == XAVIX_VIDEO_FORMAT_MP4 &&
		recorder->sink_writer) ||
		(recorder->format == XAVIX_VIDEO_FORMAT_AVI && recorder->file &&
			recorder->video_stream && recorder->audio_stream));
}

static int start_avi(xavix_video_recorder *recorder,
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
	if (!choose_output_path(directory, XAVIX_VIDEO_FORMAT_AVI,
		recorder->path, error, error_length))
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
	recorder->format = XAVIX_VIDEO_FORMAT_AVI;
	copy_path(saved_path, saved_path_length, recorder->path);
	return 1;
}

static int start_mp4(xavix_video_recorder *recorder,
	const wchar_t *directory, unsigned width, unsigned height,
	wchar_t *saved_path, size_t saved_path_length,
	wchar_t *error, size_t error_length)
{
	IMFSinkWriter *writer = NULL;
	IMFMediaType *output_video = NULL;
	IMFMediaType *input_video = NULL;
	IMFMediaType *output_audio = NULL;
	IMFMediaType *input_audio = NULL;
	DWORD video_stream = 0;
	DWORD audio_stream = 0;
	HRESULT result;
	HRESULT initialize_result;
	uint64_t bitrate;
	int success = 0;

	clear_error(error, error_length);
	copy_path(saved_path, saved_path_length, L"");
	if (!recorder || xavix_video_recorder_active(recorder))
	{
		set_error(error, error_length, L"A recording is already active.");
		return 0;
	}
	if (!width || !height || (width & 1) || (height & 1) ||
		width > INT_MAX || height > INT_MAX ||
		(uint64_t)width * height * 4 > UINT_MAX)
	{
		set_error(error, error_length,
			L"MP4 recording dimensions must be positive and even.");
		return 0;
	}
	memset(recorder, 0, sizeof(*recorder));
	recorder->format = XAVIX_VIDEO_FORMAT_MP4;
	if (!choose_output_path(directory, XAVIX_VIDEO_FORMAT_MP4,
		recorder->path, error, error_length))
		return 0;
	initialize_result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	if (SUCCEEDED(initialize_result))
		recorder->com_uninitialize = 1;
	else if (initialize_result != RPC_E_CHANGED_MODE)
	{
		set_error(error, error_length,
			L"Windows media initialization failed (0x%08lx).",
			(unsigned long)initialize_result);
		recorder->path[0] = L'\0';
		return 0;
	}
	result = MFStartup(MF_VERSION, MFSTARTUP_FULL);
	if (FAILED(result))
	{
		set_error(error, error_length,
			L"Windows Media Foundation could not start (0x%08lx).",
			(unsigned long)result);
		release_recorder(recorder, 1);
		return 0;
	}
	recorder->mf_initialized = 1;
	result = MFCreateSinkWriterFromURL(recorder->path, NULL, NULL, &writer);
	if (SUCCEEDED(result))
		recorder->sink_writer = writer;
	if (FAILED(result))
		goto failed;

	result = MFCreateMediaType(&output_video);
	if (SUCCEEDED(result)) result = IMFMediaType_SetGUID(output_video,
		&MF_MT_MAJOR_TYPE, &MFMediaType_Video);
	if (SUCCEEDED(result)) result = IMFMediaType_SetGUID(output_video,
		&MF_MT_SUBTYPE, &MFVideoFormat_H264);
	bitrate = (uint64_t)width * height * 12;
	if (bitrate < UINT32_C(2000000)) bitrate = UINT32_C(2000000);
	if (bitrate > UINT32_C(20000000)) bitrate = UINT32_C(20000000);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(output_video,
		&MF_MT_AVG_BITRATE, (UINT32)bitrate);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(output_video,
		&MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT64(output_video, &MF_MT_FRAME_SIZE,
		((uint64_t)width << 32) | height);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT64(output_video, &MF_MT_FRAME_RATE,
		((uint64_t)VIDEO_FRAME_RATE << 32) | 1);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT64(output_video, &MF_MT_PIXEL_ASPECT_RATIO,
		(UINT64_C(1) << 32) | 1);
	if (SUCCEEDED(result)) result = IMFSinkWriter_AddStream(writer,
		output_video, &video_stream);
	if (FAILED(result)) goto failed;

	result = MFCreateMediaType(&input_video);
	if (SUCCEEDED(result)) result = IMFMediaType_SetGUID(input_video,
		&MF_MT_MAJOR_TYPE, &MFMediaType_Video);
	if (SUCCEEDED(result)) result = IMFMediaType_SetGUID(input_video,
		&MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(input_video,
		&MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT64(input_video, &MF_MT_FRAME_SIZE,
		((uint64_t)width << 32) | height);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT64(input_video, &MF_MT_FRAME_RATE,
		((uint64_t)VIDEO_FRAME_RATE << 32) | 1);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT64(input_video, &MF_MT_PIXEL_ASPECT_RATIO,
		(UINT64_C(1) << 32) | 1);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(input_video,
		&MF_MT_DEFAULT_STRIDE, width * 4);
	if (SUCCEEDED(result)) result = IMFSinkWriter_SetInputMediaType(writer,
		video_stream, input_video, NULL);
	if (FAILED(result)) goto failed;

	result = MFCreateMediaType(&output_audio);
	if (SUCCEEDED(result)) result = IMFMediaType_SetGUID(output_audio,
		&MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
	if (SUCCEEDED(result)) result = IMFMediaType_SetGUID(output_audio,
		&MF_MT_SUBTYPE, &MFAudioFormat_AAC);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(output_audio,
		&MF_MT_AUDIO_NUM_CHANNELS, AUDIO_CHANNELS);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(output_audio,
		&MF_MT_AUDIO_SAMPLES_PER_SECOND, AUDIO_SAMPLE_RATE);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(output_audio,
		&MF_MT_AUDIO_BITS_PER_SAMPLE, AUDIO_BITS);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(output_audio,
		&MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(output_audio,
		&MF_MT_AUDIO_BLOCK_ALIGNMENT, 1);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(output_audio,
		&MF_MT_AAC_PAYLOAD_TYPE, 0);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(output_audio,
		&MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
	if (SUCCEEDED(result)) result = IMFSinkWriter_AddStream(writer,
		output_audio, &audio_stream);
	if (FAILED(result)) goto failed;

	result = MFCreateMediaType(&input_audio);
	if (SUCCEEDED(result)) result = IMFMediaType_SetGUID(input_audio,
		&MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
	if (SUCCEEDED(result)) result = IMFMediaType_SetGUID(input_audio,
		&MF_MT_SUBTYPE, &MFAudioFormat_PCM);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(input_audio,
		&MF_MT_AUDIO_NUM_CHANNELS, AUDIO_CHANNELS);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(input_audio,
		&MF_MT_AUDIO_SAMPLES_PER_SECOND, AUDIO_SAMPLE_RATE);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(input_audio,
		&MF_MT_AUDIO_BITS_PER_SAMPLE, AUDIO_BITS);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(input_audio,
		&MF_MT_AUDIO_BLOCK_ALIGNMENT,
		(AUDIO_CHANNELS * AUDIO_BITS) / 8);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(input_audio,
		&MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
		AUDIO_SAMPLE_RATE * AUDIO_CHANNELS * AUDIO_BITS / 8);
	if (SUCCEEDED(result)) result = IMFMediaType_SetUINT32(input_audio,
		&MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	if (SUCCEEDED(result)) result = IMFSinkWriter_SetInputMediaType(writer,
		audio_stream, input_audio, NULL);
	if (FAILED(result)) goto failed;

	result = IMFSinkWriter_BeginWriting(writer);
	if (FAILED(result)) goto failed;
	recorder->width = width;
	recorder->height = height;
	recorder->mf_video_stream = video_stream;
	recorder->mf_audio_stream = audio_stream;
	copy_path(saved_path, saved_path_length, recorder->path);
	success = 1;

failed:
	if (input_audio) IMFMediaType_Release(input_audio);
	if (output_audio) IMFMediaType_Release(output_audio);
	if (input_video) IMFMediaType_Release(input_video);
	if (output_video) IMFMediaType_Release(output_video);
	if (!success)
	{
		set_error(error, error_length,
			L"The MP4 H.264/AAC encoder could not be initialized (0x%08lx).",
			(unsigned long)result);
		release_recorder(recorder, 1);
	}
	return success;
}

int xavix_video_recorder_start_format(xavix_video_recorder *recorder,
	const wchar_t *directory, unsigned width, unsigned height,
	xavix_video_format format, wchar_t *saved_path, size_t saved_path_length,
	wchar_t *error, size_t error_length)
{
	if (format == XAVIX_VIDEO_FORMAT_MP4)
		return start_mp4(recorder, directory, width, height, saved_path,
			saved_path_length, error, error_length);
	if (format != XAVIX_VIDEO_FORMAT_AVI)
	{
		set_error(error, error_length, L"The recording format is invalid.");
		return 0;
	}
	return start_avi(recorder, directory, width, height, saved_path,
		saved_path_length, error, error_length);
}

int xavix_video_recorder_start(xavix_video_recorder *recorder,
	const wchar_t *directory, unsigned width, unsigned height,
	wchar_t *saved_path, size_t saved_path_length,
	wchar_t *error, size_t error_length)
{
	return xavix_video_recorder_start_format(recorder, directory, width,
		height, XAVIX_VIDEO_FORMAT_AVI, saved_path, saved_path_length,
		error, error_length);
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

static int write_mp4_frame(xavix_video_recorder *recorder,
	const uint32_t *pixels, unsigned pixel_stride,
	const int16_t *interleaved_stereo, size_t audio_frames,
	wchar_t *error, size_t error_length)
{
	IMFSinkWriter *writer = (IMFSinkWriter *)recorder->sink_writer;
	IMFMediaBuffer *video_buffer = NULL;
	IMFSample *video_sample = NULL;
	IMFMediaBuffer *audio_buffer = NULL;
	IMFSample *audio_sample = NULL;
	BYTE *destination = NULL;
	DWORD maximum_length = 0;
	DWORD current_length = 0;
	DWORD video_bytes = recorder->width * recorder->height * 4;
	DWORD audio_bytes = (DWORD)(audio_frames * AUDIO_CHANNELS * sizeof(int16_t));
	LONGLONG video_time = (LONGLONG)recorder->video_frame * 10000000 /
		VIDEO_FRAME_RATE;
	LONGLONG video_end = (LONGLONG)(recorder->video_frame + 1) * 10000000 /
		VIDEO_FRAME_RATE;
	LONGLONG audio_time = (LONGLONG)recorder->audio_frame * 10000000 /
		AUDIO_SAMPLE_RATE;
	LONGLONG audio_end = (LONGLONG)(recorder->audio_frame + audio_frames) *
		10000000 / AUDIO_SAMPLE_RATE;
	HRESULT result;
	uint32_t y;
	int success = 0;

	result = MFCreateMemoryBuffer(video_bytes, &video_buffer);
	if (SUCCEEDED(result)) result = IMFMediaBuffer_Lock(video_buffer,
		&destination, &maximum_length, &current_length);
	if (SUCCEEDED(result))
	{
		/* The sink writer consumes RGB32 sample buffers from their first row.
		 * The recording DIB is already top-down, so preserve its row order.
		 * Reversing it here made every encoded MP4 vertically inverted. */
		for (y = 0; y < recorder->height; ++y)
			memcpy(destination + y * recorder->width * 4,
				pixels + y * pixel_stride, recorder->width * 4);
		IMFMediaBuffer_Unlock(video_buffer);
		destination = NULL;
		result = IMFMediaBuffer_SetCurrentLength(video_buffer, video_bytes);
	}
	if (SUCCEEDED(result)) result = MFCreateSample(&video_sample);
	if (SUCCEEDED(result)) result = IMFSample_AddBuffer(video_sample,
		video_buffer);
	if (SUCCEEDED(result)) result = IMFSample_SetSampleTime(video_sample,
		video_time);
	if (SUCCEEDED(result)) result = IMFSample_SetSampleDuration(video_sample,
		video_end - video_time);
	if (SUCCEEDED(result)) result = IMFSinkWriter_WriteSample(writer,
		recorder->mf_video_stream, video_sample);
	if (FAILED(result))
	{
		set_error(error, error_length,
			L"The MP4 video frame could not be encoded (0x%08lx).",
			(unsigned long)result);
		goto done;
	}

	result = MFCreateMemoryBuffer(audio_bytes, &audio_buffer);
	if (SUCCEEDED(result)) result = IMFMediaBuffer_Lock(audio_buffer,
		&destination, &maximum_length, &current_length);
	if (SUCCEEDED(result))
	{
		memcpy(destination, interleaved_stereo, audio_bytes);
		IMFMediaBuffer_Unlock(audio_buffer);
		destination = NULL;
		result = IMFMediaBuffer_SetCurrentLength(audio_buffer, audio_bytes);
	}
	if (SUCCEEDED(result)) result = MFCreateSample(&audio_sample);
	if (SUCCEEDED(result)) result = IMFSample_AddBuffer(audio_sample,
		audio_buffer);
	if (SUCCEEDED(result)) result = IMFSample_SetSampleTime(audio_sample,
		audio_time);
	if (SUCCEEDED(result)) result = IMFSample_SetSampleDuration(audio_sample,
		audio_end - audio_time);
	if (SUCCEEDED(result)) result = IMFSinkWriter_WriteSample(writer,
		recorder->mf_audio_stream, audio_sample);
	if (FAILED(result))
	{
		set_error(error, error_length,
			L"The MP4 audio frame could not be encoded (0x%08lx).",
			(unsigned long)result);
		goto done;
	}
	recorder->video_frame++;
	recorder->audio_frame += (uint32_t)audio_frames;
	success = 1;

done:
	if (destination)
	{
		if (audio_buffer) IMFMediaBuffer_Unlock(audio_buffer);
		else if (video_buffer) IMFMediaBuffer_Unlock(video_buffer);
	}
	if (audio_sample) IMFSample_Release(audio_sample);
	if (audio_buffer) IMFMediaBuffer_Release(audio_buffer);
	if (video_sample) IMFSample_Release(video_sample);
	if (video_buffer) IMFMediaBuffer_Release(video_buffer);
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
	if (recorder->format == XAVIX_VIDEO_FORMAT_MP4)
		return write_mp4_frame(recorder, pixels, pixel_stride,
			interleaved_stereo, audio_frames, error, error_length);
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
	if (recorder->format == XAVIX_VIDEO_FORMAT_MP4)
	{
		HRESULT result = IMFSinkWriter_Finalize(
			(IMFSinkWriter *)recorder->sink_writer);
		if (FAILED(result))
		{
			set_error(error, error_length,
				L"The MP4 file could not be finalized (0x%08lx).",
				(unsigned long)result);
			release_recorder(recorder, 1);
			return 0;
		}
	}
	release_recorder(recorder, 0);
	copy_path(saved_path, saved_path_length, path);
	return 1;
}
