// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#ifndef COBJMACROS
#define COBJMACROS
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

#include "controller_input.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum
{
	NINTENDO_VENDOR_ID = 0x057e,
	WIIMOTE_PRODUCT_ID = 0x0306,
	WIIMOTE_TR_PRODUCT_ID = 0x0330
};

static const wchar_t *const ACTION_KEYS[XAVIX_CONTROLLER_ACTION_COUNT] =
{
	L"Primary", L"Secondary", L"Defense", L"Special", L"Confirm",
	L"TwoHand", L"Deflect"
};

static const unsigned DEFAULT_BINDINGS[XAVIX_CONTROLLER_ACTION_COUNT] =
{
	1, 2, 5, 6, 1, 3, 4
};

static int clamp_integer(int value, int minimum, int maximum)
{
	if (value < minimum)
		return minimum;
	if (value > maximum)
		return maximum;
	return value;
}

uint8_t xavix_controller_normalize_axis(uint32_t value, uint32_t minimum,
	uint32_t maximum, int dead_zone_percent)
{
	uint64_t numerator;
	uint32_t normalized;
	int distance;
	int dead_zone;

	if (maximum <= minimum)
		return 0x80;
	if (value < minimum)
		value = minimum;
	if (value > maximum)
		value = maximum;
	numerator = (uint64_t)(value - minimum) * 255U +
		(maximum - minimum) / 2U;
	normalized = (uint32_t)(numerator / (maximum - minimum));
	dead_zone_percent = clamp_integer(dead_zone_percent, 0, 50);
	dead_zone = (255 * dead_zone_percent + 50) / 100;
	distance = (int)normalized - 128;
	if (distance < 0)
		distance = -distance;
	if (distance <= dead_zone)
		return 0x80;
	return (uint8_t)normalized;
}

uint8_t xavix_controller_integrate_axis(uint8_t position, uint8_t axis,
	unsigned maximum_step)
{
	int direction = (int)axis - 128;
	unsigned magnitude;
	unsigned magnitude_squared;
	int step;
	int result;

	if (!direction || !maximum_step)
		return position;
	/* Use mouse-like acceleration: a small tilt advances one unit for precise
	 * menu aiming, while full deflection is fast enough for the two-reflector
	 * convergence classifiers used by Naruto. */
	magnitude = (unsigned)(direction < 0 ? -direction : direction);
	magnitude_squared = magnitude * magnitude;
	step = (int)((magnitude_squared * maximum_step + 16383U) / 16384U);
	if (step < 1)
		step = 1;
	if (direction < 0)
		step = -step;
	result = (int)position + step;
	return (uint8_t)clamp_integer(result, 0, 255);
}

unsigned xavix_controller_ramped_maximum_step(unsigned maximum_step,
	unsigned held_frames)
{
	unsigned ramped;

	if (maximum_step <= 12)
		return maximum_step;
	/* A brief full-stick tap must remain useful for precise cursor movement.
	 * Sustained travel accelerates to the higher speed required by Dragon
	 * Quest's optical slash classifier. */
	ramped = 4 + (held_frames < 10 ? held_frames : 10) * 2;
	return ramped < maximum_step ? ramped : maximum_step;
}

static uint8_t integrate_gamepad_axis(xavix_controller_input *input,
	unsigned index, uint8_t position, uint8_t axis)
{
	int8_t direction;
	unsigned maximum_step;

	if (axis == 0x80)
	{
		input->joystick_axis_direction[index] = 0;
		input->joystick_axis_frames[index] = 0;
		return position;
	}
	direction = axis < 0x80 ? -1 : 1;
	if (input->joystick_axis_direction[index] != direction)
	{
		input->joystick_axis_direction[index] = direction;
		input->joystick_axis_frames[index] = 0;
	}
	else if (input->joystick_axis_frames[index] < 10)
		++input->joystick_axis_frames[index];
	maximum_step = xavix_controller_ramped_maximum_step(
		input->maximum_step, input->joystick_axis_frames[index]);
	return xavix_controller_integrate_axis(position, axis, maximum_step);
}

