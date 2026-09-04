// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#ifndef XAVIXEMU_CONTROLLER_INPUT_H
#define XAVIXEMU_CONTROLLER_INPUT_H

#include <windows.h>
#include <mmsystem.h>

#include <stdint.h>
#include <stddef.h>

enum xavix_controller_source
{
	XAVIX_CONTROLLER_SOURCE_AUTO,
	XAVIX_CONTROLLER_SOURCE_MOUSE,
	XAVIX_CONTROLLER_SOURCE_GAMEPAD,
	XAVIX_CONTROLLER_SOURCE_WII_REMOTE
};

enum xavix_controller_action
{
	XAVIX_CONTROLLER_PRIMARY,
	XAVIX_CONTROLLER_SECONDARY,
	XAVIX_CONTROLLER_DEFENSE,
	XAVIX_CONTROLLER_SPECIAL,
	XAVIX_CONTROLLER_CONFIRM,
	XAVIX_CONTROLLER_TWO_HAND,
	XAVIX_CONTROLLER_DEFLECT,
	XAVIX_CONTROLLER_ACTION_COUNT
};

typedef struct xavix_virtual_reflector
{
	uint8_t x;
	uint8_t y;
	uint8_t area;
	int visible;
} xavix_virtual_reflector;

typedef struct xavix_controller_reading
{
	xavix_virtual_reflector reflector[2];
	uint8_t gamepad_axis_x[2];
	uint8_t gamepad_axis_y[2];
	int gamepad_axes_valid;
	uint32_t actions;
	uint32_t pressed;
	enum xavix_controller_source active_source;
	int connected;
} xavix_controller_reading;

typedef struct xavix_wii_remote_device
{
	HANDLE handle;
	HANDLE event;
	OVERLAPPED overlapped;
	unsigned input_length;
	unsigned output_length;
	uint8_t input_report[64];
	int read_pending;
	int connected;
	int raw_x;
	int raw_y;
	int raw_size;
	int point_visible;
	uint16_t buttons;
	uint16_t previous_buttons;
} xavix_wii_remote_device;

typedef struct xavix_controller_input
{
	wchar_t ini_path[MAX_PATH];
	wchar_t profile[64];
	enum xavix_controller_source preferred_source;
	int single_reflector;
	int dead_zone_percent;
	unsigned maximum_step;
	UINT joystick_id;
	JOYCAPSW joystick_caps;
	int joystick_connected;
	uint32_t joystick_buttons;
	uint8_t joystick_x[2];
	uint8_t joystick_y[2];
	int8_t joystick_axis_direction[4];
	uint8_t joystick_axis_frames[4];
	uint32_t previous_actions;
	unsigned bindings[XAVIX_CONTROLLER_ACTION_COUNT];
	xavix_wii_remote_device wii[2];
	int wii_min_x[2];
	int wii_max_x[2];
	int wii_min_y[2];
	int wii_max_y[2];
} xavix_controller_input;

void xavix_controller_input_init(xavix_controller_input *input,
	const wchar_t *ini_path);
void xavix_controller_input_shutdown(xavix_controller_input *input);
void xavix_controller_input_rescan(xavix_controller_input *input);
void xavix_controller_input_set_profile(xavix_controller_input *input,
	const char *short_name);
void xavix_controller_input_set_maximum_step(xavix_controller_input *input,
	unsigned maximum_step);
void xavix_controller_input_update(xavix_controller_input *input,
	xavix_controller_reading *reading);

enum xavix_controller_source xavix_controller_input_source(
	const xavix_controller_input *input);
void xavix_controller_input_set_source(xavix_controller_input *input,
	enum xavix_controller_source source);
int xavix_controller_input_single_reflector(
	const xavix_controller_input *input);
void xavix_controller_input_set_single_reflector(xavix_controller_input *input,
	int reflector);
int xavix_controller_input_dead_zone(const xavix_controller_input *input);
void xavix_controller_input_set_dead_zone(xavix_controller_input *input,
	int percent);
unsigned xavix_controller_input_binding(const xavix_controller_input *input,
	enum xavix_controller_action action);
void xavix_controller_input_set_binding(xavix_controller_input *input,
	enum xavix_controller_action action, unsigned button);
unsigned xavix_controller_input_first_pressed_button(
	xavix_controller_input *input);
int xavix_controller_input_wii_connected(const xavix_controller_input *input,
	unsigned index);
int xavix_controller_input_gamepad_connected(
	const xavix_controller_input *input);
int xavix_controller_input_capture_wii_calibration(
	xavix_controller_input *input, unsigned index, int upper_left);

/* Pure helpers exposed for deterministic unit tests. */
uint8_t xavix_controller_normalize_axis(uint32_t value, uint32_t minimum,
	uint32_t maximum, int dead_zone_percent);
uint8_t xavix_controller_integrate_axis(uint8_t position, uint8_t axis,
	unsigned maximum_step);
uint8_t xavix_controller_approach_axis(uint8_t position, uint8_t target,
	unsigned maximum_step);
uint8_t xavix_controller_curve_racing_axis(uint8_t axis);
uint8_t xavix_controller_encode_racing_wheel(uint8_t position);
int xavix_controller_pulse_digital_axis(uint8_t axis, unsigned phase);
unsigned xavix_controller_ramped_maximum_step(unsigned maximum_step,
	unsigned held_frames);
uint8_t xavix_controller_calibrate_wii_axis(int value, int minimum,
	int maximum, int invert);

#endif
