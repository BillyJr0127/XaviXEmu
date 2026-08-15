// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vfw.h>

#include "video_recorder.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "check failed at line %d: %s\n", \
			__LINE__, #condition); \
		return 1; \
	} \
} while (0)

enum
{
	TEST_WIDTH = 320,
	TEST_HEIGHT = 240,
	TEST_FRAMES = 4,
	AUDIO_FRAMES_PER_VIDEO_FRAME = 800
};

static int verify_stream_lengths(const wchar_t *path)
{
	PAVIFILE file = NULL;
	PAVISTREAM video = NULL;
	PAVISTREAM audio = NULL;
	HRESULT result;
	int success = 0;

	AVIFileInit();
	result = AVIFileOpenW(&file, path, OF_READ, NULL);
	if (SUCCEEDED(result))
		result = AVIFileGetStream(file, &video, streamtypeVIDEO, 0);
	if (SUCCEEDED(result))
		result = AVIFileGetStream(file, &audio, streamtypeAUDIO, 0);
	if (SUCCEEDED(result) && AVIStreamLength(video) == TEST_FRAMES &&
		AVIStreamLength(audio) ==
		TEST_FRAMES * AUDIO_FRAMES_PER_VIDEO_FRAME)
		success = 1;
	if (audio) AVIStreamRelease(audio);
	if (video) AVIStreamRelease(video);
	if (file) AVIFileRelease(file);
	AVIFileExit();
	return success;
}

int main(void)
{
	xavix_video_recorder recorder;
	uint32_t pixels[TEST_WIDTH * TEST_HEIGHT];
	int16_t audio[AUDIO_FRAMES_PER_VIDEO_FRAME * 2];
	wchar_t directory[MAX_PATH];
	wchar_t path[MAX_PATH];
	wchar_t stopped_path[MAX_PATH];
	wchar_t error[384];
	HANDLE file;
	char header[12];
	DWORD read = 0;
	unsigned frame;
	unsigned i;

	CHECK(GetTempPathW(MAX_PATH, directory) > 0);
	xavix_video_recorder_init(&recorder);
	CHECK(xavix_video_recorder_start(&recorder, directory,
		TEST_WIDTH, TEST_HEIGHT, path,
		sizeof(path) / sizeof(path[0]), error,
		sizeof(error) / sizeof(error[0])));
	CHECK(xavix_video_recorder_active(&recorder));
	for (frame = 0; frame < TEST_FRAMES; ++frame)
	{
		for (i = 0; i < TEST_WIDTH * TEST_HEIGHT; ++i)
		{
			unsigned x = i % TEST_WIDTH;
			unsigned y = i / TEST_WIDTH;
			pixels[i] = UINT32_C(0xff000000) |
				((uint32_t)((x * 17 + frame * 23) & 0xff) << 16) |
				((uint32_t)((y * 21 + frame * 11) & 0xff) << 8) |
				(uint32_t)((x + y + frame * 31) & 0xff);
		}
		for (i = 0; i < AUDIO_FRAMES_PER_VIDEO_FRAME * 2; ++i)
			audio[i] = (int16_t)((int)(i * 13 + frame * 101) - 12000);
		CHECK(xavix_video_recorder_write_frame(&recorder, pixels,
			TEST_WIDTH, audio, AUDIO_FRAMES_PER_VIDEO_FRAME,
			error, sizeof(error) / sizeof(error[0])));
	}
	CHECK(xavix_video_recorder_stop(&recorder, stopped_path,
		sizeof(stopped_path) / sizeof(stopped_path[0]), error,
		sizeof(error) / sizeof(error[0])));
	CHECK(!xavix_video_recorder_active(&recorder));
	CHECK(wcscmp(path, stopped_path) == 0);

	file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	CHECK(file != INVALID_HANDLE_VALUE);
	CHECK(ReadFile(file, header, sizeof(header), &read, NULL));
	CloseHandle(file);
	CHECK(read == sizeof(header));
	CHECK(memcmp(header, "RIFF", 4) == 0);
	CHECK(memcmp(header + 8, "AVI ", 4) == 0);
	CHECK(verify_stream_lengths(path));
	CHECK(DeleteFileW(path));
	return 0;
}
