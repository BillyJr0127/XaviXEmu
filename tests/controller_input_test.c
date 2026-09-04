// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "controller_input.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

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

static void test_axis_approach(void)
{
	CHECK(xavix_controller_approach_axis(128, 32, 8) == 120);
	CHECK(xavix_controller_approach_axis(40, 32, 8) == 32);
	CHECK(xavix_controller_approach_axis(32, 128, 6) == 38);
	CHECK(xavix_controller_approach_axis(224, 128, 6) == 218);
	CHECK(xavix_controller_approach_axis(125, 128, 6) == 128);
	CHECK(xavix_controller_approach_axis(128, 32, 0) == 128);
}

static void test_racing_axis_curve(void)
{
	CHECK(xavix_controller_curve_racing_axis(0) == 64);
	CHECK(xavix_controller_curve_racing_axis(64) == 112);
	CHECK(xavix_controller_curve_racing_axis(96) == 124);
	CHECK(xavix_controller_curve_racing_axis(128) == 128);
	CHECK(xavix_controller_curve_racing_axis(160) == 132);
	CHECK(xavix_controller_curve_racing_axis(192) == 144);
	CHECK(xavix_controller_curve_racing_axis(255) == 192);
	CHECK(xavix_controller_encode_racing_wheel(64) == 0xbf);
	CHECK(xavix_controller_encode_racing_wheel(128) == 0xff);
	CHECK(xavix_controller_encode_racing_wheel(192) == 0x3f);
}

static void test_digital_racing_axis_pulse(void)
{
	unsigned phase;
	unsigned weak_left = 0;
	unsigned medium_left = 0;

	for (phase = 0; phase < 8; ++phase)
	{
		CHECK(xavix_controller_pulse_digital_axis(128, phase) == 0);
		CHECK(xavix_controller_pulse_digital_axis(105, phase) == 0);
		CHECK(xavix_controller_pulse_digital_axis(151, phase) == 0);
		if (xavix_controller_pulse_digital_axis(96, phase) < 0)
			++weak_left;
		if (xavix_controller_pulse_digital_axis(64, phase) < 0)
			++medium_left;
		CHECK(xavix_controller_pulse_digital_axis(32, phase) == -1);
		CHECK(xavix_controller_pulse_digital_axis(224, phase) == 1);
	}
	CHECK(weak_left == 2);
	CHECK(medium_left == 4);
}

static void test_per_game_binding_profiles(void)
{
	xavix_controller_input input;
	wchar_t directory[MAX_PATH];
	wchar_t ini_path[MAX_PATH];

	memset(&input, 0, sizeof(input));
	CHECK(GetTempPathW(MAX_PATH, directory) > 0);
	CHECK(GetTempFileNameW(directory, L"xci", 0, ini_path) != 0);
	DeleteFileW(ini_path);
	wcsncpy(input.ini_path, ini_path, MAX_PATH - 1);
	input.ini_path[MAX_PATH - 1] = L'\0';
	xavix_controller_input_set_profile(&input, "profile_a");
	xavix_controller_input_set_binding(&input, XAVIX_CONTROLLER_PRIMARY, 7);
	xavix_controller_input_set_profile(&input, "profile_b");
	CHECK(xavix_controller_input_binding(&input,
		XAVIX_CONTROLLER_PRIMARY) != 7);
	xavix_controller_input_set_binding(&input, XAVIX_CONTROLLER_PRIMARY, 11);
	xavix_controller_input_set_profile(&input, "profile_a");
	CHECK(xavix_controller_input_binding(&input,
		XAVIX_CONTROLLER_PRIMARY) == 7);
	xavix_controller_input_set_profile(&input, "profile_b");
	CHECK(xavix_controller_input_binding(&input,
		XAVIX_CONTROLLER_PRIMARY) == 11);
	DeleteFileW(ini_path);
}

int main(void)
{
	test_axis_normalization();
	test_wii_calibration();
	test_relative_stick_position();
	test_axis_approach();
	test_racing_axis_curve();
	test_digital_racing_axis_pulse();
	test_per_game_binding_profiles();
	if (failures)
	{
		printf("controller input: %u failure(s)\n", failures);
		return 1;
	}
	puts("controller input: all tests passed");
	return 0;
}