uint8_t xavix_controller_calibrate_wii_axis(int value, int minimum,
	int maximum, int invert)
{
	int64_t numerator;
	int64_t denominator = (int64_t)maximum - minimum;
	int normalized;

	if (!denominator)
		return 0x80;
	numerator = ((int64_t)value - minimum) * 255;
	if ((numerator < 0) == (denominator < 0))
		numerator += denominator / 2;
	else
		numerator -= denominator / 2;
	normalized = (int)(numerator / denominator);
	normalized = clamp_integer(normalized, 0, 255);
	if (invert)
		normalized = 255 - normalized;
	return (uint8_t)normalized;
}

static void copy_wide_string(wchar_t *destination, size_t capacity,
	const wchar_t *source)
{
	if (!capacity)
		return;
	if (!source)
		source = L"";
	wcsncpy(destination, source, capacity - 1);
	destination[capacity - 1] = L'\0';
}

static void write_integer(const xavix_controller_input *input,
	const wchar_t *section, const wchar_t *key, int value)
{
	wchar_t text[24];
	if (!input || !input->ini_path[0])
		return;
	_snwprintf(text, sizeof(text) / sizeof(text[0]), L"%d", value);
	text[sizeof(text) / sizeof(text[0]) - 1] = L'\0';
	(void)WritePrivateProfileStringW(section, key, text, input->ini_path);
}

static void profile_section(const xavix_controller_input *input,
	wchar_t section[96])
{
	_snwprintf(section, 96, L"Controls.%ls",
		input->profile[0] ? input->profile : L"default");
	section[95] = L'\0';
}

static void load_bindings(xavix_controller_input *input)
{
	wchar_t section[96];
	unsigned action;

	profile_section(input, section);
	for (action = 0; action < XAVIX_CONTROLLER_ACTION_COUNT; ++action)
	{
		int value = GetPrivateProfileIntW(section, ACTION_KEYS[action],
			DEFAULT_BINDINGS[action], input->ini_path);
		input->bindings[action] =
			(unsigned)clamp_integer(value, 0, 32);
	}
}

static void close_wii_remote(xavix_wii_remote_device *remote)
{
	if (!remote)
		return;
	if (remote->read_pending && remote->handle &&
		remote->handle != INVALID_HANDLE_VALUE)
		CancelIo(remote->handle);
	if (remote->handle && remote->handle != INVALID_HANDLE_VALUE)
		CloseHandle(remote->handle);
	if (remote->event)
		CloseHandle(remote->event);
	memset(remote, 0, sizeof(*remote));
	remote->handle = INVALID_HANDLE_VALUE;
}

static int send_wii_report(xavix_wii_remote_device *remote, uint8_t report,
	const uint8_t *payload, size_t payload_size)
{
	uint8_t buffer[64];
	size_t length;
	OVERLAPPED overlapped;
	DWORD transferred = 0;
	HANDLE event;

	if (!remote || !remote->connected || remote->output_length < 2 ||
		remote->output_length > sizeof(buffer) ||
		payload_size + 1 > remote->output_length)
		return 0;
	memset(buffer, 0, remote->output_length);
	buffer[0] = report;
	if (payload_size)
		memcpy(buffer + 1, payload, payload_size);
	length = remote->output_length;
	if (HidD_SetOutputReport(remote->handle, buffer, (ULONG)length))
		return 1;
	/* Some Windows Bluetooth HID stacks reject SetOutputReport but accept the
	 * same report through an overlapped output write. */
	event = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!event)
		return 0;
	memset(&overlapped, 0, sizeof(overlapped));
	overlapped.hEvent = event;
	if (!WriteFile(remote->handle, buffer, (DWORD)length, &transferred,
		&overlapped) && GetLastError() == ERROR_IO_PENDING)
	{
		if (WaitForSingleObject(event, 100) == WAIT_OBJECT_0)
			(void)GetOverlappedResult(remote->handle, &overlapped,
				&transferred, FALSE);
		else
			CancelIoEx(remote->handle, &overlapped);
	}
	CloseHandle(event);
	return transferred == length;
}

