// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "cursor_presentation.h"

#include <stdio.h>

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
			return 1; \
		} \
	} while (0)

int main(void)
{
	drgqst_cursor_presentation cursor;
	int x;
	int y;
	unsigned step;

	drgqst_cursor_presentation_reset(&cursor);
	drgqst_cursor_presentation_update(&cursor, 128, 112);
	drgqst_cursor_presentation_get_native(&cursor, &x, &y);
	CHECK(x == 128 && y == 112);

	/* A normal 32-pixel sensor step is display-smoothed by half. */
	drgqst_cursor_presentation_update(&cursor, 160, 80);
	drgqst_cursor_presentation_get_native(&cursor, &x, &y);
	CHECK(x == 144 && y == 96);

	/* Large cuts remain immediate so presentation does not add input lag. */
	drgqst_cursor_presentation_update(&cursor, 208, 160);
	drgqst_cursor_presentation_get_native(&cursor, &x, &y);
	CHECK(x == 208 && y == 160);

	/* Small changes settle exactly rather than leaving a fractional tail. */
	drgqst_cursor_presentation_update(&cursor, 224, 144);
	for (step = 0; step < 8; ++step)
		drgqst_cursor_presentation_update(&cursor, 224, 144);
	drgqst_cursor_presentation_get_native(&cursor, &x, &y);
	CHECK(x == 224 && y == 144);

	/* Reset/postload invalidation snaps to the next restored coordinate. */
	drgqst_cursor_presentation_reset(&cursor);
	drgqst_cursor_presentation_update(&cursor, 64, 48);
	drgqst_cursor_presentation_get_native(&cursor, &x, &y);
	CHECK(x == 64 && y == 48);

	/* Both native and stretched 4:3 viewports preserve the logical hotspot. */
	drgqst_cursor_presentation_reset(&cursor);
	drgqst_cursor_presentation_update(&cursor, 128, 112);
	drgqst_cursor_presentation_map_viewport(&cursor,
		0, 0, 768, 672, &x, &y);
	CHECK(x == 384 && y == 336);
	drgqst_cursor_presentation_reset(&cursor);
	drgqst_cursor_presentation_update(&cursor, 128, 112);
	drgqst_cursor_presentation_map_viewport(&cursor,
		240, 0, 1440, 1080, &x, &y);
	CHECK(x == 960 && y == 540);
	drgqst_cursor_presentation_reset(&cursor);
	drgqst_cursor_presentation_update(&cursor, 64, 48);
	drgqst_cursor_presentation_map_viewport(&cursor,
		240, 0, 1440, 1080, &x, &y);
	CHECK(x == 600 && y == 231);

	printf("cursor_presentation_test: all tests passed\n");
	return 0;
}
