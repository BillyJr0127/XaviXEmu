// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "controller_input.h"

#include <stdio.h>

static unsigned failures;

#define CHECK(condition) do { if (!(condition)) { \
	printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; \
} } while (0)

static void test_axis_normalization(void)
{
	CHECK(xavix_controller_normalize_axis(0, 0, 65535, 0) == 0);
	CHECK(xavix_controller_normalize_axis(65535, 0, 65535, 0) == 255);
	CHECK(xavix_controller_normalize_axis(32768, 0, 65535, 12) == 128);
	CHECK(xavix_controller_normalize_axis(30000, 0, 65535, 12) == 128);
	CHECK(xavix_controller_normalize_axis(0, 1, 1, 0) == 128);
}

static void test_wii_calibration(void)
{
	CHECK(xavix_controller_calibrate_wii_axis(1023, 1023, 0, 0) == 0);
	CHECK(xavix_controller_calibrate_wii_axis(0, 1023, 0, 0) == 255);
	CHECK(xavix_controller_calibrate_wii_axis(512, 1023, 0, 0) >= 127);
	CHECK(xavix_controller_calibrate_wii_axis(512, 1023, 0, 0) <= 128);
	CHECK(xavix_controller_calibrate_wii_axis(-100, 0, 767, 0) == 0);
	CHECK(xavix_controller_calibrate_wii_axis(900, 0, 767, 0) == 255);
	CHECK(xavix_controller_calibrate_wii_axis(0, 0, 767, 1) == 255);
}

static void test_relative_stick_position(void)
{
	uint8_t position = 128;

	position = xavix_controller_integrate_axis(position, 255, 12);
	CHECK(position == 140);
	position = xavix_controller_integrate_axis(position, 128, 12);
	CHECK(position == 140);
	position = xavix_controller_integrate_axis(position, 129, 12);
	CHECK(position == 141);
	position = xavix_controller_integrate_axis(position, 64, 12);
	CHECK(position == 138);
	position = xavix_controller_integrate_axis(position, 0, 12);
	CHECK(position == 126);
	CHECK(xavix_controller_integrate_axis(253, 255, 12) == 255);
	CHECK(xavix_controller_integrate_axis(2, 0, 12) == 0);
	/* Kenshin Dragon Quest needs a faster full-deflection stroke to cross the
	 * guest firmware's LARGE-motion threshold.  Fine tilts remain precise. */
	CHECK(xavix_controller_integrate_axis(0x20, 255, 24) == 0x38);
	CHECK(xavix_controller_integrate_axis(0x80, 160, 24) == 0x82);
	CHECK(xavix_controller_ramped_maximum_step(12, 0) == 12);
	CHECK(xavix_controller_ramped_maximum_step(24, 0) == 4);
	CHECK(xavix_controller_ramped_maximum_step(24, 3) == 10);
	CHECK(xavix_controller_ramped_maximum_step(24, 10) == 24);
	CHECK(xavix_controller_ramped_maximum_step(24, 99) == 24);
}

int main(void)
{
	test_axis_normalization();
	test_wii_calibration();
	test_relative_stick_position();
	if (failures)
	{
		printf("controller input: %u failure(s)\n", failures);
		return 1;
	}
	puts("controller input: all tests passed");
	return 0;
}