static int write_wii_memory(xavix_wii_remote_device *remote,
	uint32_t address, const uint8_t *data, size_t size)
{
	uint8_t payload[21];
	if (!data || !size || size > 16)
		return 0;
	memset(payload, 0, sizeof(payload));
	payload[1] = (uint8_t)(address >> 24);
	payload[2] = (uint8_t)(address >> 16);
	payload[3] = (uint8_t)(address >> 8);
	payload[4] = (uint8_t)address;
	payload[5] = (uint8_t)size;
	memcpy(payload + 6, data, size);
	return send_wii_report(remote, 0x16, payload, 6 + size);
}

static void initialize_wii_ir(xavix_wii_remote_device *remote,
	unsigned index)
{
	static const uint8_t sensitivity_1[9] =
		{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x00, 0xc0 };
	static const uint8_t sensitivity_2[2] = { 0x40, 0x00 };
	const uint8_t enable = 0x04;
	const uint8_t camera_enable = 0x08;
	const uint8_t extended_mode = 0x03;
	uint8_t led = (uint8_t)(index ? 0x20 : 0x10);
	uint8_t reporting[2] = { 0x00, 0x33 };

	(void)send_wii_report(remote, 0x11, &led, 1);
	(void)send_wii_report(remote, 0x13, &enable, 1);
	(void)send_wii_report(remote, 0x1a, &enable, 1);
	Sleep(10);
	(void)write_wii_memory(remote, UINT32_C(0x04b00030),
		&camera_enable, 1);
	(void)write_wii_memory(remote, UINT32_C(0x04b00000),
		sensitivity_1, sizeof(sensitivity_1));
	(void)write_wii_memory(remote, UINT32_C(0x04b0001a),
		sensitivity_2, sizeof(sensitivity_2));
	(void)write_wii_memory(remote, UINT32_C(0x04b00033),
		&extended_mode, 1);
	(void)send_wii_report(remote, 0x12, reporting, sizeof(reporting));
}

static int is_wii_product(USHORT product)
{
	return product == WIIMOTE_PRODUCT_ID || product == WIIMOTE_TR_PRODUCT_ID;
}

