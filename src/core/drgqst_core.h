/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 Billy Jr. and contributors */
#ifndef DRGQST_PLAYER_CORE_H
#define DRGQST_PLAYER_CORE_H

#include "xavix_cpu.h"
#include "xavix_audio.h"
#include "xavix_machine.h"
#include "xavix_video.h"

#include <stddef.h>
#include <stdint.h>

enum
{
	DRGQST_AUDIO_RATE = 48000,
	DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME = DRGQST_AUDIO_RATE / XAVIX_FRAME_RATE
};

enum drgqst_core_profile
{
	DRGQST_CORE_DRAGON_QUEST = 0,
	DRGQST_CORE_BAN_ONEP = 1,
	DRGQST_CORE_BAN_OMT = 2,
	DRGQST_CORE_TTV_CU5501_24C02 = 3,
	DRGQST_CORE_TTV_CU5501A_24C02 = 4,
	DRGQST_CORE_XAVIX_BASE = 5,
	DRGQST_CORE_XAVIX_I2C_24C16 = 6,
	DRGQST_CORE_XAVIX2000_I2C_24C04 = 7
};

typedef struct drgqst_core
{
	xavix_machine machine;
	xavix_cpu_t cpu;
	xavix_audio audio;
	xavix_video video;
	int16_t frame_audio[DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME * 2];
	uint32_t frame_fraction;
	uint32_t audio_frame_cycles;
	uint32_t audio_frame_position;
	uint8_t audio_frame_active;
	uint8_t game_profile;
	uint8_t ban_onep_sync_divider;
	uint8_t ban_onep_sync_period;
	uint8_t ban_onep_sync_phase;
	uint8_t ban_onep_buttons;
	uint8_t ban_onep_drag_active;
	uint8_t ban_onep_drag_origin_x;
	uint8_t ban_onep_left_punch;
	uint8_t ban_onep_right_punch;
	uint8_t ban_onep_aim_x;
	uint8_t ban_onep_aim_y;
	uint8_t ban_onep_bazooka_phase;
	uint8_t ttv_exposure_pending;
	uint8_t epo_hamd_packet;
	uint8_t epo_hamd_packet_mask;
	uint8_t epo_hamd_packet_queue[4];
	uint8_t epo_hamd_packet_queue_head;
	uint8_t epo_hamd_packet_queue_count;
	uint8_t tvpc_keyboard_rows[8];
} drgqst_core;

int drgqst_core_init(drgqst_core *core, const uint8_t *rom, size_t rom_size);
int drgqst_core_init_profile(drgqst_core *core, const uint8_t *rom,
	size_t rom_size, enum drgqst_core_profile profile);
void drgqst_core_reset(drgqst_core *core);

/* Execute whole instructions.  Intended for oracle tests and diagnostics. */
int drgqst_core_step(drgqst_core *core);
uint64_t drgqst_core_run_instructions(drgqst_core *core, uint32_t instructions);

/* Execute at least the requested number of master-clock cycles. */
uint64_t drgqst_core_run_cycles(drgqst_core *core, uint32_t cycles);

/* Run one 60 Hz host frame, assert XaviX vblank and render 256x224 pixels. */
const uint32_t *drgqst_core_run_frame(drgqst_core *core);
const uint32_t *drgqst_core_framebuffer(const drgqst_core *core);

/* Generate interleaved signed 16-bit stereo at 48 kHz. */
size_t drgqst_core_generate_audio(drgqst_core *core,
	int16_t *interleaved_stereo, size_t frames);
const int16_t *drgqst_core_frame_audio(const drgqst_core *core);

int drgqst_core_feather_visible(const drgqst_core *core);
int drgqst_core_internal_cursor_visible(const drgqst_core *core);

/* Firmware-calibrated logical sword hotspot in the native 256x224 picture. */
int drgqst_core_sword_cursor_position(const drgqst_core *core, int *x, int *y);

void drgqst_core_set_mouse(drgqst_core *core, uint8_t x, uint8_t y,
	int broadside, int step_forward);

/* Emit one two-reflector forward-thrust gesture for the One Piece game. */
void drgqst_core_trigger_bazooka(drgqst_core *core);

/* Queue one eight-bit wireless reflector packet for Ham-chans. */
void drgqst_core_trigger_hamd_packet(drgqst_core *core, uint8_t packet);

/* Set one raw active-high row returned by the TV-PC keyboard controller. */
void drgqst_core_set_tvpc_keyboard_row(drgqst_core *core, unsigned row,
	uint8_t keys);

#endif
