// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "cursor_presentation.h"

#include <stddef.h>

enum
{
	CURSOR_FRACTION = 256,
	CURSOR_SNAP_DISTANCE = 48,
	CURSOR_SETTLE_DISTANCE = 1,
	CURSOR_NATIVE_WIDTH = 256,
	CURSOR_NATIVE_HEIGHT = 224
};

static int32_t smooth_coordinate(int target, int32_t display, uint8_t *valid)
{
	const int32_t target_256 = (int32_t)target * CURSOR_FRACTION;
	const int32_t delta = target_256 - display;

	if (!*valid || delta <= -CURSOR_SNAP_DISTANCE * CURSOR_FRACTION ||
		delta >= CURSOR_SNAP_DISTANCE * CURSOR_FRACTION)
	{
		*valid = 1;
		return target_256;
	}
	if (delta >= -CURSOR_SETTLE_DISTANCE * CURSOR_FRACTION &&
		delta <= CURSOR_SETTLE_DISTANCE * CURSOR_FRACTION)
		return target_256;
	return display + delta / 2;
}

static int divide_round_nearest(int64_t numerator, int64_t denominator)
{
	if (numerator < 0)
		return (int)((numerator - denominator / 2) / denominator);
	return (int)((numerator + denominator / 2) / denominator);
}

void drgqst_cursor_presentation_reset(drgqst_cursor_presentation *cursor)
{
	if (!cursor)
		return;
	cursor->display_x_256 = 0;
	cursor->display_y_256 = 0;
	cursor->x_valid = 0;
	cursor->y_valid = 0;
}

void drgqst_cursor_presentation_update(drgqst_cursor_presentation *cursor,
	int target_x, int target_y)
{
	if (!cursor)
		return;
	cursor->display_x_256 = smooth_coordinate(target_x,
		cursor->display_x_256, &cursor->x_valid);
	cursor->display_y_256 = smooth_coordinate(target_y,
		cursor->display_y_256, &cursor->y_valid);
}

void drgqst_cursor_presentation_get_native(
	const drgqst_cursor_presentation *cursor, int *x, int *y)
{
	if (x)
		*x = cursor && cursor->x_valid ?
			divide_round_nearest(cursor->display_x_256, CURSOR_FRACTION) : 0;
	if (y)
		*y = cursor && cursor->y_valid ?
			divide_round_nearest(cursor->display_y_256, CURSOR_FRACTION) : 0;
}

void drgqst_cursor_presentation_map_viewport(
	const drgqst_cursor_presentation *cursor,
	int viewport_x, int viewport_y, int viewport_width, int viewport_height,
	int *x, int *y)
{
	if (x)
		*x = viewport_x;
	if (y)
		*y = viewport_y;
	if (!cursor || !cursor->x_valid || !cursor->y_valid ||
		viewport_width <= 0 || viewport_height <= 0)
		return;
	if (x)
		*x = viewport_x + divide_round_nearest(
			(int64_t)cursor->display_x_256 * viewport_width,
			CURSOR_NATIVE_WIDTH * CURSOR_FRACTION);
	if (y)
		*y = viewport_y + divide_round_nearest(
			(int64_t)cursor->display_y_256 * viewport_height,
			CURSOR_NATIVE_HEIGHT * CURSOR_FRACTION);
}