static void rescan_wii_remotes(xavix_controller_input *input)
{
	GUID hid_guid;
	HDEVINFO devices;
	SP_DEVICE_INTERFACE_DATA interface_data;
	DWORD device_index;
	unsigned found = 0;

	close_wii_remote(&input->wii[0]);
	close_wii_remote(&input->wii[1]);
	HidD_GetHidGuid(&hid_guid);
	devices = SetupDiGetClassDevsW(&hid_guid, NULL, NULL,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (devices == INVALID_HANDLE_VALUE)
		return;
	memset(&interface_data, 0, sizeof(interface_data));
	interface_data.cbSize = sizeof(interface_data);
	for (device_index = 0; found < 2 &&
		SetupDiEnumDeviceInterfaces(devices, NULL, &hid_guid, device_index,
			&interface_data); ++device_index)
	{
		PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail;
		DWORD required = 0;
		HANDLE handle;
		HIDD_ATTRIBUTES attributes;
		PHIDP_PREPARSED_DATA preparsed = NULL;
		HIDP_CAPS caps;
		xavix_wii_remote_device *remote;

		(void)SetupDiGetDeviceInterfaceDetailW(devices, &interface_data,
			NULL, 0, &required, NULL);
		if (!required)
			continue;
		detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(required);
		if (!detail)
			break;
		detail->cbSize = sizeof(*detail);
		if (!SetupDiGetDeviceInterfaceDetailW(devices, &interface_data,
			detail, required, NULL, NULL))
		{
			free(detail);
			continue;
		}
		handle = CreateFileW(detail->DevicePath,
			GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
		free(detail);
		if (handle == INVALID_HANDLE_VALUE)
			continue;
		memset(&attributes, 0, sizeof(attributes));
		attributes.Size = sizeof(attributes);
		if (!HidD_GetAttributes(handle, &attributes) ||
			attributes.VendorID != NINTENDO_VENDOR_ID ||
			!is_wii_product(attributes.ProductID) ||
			!HidD_GetPreparsedData(handle, &preparsed) ||
			HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS ||
			caps.InputReportByteLength > 64 || caps.OutputReportByteLength > 64)
		{
			if (preparsed)
				HidD_FreePreparsedData(preparsed);
			CloseHandle(handle);
			continue;
		}
		HidD_FreePreparsedData(preparsed);
		remote = &input->wii[found];
		memset(remote, 0, sizeof(*remote));
		remote->handle = handle;
		remote->event = CreateEventW(NULL, TRUE, FALSE, NULL);
		if (!remote->event)
		{
			close_wii_remote(remote);
			continue;
		}
		remote->overlapped.hEvent = remote->event;
		remote->input_length = caps.InputReportByteLength;
		remote->output_length = caps.OutputReportByteLength;
		remote->connected = 1;
		(void)HidD_SetNumInputBuffers(handle, 32);
		initialize_wii_ir(remote, found);
		++found;
	}
	SetupDiDestroyDeviceInfoList(devices);
}

static void rescan_joystick(xavix_controller_input *input)
{
	UINT count = joyGetNumDevs();
	UINT id;

	input->joystick_connected = 0;
	for (id = 0; id < count; ++id)
	{
		JOYINFOEX state;
		memset(&state, 0, sizeof(state));
		state.dwSize = sizeof(state);
		state.dwFlags = JOY_RETURNALL;
		if (joyGetPosEx(id, &state) != JOYERR_NOERROR ||
			joyGetDevCapsW(id, &input->joystick_caps,
				sizeof(input->joystick_caps)) != JOYERR_NOERROR)
			continue;
		input->joystick_id = id;
		input->joystick_connected = 1;
		input->joystick_buttons = state.dwButtons;
		return;
	}
}

void xavix_controller_input_rescan(xavix_controller_input *input)
{
	if (!input)
		return;
	rescan_joystick(input);
	rescan_wii_remotes(input);
}

static void parse_wii_report(xavix_wii_remote_device *remote,
	const uint8_t *report, unsigned length)
{
	unsigned offset;
	unsigned point;
	int total_x = 0;
	int total_y = 0;
	int total_size = 0;
	int visible = 0;

	if (!remote || !report || length < 3)
		return;
	remote->buttons = (uint16_t)(((uint16_t)report[1] << 8) | report[2]);
	if (report[0] != 0x33 || length < 18)
		return;
	offset = 6;
	for (point = 0; point < 4; ++point)
	{
		const uint8_t *data = report + offset + point * 3;
		int x = data[0] | ((data[2] & 0x30) << 4);
		int y = data[1] | ((data[2] & 0xc0) << 2);
		if (x == 1023 && y == 1023)
			continue;
		total_x += x;
		total_y += y;
		total_size += data[2] & 0x0f;
		++visible;
	}
	remote->point_visible = visible != 0;
	if (visible)
	{
		remote->raw_x = total_x / visible;
		remote->raw_y = total_y / visible;
		remote->raw_size = total_size / visible;
	}
}

static void poll_wii_remote(xavix_wii_remote_device *remote)
{
	unsigned iterations;

	if (!remote || !remote->connected)
		return;
	for (iterations = 0; iterations < 8; ++iterations)
	{
		DWORD transferred = 0;
		BOOL result;
		if (remote->read_pending)
		{
			result = GetOverlappedResult(remote->handle, &remote->overlapped,
				&transferred, FALSE);
			if (!result)
			{
				if (GetLastError() == ERROR_IO_INCOMPLETE)
					return;
				close_wii_remote(remote);
				return;
			}
			remote->read_pending = 0;
			ResetEvent(remote->event);
			parse_wii_report(remote, remote->input_report, transferred);
			continue;
		}
		memset(remote->input_report, 0, sizeof(remote->input_report));
		result = ReadFile(remote->handle, remote->input_report,
			remote->input_length, &transferred, &remote->overlapped);
		if (result)
		{
			parse_wii_report(remote, remote->input_report, transferred);
			continue;
		}
		if (GetLastError() == ERROR_IO_PENDING)
		{
			remote->read_pending = 1;
			return;
		}
		close_wii_remote(remote);
		return;
	}
}

static uint32_t wii_button_mask(const xavix_wii_remote_device *remote,
	unsigned index)
{
	static const uint16_t source_masks[11] =
	{
		/* Keep A/B/1/2 aligned with conventional gamepad buttons 1..4 so
		 * the default per-game bindings are useful on either input source. */
		0x0800, 0x0400, 0x0200, 0x0100, 0x1000, 0x0010,
		0x0080, 0x0008, 0x0004, 0x0001, 0x0002
	};
	uint32_t result = 0;
	unsigned button;
	unsigned shift = index ? 16 : 0;

	if (!remote || !remote->connected)
		return 0;
	for (button = 0; button < 11; ++button)
		if (remote->buttons & source_masks[button])
			result |= UINT32_C(1) << (shift + button);
	return result;
}

static uint32_t actions_for_buttons(const xavix_controller_input *input,
	uint32_t buttons)
{
	uint32_t actions = 0;
	unsigned action;
	for (action = 0; action < XAVIX_CONTROLLER_ACTION_COUNT; ++action)
	{
		unsigned button = input->bindings[action];
		if (button && button <= 32 &&
			(buttons & (UINT32_C(1) << (button - 1))))
			actions |= UINT32_C(1) << action;
	}
	return actions;
}

static int update_gamepad(xavix_controller_input *input,
	xavix_controller_reading *reading, uint32_t *buttons)
{
	JOYINFOEX state;
	const JOYCAPSW *caps = &input->joystick_caps;
	uint32_t right_x;
	uint32_t right_y;
	uint32_t right_min_x;
	uint32_t right_max_x;
	uint32_t right_min_y;
	uint32_t right_max_y;

	if (!input->joystick_connected)
		return 0;
	memset(&state, 0, sizeof(state));
	state.dwSize = sizeof(state);
	state.dwFlags = JOY_RETURNALL;
	if (joyGetPosEx(input->joystick_id, &state) != JOYERR_NOERROR)
	{
		input->joystick_connected = 0;
		return 0;
	}
	input->joystick_buttons = state.dwButtons;
	input->joystick_x[0] = integrate_gamepad_axis(input, 0,
		input->joystick_x[0], xavix_controller_normalize_axis(state.dwXpos,
		caps->wXmin, caps->wXmax, input->dead_zone_percent));
	input->joystick_y[0] = integrate_gamepad_axis(input, 1,
		input->joystick_y[0], xavix_controller_normalize_axis(state.dwYpos,
		caps->wYmin, caps->wYmax, input->dead_zone_percent));
	reading->reflector[0].x = input->joystick_x[0];
	reading->reflector[0].y = input->joystick_y[0];
	reading->reflector[0].area = 0x28;
	reading->reflector[0].visible = 1;
	if (caps->wCaps & JOYCAPS_HASZ)
	{
		right_x = state.dwZpos;
		right_min_x = caps->wZmin;
		right_max_x = caps->wZmax;
	}
	else
	{
		right_x = state.dwUpos;
		right_min_x = caps->wUmin;
		right_max_x = caps->wUmax;
	}
	if (caps->wCaps & JOYCAPS_HASR)
	{
		right_y = state.dwRpos;
		right_min_y = caps->wRmin;
		right_max_y = caps->wRmax;
	}
	else
	{
		right_y = state.dwVpos;
		right_min_y = caps->wVmin;
		right_max_y = caps->wVmax;
	}
	input->joystick_x[1] = integrate_gamepad_axis(input, 2,
		input->joystick_x[1], xavix_controller_normalize_axis(right_x,
		right_min_x, right_max_x, input->dead_zone_percent));
	input->joystick_y[1] = integrate_gamepad_axis(input, 3,
		input->joystick_y[1], xavix_controller_normalize_axis(right_y,
		right_min_y, right_max_y, input->dead_zone_percent));
	reading->reflector[1].x = input->joystick_x[1];
	reading->reflector[1].y = input->joystick_y[1];
	reading->reflector[1].area = 0x28;
	reading->reflector[1].visible = 1;
	*buttons = state.dwButtons;
	return 1;
}

static int update_wii(xavix_controller_input *input,
	xavix_controller_reading *reading, uint32_t *buttons)
{
	unsigned index;
	int connected = 0;

	*buttons = 0;
	for (index = 0; index < 2; ++index)
	{
		xavix_wii_remote_device *remote = &input->wii[index];
		poll_wii_remote(remote);
		if (!remote->connected)
			continue;
		connected = 1;
		*buttons |= wii_button_mask(remote, index);
		if (!remote->point_visible)
			continue;
		reading->reflector[index].x = xavix_controller_calibrate_wii_axis(
			remote->raw_x, input->wii_min_x[index],
			input->wii_max_x[index], 0);
		reading->reflector[index].y = xavix_controller_calibrate_wii_axis(
			remote->raw_y, input->wii_min_y[index],
			input->wii_max_y[index], 0);
		reading->reflector[index].area = 0x28;
		reading->reflector[index].visible = 1;
	}
	return connected;
}

void xavix_controller_input_init(xavix_controller_input *input,
	const wchar_t *ini_path)
{
	wchar_t source[24];
	unsigned index;

	if (!input)
		return;
	memset(input, 0, sizeof(*input));
	input->joystick_x[0] = 0x80;
	input->joystick_y[0] = 0x80;
	input->joystick_x[1] = 0x80;
	input->joystick_y[1] = 0x80;
	input->maximum_step = 12;
	input->wii[0].handle = INVALID_HANDLE_VALUE;
	input->wii[1].handle = INVALID_HANDLE_VALUE;
	copy_wide_string(input->ini_path,
		sizeof(input->ini_path) / sizeof(input->ini_path[0]), ini_path);
	copy_wide_string(input->profile,
		sizeof(input->profile) / sizeof(input->profile[0]), L"default");
	/* Preserve the existing mouse controls until the user explicitly chooses
	 * a controller.  Auto is useful after setup but must not let an idle HID
	 * joystick unexpectedly take ownership on first launch. */
	GetPrivateProfileStringW(L"Controller", L"Source", L"mouse", source,
		sizeof(source) / sizeof(source[0]), input->ini_path);
	if (_wcsicmp(source, L"mouse") == 0)
		input->preferred_source = XAVIX_CONTROLLER_SOURCE_MOUSE;
	else if (_wcsicmp(source, L"gamepad") == 0)
		input->preferred_source = XAVIX_CONTROLLER_SOURCE_GAMEPAD;
	else if (_wcsicmp(source, L"wii") == 0)
		input->preferred_source = XAVIX_CONTROLLER_SOURCE_WII_REMOTE;
	else
		input->preferred_source = XAVIX_CONTROLLER_SOURCE_AUTO;
	input->single_reflector = clamp_integer(GetPrivateProfileIntW(
		L"Controller", L"SingleReflector", 0, input->ini_path), 0, 1);
	input->dead_zone_percent = clamp_integer(GetPrivateProfileIntW(
		L"Controller", L"DeadZone", 12, input->ini_path), 0, 50);
	for (index = 0; index < 2; ++index)
	{
		wchar_t key[32];
		_snwprintf(key, 32, L"Wii%uLeftX", index + 1);
		input->wii_min_x[index] = GetPrivateProfileIntW(L"Controller", key,
			1023, input->ini_path);
		_snwprintf(key, 32, L"Wii%uRightX", index + 1);
		input->wii_max_x[index] = GetPrivateProfileIntW(L"Controller", key,
			0, input->ini_path);
		_snwprintf(key, 32, L"Wii%uTopY", index + 1);
		input->wii_min_y[index] = GetPrivateProfileIntW(L"Controller", key,
			0, input->ini_path);
		_snwprintf(key, 32, L"Wii%uBottomY", index + 1);
		input->wii_max_y[index] = GetPrivateProfileIntW(L"Controller", key,
			767, input->ini_path);
	}
	load_bindings(input);
	xavix_controller_input_rescan(input);
}

void xavix_controller_input_shutdown(xavix_controller_input *input)
{
	if (!input)
		return;
	close_wii_remote(&input->wii[0]);
	close_wii_remote(&input->wii[1]);
	input->joystick_connected = 0;
}

void xavix_controller_input_set_profile(xavix_controller_input *input,
	const char *short_name)
{
	if (!input)
		return;
	if (!short_name || !MultiByteToWideChar(CP_UTF8, 0, short_name, -1,
		input->profile, sizeof(input->profile) / sizeof(input->profile[0])))
		copy_wide_string(input->profile,
			sizeof(input->profile) / sizeof(input->profile[0]), L"default");
	load_bindings(input);
}

void xavix_controller_input_set_maximum_step(xavix_controller_input *input,
	unsigned maximum_step)
{
	if (!input)
		return;
	input->maximum_step = maximum_step ? maximum_step : 12;
	memset(input->joystick_axis_direction, 0,
		sizeof(input->joystick_axis_direction));
	memset(input->joystick_axis_frames, 0,
		sizeof(input->joystick_axis_frames));
}

void xavix_controller_input_update(xavix_controller_input *input,
	xavix_controller_reading *reading)
{
	uint32_t buttons = 0;
	uint32_t actions;
	int connected = 0;

	if (!reading)
		return;
	memset(reading, 0, sizeof(*reading));
	reading->active_source = XAVIX_CONTROLLER_SOURCE_MOUSE;
	if (!input || input->preferred_source == XAVIX_CONTROLLER_SOURCE_MOUSE)
		return;
	if (input->preferred_source == XAVIX_CONTROLLER_SOURCE_WII_REMOTE ||
		input->preferred_source == XAVIX_CONTROLLER_SOURCE_AUTO)
	{
		connected = update_wii(input, reading, &buttons);
		if (connected)
			reading->active_source = XAVIX_CONTROLLER_SOURCE_WII_REMOTE;
	}
	if (!connected && (input->preferred_source ==
		XAVIX_CONTROLLER_SOURCE_GAMEPAD || input->preferred_source ==
		XAVIX_CONTROLLER_SOURCE_AUTO))
	{
		connected = update_gamepad(input, reading, &buttons);
		if (connected)
			reading->active_source = XAVIX_CONTROLLER_SOURCE_GAMEPAD;
	}
	reading->connected = connected;
	actions = connected ? actions_for_buttons(input, buttons) : 0;
	reading->actions = actions;
	reading->pressed = actions & ~input->previous_actions;
	input->previous_actions = actions;
}

enum xavix_controller_source xavix_controller_input_source(
	const xavix_controller_input *input)
{
	return input ? input->preferred_source : XAVIX_CONTROLLER_SOURCE_AUTO;
}

void xavix_controller_input_set_source(xavix_controller_input *input,
	enum xavix_controller_source source)
{
	static const wchar_t *const names[] =
		{ L"auto", L"mouse", L"gamepad", L"wii" };
	if (!input || source < XAVIX_CONTROLLER_SOURCE_AUTO ||
		source > XAVIX_CONTROLLER_SOURCE_WII_REMOTE)
		return;
	input->preferred_source = source;
	(void)WritePrivateProfileStringW(L"Controller", L"Source", names[source],
		input->ini_path);
}

int xavix_controller_input_single_reflector(
	const xavix_controller_input *input)
{
	return input ? input->single_reflector : 0;
}

void xavix_controller_input_set_single_reflector(xavix_controller_input *input,
	int reflector)
{
	if (!input)
		return;
	input->single_reflector = reflector ? 1 : 0;
	write_integer(input, L"Controller", L"SingleReflector",
		input->single_reflector);
}

int xavix_controller_input_dead_zone(const xavix_controller_input *input)
{
	return input ? input->dead_zone_percent : 12;
}

void xavix_controller_input_set_dead_zone(xavix_controller_input *input,
	int percent)
{
	if (!input)
		return;
	input->dead_zone_percent = clamp_integer(percent, 0, 50);
	write_integer(input, L"Controller", L"DeadZone",
		input->dead_zone_percent);
}

unsigned xavix_controller_input_binding(const xavix_controller_input *input,
	enum xavix_controller_action action)
{
	return input && action >= 0 && action < XAVIX_CONTROLLER_ACTION_COUNT ?
		input->bindings[action] : 0;
}

void xavix_controller_input_set_binding(xavix_controller_input *input,
	enum xavix_controller_action action, unsigned button)
{
	wchar_t section[96];
	if (!input || action < 0 || action >= XAVIX_CONTROLLER_ACTION_COUNT)
		return;
	input->bindings[action] = button <= 32 ? button : 0;
	profile_section(input, section);
	write_integer(input, section, ACTION_KEYS[action],
		(int)input->bindings[action]);
}

unsigned xavix_controller_input_first_pressed_button(
	xavix_controller_input *input)
{
	JOYINFOEX state;
	uint32_t buttons = 0;
	unsigned bit;
	unsigned index;

	if (!input)
		return 0;
	if (input->joystick_connected)
	{
		memset(&state, 0, sizeof(state));
		state.dwSize = sizeof(state);
		state.dwFlags = JOY_RETURNBUTTONS;
		if (joyGetPosEx(input->joystick_id, &state) == JOYERR_NOERROR)
			buttons = state.dwButtons;
	}
	for (index = 0; index < 2; ++index)
	{
		poll_wii_remote(&input->wii[index]);
		buttons |= wii_button_mask(&input->wii[index], index);
	}
	for (bit = 0; bit < 32; ++bit)
		if (buttons & (UINT32_C(1) << bit))
			return bit + 1;
	return 0;
}

int xavix_controller_input_wii_connected(const xavix_controller_input *input,
	unsigned index)
{
	return input && index < 2 && input->wii[index].connected;
}

int xavix_controller_input_gamepad_connected(
	const xavix_controller_input *input)
{
	return input && input->joystick_connected;
}

int xavix_controller_input_capture_wii_calibration(
	xavix_controller_input *input, unsigned index, int upper_left)
{
	wchar_t key[32];
	xavix_wii_remote_device *remote;
	if (!input || index >= 2)
		return 0;
	remote = &input->wii[index];
	poll_wii_remote(remote);
	if (!remote->connected || !remote->point_visible)
		return 0;
	if (upper_left)
	{
		input->wii_min_x[index] = remote->raw_x;
		input->wii_min_y[index] = remote->raw_y;
		_snwprintf(key, 32, L"Wii%uLeftX", index + 1);
		write_integer(input, L"Controller", key, remote->raw_x);
		_snwprintf(key, 32, L"Wii%uTopY", index + 1);
		write_integer(input, L"Controller", key, remote->raw_y);
	}
	else
	{
		input->wii_max_x[index] = remote->raw_x;
		input->wii_max_y[index] = remote->raw_y;
		_snwprintf(key, 32, L"Wii%uRightX", index + 1);
		write_integer(input, L"Controller", key, remote->raw_x);
		_snwprintf(key, 32, L"Wii%uBottomY", index + 1);
		write_integer(input, L"Controller", key, remote->raw_y);
	}
	return 1;
}
