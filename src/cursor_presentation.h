// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#ifndef DRGQST_PLAYER_CURSOR_PRESENTATION_H
#define DRGQST_PLAYER_CURSOR_PRESENTATION_H

#include <stdint.h>

typedef struct drgqst_cursor_presentation
{
	int32_t display_x_256;
	int32_t display_y_256;
	uint8_t x_valid;
	uint8_t y_valid;
} drgqst_cursor_presentation;

void drgqst_cursor_presentation_reset(drgqst_cursor_presentation *cursor);
void drgqst_cursor_presentation_update(drgqst_cursor_presentation *cursor,
	int target_x, int target_y);
void drgqst_cursor_presentation_get_native(
	const drgqst_cursor_presentation *cursor, int *x, int *y);
void drgqst_cursor_presentation_map_viewport(
	const drgqst_cursor_presentation *cursor,
	int viewport_x, int viewport_y, int viewport_width, int viewport_height,
	int *x, int *y);

#endif
