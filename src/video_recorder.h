// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#ifndef XAVIXEMU_VIDEO_RECORDER_H
#define XAVIXEMU_VIDEO_RECORDER_H

#include <windows.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xavix_video_recorder
{
	void *file;
	void *video_stream;
	void *audio_stream;
	void *imaging_factory;
	uint32_t video_frame;
	uint32_t audio_frame;
	unsigned width;
	unsigned height;
	int avi_initialized;
	int com_uninitialize;
	wchar_t path[MAX_PATH];
} xavix_video_recorder;

void xavix_video_recorder_init(xavix_video_recorder *recorder);
int xavix_video_recorder_active(const xavix_video_recorder *recorder);

int xavix_video_recorder_start(
	xavix_video_recorder *recorder,
	const wchar_t *directory,
	unsigned width,
	unsigned height,
	wchar_t *saved_path,
	size_t saved_path_length,
	wchar_t *error,
	size_t error_length);

int xavix_video_recorder_write_frame(
	xavix_video_recorder *recorder,
	const uint32_t *pixels,
	unsigned pixel_stride,
	const int16_t *interleaved_stereo,
	size_t audio_frames,
	wchar_t *error,
	size_t error_length);

int xavix_video_recorder_stop(
	xavix_video_recorder *recorder,
	wchar_t *saved_path,
	size_t saved_path_length,
	wchar_t *error,
	size_t error_length);

#ifdef __cplusplus
}
#endif

#endif
