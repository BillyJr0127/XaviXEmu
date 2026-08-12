/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 Billy Jr. and contributors */
#include "../src/core/drgqst_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ROM_SIZE UINT32_C(0x800000)
#define TEST_OMT_ROM_SIZE UINT32_C(0x400000)
#define TEST_BOWL_ROM_SIZE UINT32_C(0x200000)

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
			return 0; \
		} \
	} while (0)

#define RUN_TEST(test) \
	do { \
		if (!(test)) { \
			fprintf(stderr, "%s:%d: test failed: %s\n", __FILE__, __LINE__, #test); \
			return EXIT_FAILURE; \
		} \
	} while (0)

static void make_test_rom_sized(uint8_t *rom, size_t rom_size)
{
	static const uint8_t program[] = {
		0xa2, 0x00,       /* ldx #$00 */
		0xa9, 0x01,       /* lda #$01 */
		0x85, 0x10,       /* sta $10 */
		0xe6, 0x10,       /* loop: inc $10 */
		0xa5, 0x10,       /* lda $10 */
		0x69, 0x03,       /* adc #$03 */
		0x85, 0x11,       /* sta $11 */
		0xe8,             /* inx */
		0x86, 0x12,       /* stx $12 */
		0x4c, 0x06, 0x80  /* jmp loop */
	};
	uint32_t index;
	for (index = 0; index < rom_size; ++index)
		rom[index] = (uint8_t)((index * 37U) ^ (index >> 9) ^ (index >> 17));
	memcpy(rom + 0x8000, program, sizeof(program));
	rom[0xfffa] = 0x00;
	rom[0xfffb] = 0x80;
	rom[0xfffc] = 0x00;
	rom[0xfffd] = 0x80;
	rom[0xfffe] = 0x00;
	rom[0xffff] = 0x80;
}

static void make_test_rom(uint8_t *rom)
{
	make_test_rom_sized(rom, TEST_ROM_SIZE);
}

static void configure_checkpoint(drgqst_core *core)
{
	xavix_audio_voice *const voice = &core->audio.voice[0];

	drgqst_core_run_instructions(core, 73);
	drgqst_core_set_mouse(core, 0x35, 0xa4, 1, 0);
	core->machine.state.palette_sh[7] = 0x62;
	core->machine.state.palette_l[7] = 0xb1;
	core->machine.state.fragment_ram[19] = 0x87;
	core->machine.state.peripherals.eeprom.data[42] = 0x5a;
	core->machine.state.peripherals.eeprom.dirty = 1;
	core->machine.state.peripherals.eeprom.write_generation = 17;
	core->cpu.j = 0x11;
	core->cpu.k = 0x22;
	core->cpu.l = 0x33;
	core->cpu.m = 0x44;
	core->cpu.pa = UINT32_C(0x123456);
	core->cpu.pb = UINT32_C(0x654321);
	core->frame_fraction = 37;

	voice->enabled = 1;
	voice->type = 1;
	voice->volume = 15;
	voice->rate = 0x1234;
	voice->noise_state = 0xace1;
	voice->env_mode = 0;
	voice->env_volume_left = 0xe0;
	voice->env_volume_right = 0xc0;
	voice->env_active_left = 1;
	voice->env_active_right = 1;
	core->audio.voice_start[0] |= 1;
	core->audio.master_volume = 0xd0;
	core->audio.mixer_monaural = 0;
	core->audio.mixer_capacity = 2;
	core->audio.mixer_amp = 2;
	core->audio.mixer_gain = 2;
	drgqst_core_generate_audio(core, NULL, 31);
}

static int save_exact(const drgqst_core *core, uint8_t *buffer, size_t size)
{
	size_t written = 0;
	return drgqst_state_save(core, buffer, size, &written) && written == size;
}

static int test_firmware_cursor_position(void)
{
	drgqst_core *core = (drgqst_core *)calloc(1, sizeof(*core));
	int x = 0;
	int y = 0;

	CHECK(core != NULL);
	CHECK(!drgqst_core_sword_cursor_position(NULL, &x, &y));
	core->machine.state.main_ram[0x026c] = 0xc0;
	core->machine.state.main_ram[0x026d] = 0xff;
	core->machine.state.main_ram[0x0270] = 0x40;
	core->machine.state.main_ram[0x0271] = 0x00;
	CHECK(drgqst_core_sword_cursor_position(core, &x, &y));
	CHECK(x == 64 && y == 48);

	core->machine.state.main_ram[0x026c] = 0x30;
	core->machine.state.main_ram[0x026d] = 0x00;
	core->machine.state.main_ram[0x0270] = 0xe0;
	core->machine.state.main_ram[0x0271] = 0xff;
	CHECK(drgqst_core_sword_cursor_position(core, &x, &y));
	CHECK(x == 176 && y == 144);
	CHECK(drgqst_core_sword_cursor_position(core, NULL, NULL));
	free(core);
	return 1;
}

static int test_internal_cursor_watch_profiles(void)
{
	uint8_t *rom = (uint8_t *)malloc(TEST_ROM_SIZE);
	drgqst_core *core = (drgqst_core *)malloc(sizeof(*core));
	const xavix_video_sprite_watch *watch;
	int ok = 0;

	if (!rom || !core)
		goto done;
	make_test_rom(rom);
	if (!drgqst_core_init_profile(core, rom, TEST_ROM_SIZE,
		DRGQST_CORE_TTV_CU5501_24C02))
		goto done;
	watch = &core->video.sprite_watch;
	if (!watch->enabled || watch->first_address != UINT32_C(0xa14660) ||
		watch->last_address != UINT32_C(0xa14780) ||
		watch->address_stride != 0x60)
		goto done;
	drgqst_core_set_mouse(core, 0x80, 0x80, 0, 1);
	if (core->machine.state.peripherals.sensor.host_mode !=
		XAVIX_SENSOR_VERTICAL)
		goto done;

	if (!drgqst_core_init_profile(core, rom, TEST_ROM_SIZE,
		DRGQST_CORE_TTV_CU5501A_24C02))
		goto done;
	watch = &core->video.sprite_watch;
	if (!watch->enabled || watch->first_address != UINT32_C(0xf6eee0) ||
		watch->last_address != UINT32_C(0xf6f060) ||
		watch->address_stride != 0x80 ||
		watch->second_first_address != UINT32_C(0xf01fe0) ||
		watch->second_last_address != UINT32_C(0xf02160) ||
		watch->second_address_stride != 0x80)
		goto done;
	drgqst_core_set_mouse(core, 0x80, 0x80, 1, 0);
	if (core->machine.state.peripherals.sensor.host_mode !=
		XAVIX_SENSOR_BROADSIDE)
		goto done;
	drgqst_core_set_mouse(core, 0x80, 0x80, 0, 0);
	if (core->machine.state.peripherals.sensor.host_mode !=
		XAVIX_SENSOR_NARROW)
		goto done;
	drgqst_core_set_mouse(core, 0x80, 0x80, 0, 1);
	if (core->machine.state.peripherals.sensor.host_mode !=
		XAVIX_SENSOR_STEP_FORWARD)
		goto done;
	drgqst_core_set_mouse(core, 0x80, 0x80, 0, 0);
	if (core->machine.state.peripherals.sensor.host_mode !=
		XAVIX_SENSOR_NARROW)
		goto done;

	if (!drgqst_core_init_profile(core, rom, TEST_ROM_SIZE,
		DRGQST_CORE_BAN_ONEP) || core->video.sprite_watch.enabled)
		goto done;
	ok = 1;
done:
	free(core);
	free(rom);
	return ok;
}

static int test_round_trip_and_continuation(void)
{
	uint8_t *rom = NULL;
	drgqst_core *original = NULL;
	drgqst_core *restored = NULL;
	uint8_t *guarded = NULL;
	uint8_t *saved = NULL;
	uint8_t *round_trip = NULL;
	uint8_t *before_bad_load = NULL;
	uint8_t *after_bad_load = NULL;
	uint8_t *damaged = NULL;
	int16_t original_audio[256 * 2];
	int16_t restored_audio[256 * 2];
	xavix_video_sprite_watch custom_watch;
	xavix_cpu_read8_fn old_read;
	xavix_cpu_write8_fn old_write;
	void *old_cpu_opaque;
	xavix_machine_hooks old_hooks;
	const uint8_t *old_rom;
	size_t old_rom_size;
	const size_t size = drgqst_state_serialized_size();
	size_t written = 0;
	int ok = 0;

	CHECK(size > 24 && size < 65536);
	rom = (uint8_t *)malloc(TEST_ROM_SIZE);
	original = (drgqst_core *)malloc(sizeof(*original));
	restored = (drgqst_core *)malloc(sizeof(*restored));
	guarded = (uint8_t *)malloc(size + 2);
	round_trip = (uint8_t *)malloc(size);
	before_bad_load = (uint8_t *)malloc(size);
	after_bad_load = (uint8_t *)malloc(size);
	damaged = (uint8_t *)malloc(size);
	if (!rom || !original || !restored || !guarded || !round_trip ||
		!before_bad_load || !after_bad_load || !damaged)
		goto done;
	saved = guarded + 1;
	make_test_rom(rom);
	if (!drgqst_core_init(original, rom, TEST_ROM_SIZE) ||
		!drgqst_core_init(restored, rom, TEST_ROM_SIZE))
		goto done;
	configure_checkpoint(original);
	original->video.framebuffer[0] = UINT32_C(0xff123456);
	original->video.zbuffer[0] = 0x7f;
	original->frame_audio[0] = 1234;

	memset(guarded, 0xa5, size + 2);
	if (drgqst_state_save(original, NULL, size, &written) || written != size ||
		drgqst_state_save(original, saved, size - 1, &written) || written != size ||
		guarded[0] != 0xa5 || guarded[size + 1] != 0xa5 ||
		!drgqst_state_save(original, saved, size, &written) || written != size ||
		guarded[0] != 0xa5 || guarded[size + 1] != 0xa5)
		goto done;
	if (memcmp(saved, "DRGQSTS1", 8) || saved[8] != DRGQST_STATE_FORMAT_VERSION ||
		saved[9] != 0 || saved[10] != 24 || saved[11] != 0)
		goto done;

	custom_watch = restored->video.sprite_watch;
	custom_watch.first_address ^= UINT32_C(0x100);
	custom_watch.last_address ^= UINT32_C(0x200);
	xavix_video_set_sprite_watch(&restored->video, &custom_watch);
	restored->video.framebuffer[0] = UINT32_C(0xffabcdef);
	restored->frame_audio[0] = -2222;
	restored->audio_frame_cycles = 1234;
	restored->audio_frame_position = 567;
	restored->audio_frame_active = 1;
	old_read = restored->cpu.read8;
	old_write = restored->cpu.write8;
	old_cpu_opaque = restored->cpu.opaque;
	old_hooks = restored->machine.hooks;
	old_rom = restored->machine.rom;
	old_rom_size = restored->machine.rom_size;
	if (!drgqst_state_load(restored, saved, size) ||
		restored->cpu.read8 != old_read || restored->cpu.write8 != old_write ||
		restored->cpu.opaque != old_cpu_opaque || restored->machine.rom != old_rom ||
		restored->machine.rom_size != old_rom_size ||
		restored->machine.hooks.context != old_hooks.context ||
		restored->machine.hooks.read_sound != old_hooks.read_sound ||
		restored->machine.hooks.write_sound != old_hooks.write_sound ||
		memcmp(&restored->video.sprite_watch, &custom_watch, sizeof(custom_watch)) ||
		restored->video.framebuffer[0] != 0 || restored->video.zbuffer[0] != 0 ||
		restored->frame_audio[0] != 0 || restored->audio_frame_cycles != 0 ||
		restored->audio_frame_position != 0 || restored->audio_frame_active != 0 ||
		!save_exact(restored, round_trip, size) ||
		memcmp(saved, round_trip, size))
		goto done;

	/* Rejected input must not partially alter the destination. */
	if (!save_exact(restored, before_bad_load, size))
		goto done;
	memcpy(damaged, saved, size);
	damaged[size - 1] ^= 0x80;
	if (drgqst_state_load(restored, damaged, size) ||
		drgqst_state_load(restored, saved, size - 1) ||
		drgqst_state_load(restored, saved, size + 1) ||
		!save_exact(restored, after_bad_load, size) ||
		memcmp(before_bad_load, after_bad_load, size))
		goto done;
	memcpy(damaged, saved, size);
	damaged[0] ^= 1;
	if (drgqst_state_load(restored, damaged, size))
		goto done;
	memcpy(damaged, saved, size);
	damaged[8] = (uint8_t)(DRGQST_STATE_FORMAT_VERSION + 1);
	if (drgqst_state_load(restored, damaged, size))
		goto done;
	memcpy(damaged, saved, size);
	damaged[20] = 1;
	if (drgqst_state_load(restored, damaged, size))
		goto done;

	/* Both instances now start from the same checkpoint. */
	if (drgqst_core_run_instructions(original, 250) !=
		drgqst_core_run_instructions(restored, 250) ||
		drgqst_core_generate_audio(original, original_audio, 256) != 256 ||
		drgqst_core_generate_audio(restored, restored_audio, 256) != 256 ||
		memcmp(original_audio, restored_audio, sizeof(original_audio)))
		goto done;
	if (!drgqst_core_run_frame(original) || !drgqst_core_run_frame(restored) ||
		memcmp(drgqst_core_framebuffer(original), drgqst_core_framebuffer(restored),
			XAVIX_VIDEO_PIXELS * sizeof(uint32_t)) ||
		memcmp(drgqst_core_frame_audio(original), drgqst_core_frame_audio(restored),
			DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME * 2 * sizeof(int16_t)) ||
		!save_exact(original, saved, size) || !save_exact(restored, round_trip, size) ||
		memcmp(saved, round_trip, size))
		goto done;

	ok = 1;
done:
	free(damaged);
	free(after_bad_load);
	free(before_bad_load);
	free(round_trip);
	free(guarded);
	free(restored);
	free(original);
	free(rom);
	return ok;
}

static int test_audio_register_writes_are_timed_within_frame(void)
{
	static const uint8_t program[] = {
		0xa9, 0x01,             /* lda #$01 */
		0x8d, 0xf0, 0x75,       /* sta $75f0: voice 0 on */
		0xa0, 0x80,             /* ldy #$80 */
		0xa2, 0x00,             /* outer: ldx #$00 */
		0xca,                   /* inner: dex */
		0xd0, 0xfd,             /* bne inner */
		0x88,                   /* dey */
		0xd0, 0xf8,             /* bne outer */
		0xa9, 0x00,             /* lda #$00 */
		0x8d, 0xf0, 0x75,       /* sta $75f0: voice 0 off */
		0x4c, 0x14, 0x80        /* idle: jmp idle */
	};
	uint8_t *rom = NULL;
	drgqst_core *core = NULL;
	uint8_t *registers;
	const int16_t *pcm;
	size_t first_nonzero = DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME;
	size_t last_nonzero = 0;
	size_t frame;
	int ok = 0;

	rom = (uint8_t *)malloc(TEST_ROM_SIZE);
	core = (drgqst_core *)malloc(sizeof(*core));
	if (!rom || !core)
		goto done;
	memset(rom, 0x7f, TEST_ROM_SIZE);
	memcpy(rom + 0x8000, program, sizeof(program));
	rom[0xfffa] = 0x00;
	rom[0xfffb] = 0x80;
	rom[0xfffc] = 0x00;
	rom[0xfffd] = 0x80;
	rom[0xfffe] = 0x00;
	rom[0xffff] = 0x80;
	if (!drgqst_core_init(core, rom, TEST_ROM_SIZE))
		goto done;

	registers = core->machine.state.main_ram + 0x0200;
	registers[0x00] = 0xfe; /* WM2, maximum 14-bit pitch step */
	registers[0x01] = 0xff;
	registers[0x02] = 0x00;
	registers[0x03] = 0x00;
	registers[0x04] = 0x01;
	registers[0x05] = 0x00;
	registers[0x06] = 0x01;
	registers[0x08] = 0x0f; /* VM0, full voice gain */
	registers[0x0a] = 0xff;
	registers[0x0c] = 0xff;

	CHECK(drgqst_core_run_frame(core) != NULL);
	pcm = drgqst_core_frame_audio(core);
	for (frame = 0; frame < DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME; ++frame)
	{
		if (pcm[frame * 2U] || pcm[frame * 2U + 1U])
		{
			if (first_nonzero == DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME)
				first_nonzero = frame;
			last_nonzero = frame;
		}
	}
	/* The synthetic CPU starts voice 0 immediately, then stops it after about
	 * 164k of the frame's 358k cycles.  Whole-frame audio generation would
	 * incorrectly produce complete silence because only the final off state
	 * would be observed. */
	CHECK(first_nonzero < 4U);
	CHECK(last_nonzero > 300U && last_nonzero < 450U);
	for (frame = last_nonzero + 1U;
		frame < DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME; ++frame)
		CHECK(pcm[frame * 2U] == 0 && pcm[frame * 2U + 1U] == 0);
	ok = 1;
done:
	free(core);
	free(rom);
	return ok;
}

static int test_repeated_one_piece_load_resets_host_input(void)
{
	uint8_t *rom = NULL;
	drgqst_core *core = NULL;
	uint8_t *state = NULL;
	const size_t state_size = drgqst_state_serialized_size();
	size_t written = 0;
	unsigned iteration;
	int ok = 0;

	rom = (uint8_t *)malloc(TEST_ROM_SIZE);
	core = (drgqst_core *)malloc(sizeof(*core));
	state = (uint8_t *)malloc(state_size);
	if (!rom || !core || !state)
		goto done;
	make_test_rom(rom);
	if (!drgqst_core_init_profile(core, rom, TEST_ROM_SIZE,
		DRGQST_CORE_BAN_ONEP) ||
		!drgqst_state_save(core, state, state_size, &written) ||
		written != state_size)
		goto done;

	for (iteration = 0; iteration < 5000; ++iteration)
	{
		core->ban_onep_sync_divider = 7;
		core->ban_onep_sync_phase = 3;
		core->ban_onep_buttons = 2;
		core->ban_onep_drag_active = 1;
		core->ban_onep_drag_origin_x = 0x12;
		core->ban_onep_left_punch = 10;
		core->ban_onep_right_punch = 12;
		core->ban_onep_aim_x = 0x93;
		core->ban_onep_bazooka_phase = 7;
		if (!drgqst_state_load(core, state, state_size) ||
			core->ban_onep_sync_divider || core->ban_onep_sync_phase ||
			core->ban_onep_buttons || core->ban_onep_drag_active ||
			core->ban_onep_drag_origin_x != core->ban_onep_aim_x ||
			core->ban_onep_left_punch || core->ban_onep_right_punch ||
			core->ban_onep_bazooka_phase)
			goto done;
	}
	ok = 1;
done:
	free(state);
	free(core);
	free(rom);
	return ok;
}

static int test_one_piece_bazooka_gesture(void)
{
	uint8_t *rom = (uint8_t *)malloc(TEST_ROM_SIZE);
	drgqst_core *core = (drgqst_core *)malloc(sizeof(*core));
	unsigned frame;
	int ok = 0;

	if (!rom || !core)
		goto done;
	make_test_rom(rom);
	if (!drgqst_core_init_profile(core, rom, TEST_ROM_SIZE,
		DRGQST_CORE_BAN_ONEP))
		goto done;
	drgqst_core_trigger_bazooka(core);
	for (frame = 0; frame < 12; ++frame)
	{
		const unsigned expected = 0xdfU -
			((0xdfU - 0x20U) * frame + 5U) / 11U;
		drgqst_core_set_mouse(core, 0x80, 0x80, 0, 0);
		if (core->ban_onep_bazooka_phase != frame + 1U ||
			core->ban_onep_left_punch != (frame ? 10U : 1U) ||
			core->ban_onep_right_punch != frame + 1U ||
			core->ban_onep_aim_x != expected ||
			core->ban_onep_aim_y != expected ||
			(core->machine.state.input0 & 0x03))
			goto done;
		drgqst_core_run_frame(core);
	}
	if (core->ban_onep_bazooka_phase)
		goto done;
	drgqst_core_set_mouse(core, 0x55, 0x77, 0, 0);
	if (core->ban_onep_aim_x != 0x55 || core->ban_onep_aim_y != 0x77)
		goto done;
	ok = 1;
done:
	free(core);
	free(rom);
	return ok;
}

static int test_onmyou_4mb_sensor_profile(void)
{
	uint8_t *rom = (uint8_t *)malloc(TEST_OMT_ROM_SIZE);
	drgqst_core *core = (drgqst_core *)malloc(sizeof(*core));
	uint8_t *state = (uint8_t *)malloc(drgqst_state_serialized_size());
	int ok = 0;

	if (!rom || !core || !state)
		goto done;
	make_test_rom_sized(rom, TEST_OMT_ROM_SIZE);
	if (!drgqst_core_init_profile(core, rom, TEST_OMT_ROM_SIZE,
		DRGQST_CORE_BAN_OMT) || core->machine.rom_size != TEST_OMT_ROM_SIZE)
		goto done;
	drgqst_core_set_mouse(core, 0x25, 0x91, 0, 0);
	if (core->machine.state.peripherals.sensor.host_x != 0x25 ||
		core->machine.state.peripherals.sensor.host_y != 0x91 ||
		core->machine.state.peripherals.sensor.host_mode != XAVIX_SENSOR_NARROW ||
		(core->machine.state.input0 & 0x03))
		goto done;
	drgqst_core_set_mouse(core, 0x35, 0xa1, 1, 0);
	if (core->machine.state.peripherals.sensor.host_mode != XAVIX_SENSOR_NARROW ||
		(core->machine.state.input0 & 0x03) != 0x01)
		goto done;
	drgqst_core_set_mouse(core, 0x45, 0xb1, 0, 1);
	if (core->machine.state.peripherals.sensor.host_mode != XAVIX_SENSOR_NARROW ||
		(core->machine.state.input0 & 0x03) != 0x02)
		goto done;
	drgqst_core_set_mouse(core, 0x4a, 0xb6, 1, 1);
	if (core->machine.state.peripherals.sensor.host_mode != XAVIX_SENSOR_STEP_FORWARD ||
		(core->machine.state.input0 & 0x03))
		goto done;
	drgqst_core_set_mouse(core, 0x55, 0xc1, 0, 0);
	if (core->machine.state.input0 & 0x03)
		goto done;
	core->machine.state.main_ram[0x1234] = 0x5a;
	if (!save_exact(core, state, drgqst_state_serialized_size()))
		goto done;
	core->machine.state.main_ram[0x1234] = 0xa5;
	if (!drgqst_state_load(core, state, drgqst_state_serialized_size()) ||
		core->machine.state.main_ram[0x1234] != 0x5a ||
		core->machine.rom_size != TEST_OMT_ROM_SIZE ||
		core->game_profile != DRGQST_CORE_BAN_OMT)
		goto done;
	ok = 1;
done:
	free(state);
	free(core);
	free(rom);
	return ok;
}

static void core_i2c_send_control(drgqst_core *core, uint8_t control)
{
	unsigned bit;

	/* Drive P1.4=SCL and P1.3=SDA high, then lower SDA for START. */
	xavix_machine_write_low(&core->machine, 0x7a01, 0x18);
	xavix_machine_write_low(&core->machine, 0x7a03, 0x18);
	xavix_machine_write_low(&core->machine, 0x7a01, 0x10);
	for (bit = 0; bit < 8; ++bit)
	{
		const uint8_t sda = control & (uint8_t)(0x80U >> bit) ? 0x08 : 0;
		xavix_machine_write_low(&core->machine, 0x7a01, sda);
		xavix_machine_write_low(&core->machine, 0x7a01,
			(uint8_t)(0x10 | sda));
	}
}

static int test_early_xavix_profiles(void)
{
	uint8_t *rom = (uint8_t *)malloc(TEST_ROM_SIZE);
	drgqst_core *core = (drgqst_core *)malloc(sizeof(*core));
	uint8_t *state = (uint8_t *)malloc(drgqst_state_serialized_size());
	static const uint8_t expected_bowl_sync[4] = { 0x00, 0x02, 0x06, 0x04 };
	size_t written = 0;
	unsigned phase;
	int ok = 0;

	if (!rom || !core || !state)
		goto done;
	make_test_rom(rom);
	if (!drgqst_core_init_profile(core, rom, TEST_ROM_SIZE,
		DRGQST_CORE_XAVIX_BASE))
		goto done;
	core->machine.state.input0 = 0x5a;
	if (xavix_machine_read_low(&core->machine, 0x7a00) != 0x5a)
		goto done;

	if (!drgqst_core_init_profile(core, rom, TEST_OMT_ROM_SIZE,
		DRGQST_CORE_XAVIX_I2C_24C16))
		goto done;
	core->machine.state.input0 = 0x80;
	core->machine.state.anport_regs[2] = 0x17;
	core->machine.state.anport_regs[3] = 0xe9;
	drgqst_core_set_tvpc_keyboard_row(core, 1, 0x03);
	if (xavix_machine_read_low(&core->machine, 0x7a00) != 0x80 ||
		xavix_machine_read_low(&core->machine, 0x7b10) != 0x17 ||
		xavix_machine_read_low(&core->machine, 0x7b11) != 0xe9 ||
		xavix_machine_read_external(&core->machine, 0x600001) != 0 ||
		xavix_machine_read_external(&core->machine, 0x600002) != 0x03 ||
		xavix_machine_read_external(&core->machine, 0x610002) !=
			rom[0x210002])
		goto done;

	if (!drgqst_core_init_profile(core, rom, TEST_ROM_SIZE,
		DRGQST_CORE_XAVIX2000_I2C_24C04))
		goto done;
	core->machine.state.input0 = 0xa5;
	if (xavix_machine_read_low(&core->machine, 0x7a00) != 0xa5)
		goto done;
	/* 24C04 control bit 1 selects address bit 8, while bit 2 remains a
	 * device-select line tied low.  Accepting A2 and rejecting A4 proves
	 * this profile is neither the 24C02 nor 24C08 wiring. */
	core_i2c_send_control(core, 0xa2);
	if (!core->machine.state.peripherals.eeprom.selected ||
		core->machine.state.peripherals.eeprom.pending_state !=
			XAVIX_I2C_RECEIVE_ADDRESS)
		goto done;
	if (!drgqst_core_init_profile(core, rom, TEST_ROM_SIZE,
		DRGQST_CORE_XAVIX2000_I2C_24C04))
		goto done;
	core_i2c_send_control(core, 0xa4);
	if (core->machine.state.peripherals.eeprom.selected ||
		core->machine.state.peripherals.eeprom.pending_state != XAVIX_I2C_IGNORE)
		goto done;

	/* Choro-Q uses the same plain 24C04 profile with a 4 MiB external ROM.
	 * Verify that this exact board size is accepted and mirrored, without
	 * attaching an optical-input profile. */
	if (!drgqst_core_init_profile(core, rom, TEST_OMT_ROM_SIZE,
		DRGQST_CORE_XAVIX2000_I2C_24C04) ||
		core->machine.rom_size != TEST_OMT_ROM_SIZE ||
		core->game_profile != DRGQST_CORE_XAVIX2000_I2C_24C04 ||
		xavix_machine_read_external(&core->machine, 0x401234) != rom[0x001234])
		goto done;

	/* Excite Bowling is a 2 MiB image mirrored four times over the 8 MiB
	 * external bus.  Keep the core whitelist strict while accepting exactly
	 * that additional board size. */
	if (!drgqst_core_init_profile(core, rom, TEST_BOWL_ROM_SIZE,
		DRGQST_CORE_EPO_BOWL_SENSOR_24C04) ||
		core->machine.rom_size != TEST_BOWL_ROM_SIZE ||
		xavix_machine_read_external(&core->machine, 0x001234) != rom[0x001234] ||
		xavix_machine_read_external(&core->machine, 0x201234) != rom[0x001234] ||
		xavix_machine_read_external(&core->machine, 0x601234) != rom[0x001234])
		goto done;
	for (phase = 0; phase < 4; ++phase)
	{
		if ((xavix_machine_read_low(&core->machine, 0x7a01) & 0x06) !=
			expected_bowl_sync[phase])
			goto done;
	}
	core_i2c_send_control(core, 0xa2);
	if (!core->machine.state.peripherals.eeprom.selected ||
		core->machine.state.peripherals.eeprom.pending_state !=
			XAVIX_I2C_RECEIVE_ADDRESS)
		goto done;
	core->machine.state.input0 = 0x2f;
	if (!drgqst_state_save(core, state, drgqst_state_serialized_size(),
		&written) || written != drgqst_state_serialized_size())
		goto done;
	core->machine.state.input0 = 0;
	core->ban_onep_sync_divider = 1;
	core->ban_onep_sync_phase = 3;
	if (!drgqst_state_load(core, state, written) ||
		core->machine.rom_size != TEST_BOWL_ROM_SIZE ||
		core->game_profile != DRGQST_CORE_EPO_BOWL_SENSOR_24C04 ||
		core->machine.state.input0 != 0x2f ||
		core->ban_onep_sync_divider || core->ban_onep_sync_phase)
		goto done;
	if (drgqst_core_init_profile(core, rom, TEST_BOWL_ROM_SIZE,
		DRGQST_CORE_XAVIX2000_I2C_24C04))
		goto done;
	if (drgqst_core_init_profile(core, rom, UINT32_C(0x100000),
		DRGQST_CORE_EPO_BOWL_SENSOR_24C04))
		goto done;
	ok = 1;
done:
	free(state);
	free(core);
	free(rom);
	return ok;
}

static int test_super_dash_ball_anport_and_nvram(void)
{
	uint8_t *rom = (uint8_t *)malloc(TEST_OMT_ROM_SIZE);
	drgqst_core *core = (drgqst_core *)malloc(sizeof(*core));
	uint32_t generation;
	int ok = 0;

	if (!rom || !core)
		goto done;
	make_test_rom_sized(rom, TEST_OMT_ROM_SIZE);
	if (!drgqst_core_init_profile(core, rom, TEST_OMT_ROM_SIZE,
		DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM_SDB))
		goto done;
	if (xavix_machine_read_low(&core->machine, 0x7b00) != 0x7f ||
		xavix_machine_read_low(&core->machine, 0x7b01) != 0x7f ||
		xavix_machine_read_low(&core->machine, 0x7b10) != 0x7f ||
		xavix_machine_read_low(&core->machine, 0x7b11) != 0x7f)
		goto done;
	drgqst_core_set_sdb_input(core, 0, 0x11, 0x81, 1);
	drgqst_core_set_sdb_input(core, 1, 0xfe, 0x40, 1);
	if (xavix_machine_read_low(&core->machine, 0x7b00) != 0x6f ||
		xavix_machine_read_low(&core->machine, 0x7b01) != 0xff ||
		xavix_machine_read_low(&core->machine, 0x7b10) != 0x82 ||
		xavix_machine_read_low(&core->machine, 0x7b11) != 0x40 ||
		!(core->machine.state.input1 & 0x01) ||
		!(core->machine.state.input0 & 0x10))
		goto done;
	xavix_machine_write_low(&core->machine, 0x7b00, 0xaa);
	if (xavix_machine_read_low(&core->machine, 0x7b00) != 0x6f ||
		core->machine.state.anport_regs[0] != 0x11)
		goto done;
	drgqst_core_set_sdb_input(core, 0, 0x00, 0xff, 0);
	if (core->machine.state.anport_regs[0] != 0x01 ||
		core->machine.state.anport_regs[1] != 0xfe ||
		xavix_machine_read_low(&core->machine, 0x7b00) != 0x7f ||
		xavix_machine_read_low(&core->machine, 0x7b01) != 0x82)
		goto done;
	generation = core->machine.nvram_write_generation;
	xavix_machine_write_low(&core->machine,
		XAVIX_PARALLEL_NVRAM_BASE + 7, 0x5a);
	core->machine.state.main_ram[XAVIX_PARALLEL_NVRAM_BASE - 1] = 0x33;
	if (core->machine.nvram_write_generation != generation + 1)
		goto done;
	drgqst_core_reset(core);
	if (core->machine.state.main_ram[XAVIX_PARALLEL_NVRAM_BASE + 7] != 0x5a ||
		core->machine.state.main_ram[XAVIX_PARALLEL_NVRAM_BASE - 1] != 0xff ||
		xavix_machine_read_low(&core->machine, 0x7b00) != 0x7f ||
		(core->machine.state.input0 & 0x10) ||
		(core->machine.state.input1 & 0x01))
		goto done;
	ok = 1;
done:
	free(core);
	free(rom);
	return ok;
}

static int test_generic_parallel_nvram_profile(void)
{
	uint8_t *rom = (uint8_t *)malloc(TEST_OMT_ROM_SIZE);
	drgqst_core *core = (drgqst_core *)malloc(sizeof(*core));
	uint32_t generation;
	unsigned channel;
	int ok = 0;

	if (!rom || !core)
		goto done;
	make_test_rom_sized(rom, TEST_OMT_ROM_SIZE);
	if (!drgqst_core_init_profile(core, rom, TEST_OMT_ROM_SIZE,
		DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM))
		goto done;
	if (core->game_profile != DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM ||
		xavix_machine_read_low(&core->machine, 0x7b00) != 0xff ||
		xavix_machine_read_low(&core->machine, 0x7b01) != 0xff ||
		xavix_machine_read_low(&core->machine, 0x7b10) != 0xff ||
		xavix_machine_read_low(&core->machine, 0x7b11) != 0xff)
		goto done;
	xavix_machine_write_low(&core->machine, 0x7b00, 0x22);
	if (xavix_machine_read_low(&core->machine, 0x7b00) != 0xff)
		goto done;
	for (channel = 0; channel < 8; ++channel)
	{
		const uint8_t control = (uint8_t)((channel & 3) |
			(channel >= 4 ? 0x10 : 0));
		xavix_machine_write_low(&core->machine, 0x7b81, control);
		if (xavix_machine_read_low(&core->machine, 0x7b80) != 0x00)
			goto done;
	}
	core->machine.state.input0 = 0xf3;
	if (xavix_machine_read_low(&core->machine, 0x7a00) != 0xf3)
		goto done;
	/* Host pointer input must not feed the unconnected optical sensor. */
	drgqst_core_set_mouse(core, 0x12, 0xe4, 1, 1);
	if (core->machine.state.input0 != 0xf3)
		goto done;
	generation = core->machine.nvram_write_generation;
	xavix_machine_write_low(&core->machine,
		XAVIX_PARALLEL_NVRAM_BASE + 0x123, 0x5a);
	if (core->machine.nvram_write_generation != generation + 1)
		goto done;
	xavix_machine_write_low(&core->machine,
		XAVIX_PARALLEL_NVRAM_BASE + 0x123, 0x5a);
	if (core->machine.nvram_write_generation != generation + 1)
		goto done;
	xavix_machine_write_low(&core->machine,
		XAVIX_PARALLEL_NVRAM_BASE - 1, 0x33);
	if (core->machine.nvram_write_generation != generation + 1)
		goto done;
	drgqst_core_reset(core);
	if (core->machine.state.main_ram[
			XAVIX_PARALLEL_NVRAM_BASE + 0x123] != 0x5a ||
		core->machine.state.main_ram[XAVIX_PARALLEL_NVRAM_BASE - 1] != 0xff ||
		xavix_machine_read_low(&core->machine, 0x7b00) != 0xff ||
		xavix_machine_read_low(&core->machine, 0x7b80) != 0xff ||
		core->machine.state.input0)
		goto done;
	ok = 1;
done:
	free(core);
	free(rom);
	return ok;
}

static int test_plain_xavix2000_profile(void)
{
	static const uint16_t anport_addresses[4] =
	{
		0x7b00, 0x7b01, 0x7b10, 0x7b11
	};
	static const uint8_t adc_controls[8] =
	{
		0x00, 0x01, 0x02, 0x03, 0x10, 0x11, 0x12, 0x13
	};
	uint8_t *rom = (uint8_t *)malloc(TEST_OMT_ROM_SIZE);
	drgqst_core *core = (drgqst_core *)malloc(sizeof(*core));
	uint8_t *state = (uint8_t *)malloc(drgqst_state_serialized_size());
	size_t written = 0;
	unsigned index;
	int ok = 0;

	if (!rom || !core || !state)
		goto done;
	make_test_rom_sized(rom, TEST_OMT_ROM_SIZE);
	if (!drgqst_core_init_profile(core, rom, TEST_OMT_ROM_SIZE,
		DRGQST_CORE_XAVIX2000_PLAIN) ||
		core->game_profile != DRGQST_CORE_XAVIX2000_PLAIN ||
		core->machine.rom_size != TEST_OMT_ROM_SIZE ||
		xavix_machine_read_external(&core->machine, 0x401234) != rom[0x001234])
		goto done;
	if (drgqst_core_init_profile(core, rom, TEST_BOWL_ROM_SIZE,
		DRGQST_CORE_XAVIX2000_PLAIN))
		goto done;

	core->machine.state.input0 = 0xa5;
	core->machine.state.input1 = 0x5a;
	if (xavix_machine_read_low(&core->machine, 0x7a00) != 0xa5 ||
		xavix_machine_read_low(&core->machine, 0x7a01) != 0x5a)
		goto done;
	core_i2c_send_control(core, 0xa2);
	if (core->machine.state.peripherals.eeprom.selected ||
		core->machine.state.peripherals.eeprom.protocol_state != XAVIX_I2C_IDLE ||
		core->machine.state.peripherals.sensor.pixel ||
		core->machine.state.peripherals.sensor.adc_phase)
		goto done;

	for (index = 0; index < 4; ++index)
	{
		if (xavix_machine_read_low(&core->machine,
			anport_addresses[index]) != 0xff)
			goto done;
		xavix_machine_write_low(&core->machine,
			anport_addresses[index], (uint8_t)(0x20 + index));
		if (xavix_machine_read_low(&core->machine,
			anport_addresses[index]) != 0xff ||
			core->machine.state.anport_regs[index] != 0x00)
			goto done;
	}
	if (xavix_machine_read_low(&core->machine, 0x7b80) != 0xff)
		goto done;
	for (index = 0; index < 8; ++index)
	{
		xavix_machine_write_low(&core->machine, 0x7b81,
			adc_controls[index]);
		if (xavix_machine_read_low(&core->machine, 0x7b80) != 0x00 ||
			core->machine.state.peripherals.sensor.pixel ||
			core->machine.state.peripherals.sensor.adc_phase)
			goto done;
	}

	/* Host pointer and Ham-chans packet helpers must remain disconnected. */
	core->machine.state.input0 = 0x3c;
	drgqst_core_set_mouse(core, 0x12, 0xe4, 1, 1);
	xavix_machine_write_low(&core->machine, 0x7a80, 0x01);
	drgqst_core_trigger_hamd_packet(core, 0x81);
	if (core->machine.state.input0 != 0x3c ||
		core->machine.state.peripherals.sensor.host_x != 0x80 ||
		core->machine.state.peripherals.sensor.host_y != 0x80 ||
		core->machine.state.ioevent_active ||
		core->machine.state.irq_source)
		goto done;

	core->machine.state.main_ram[XAVIX_PARALLEL_NVRAM_BASE + 0x123] = 0x5a;
	core->machine.state.input0 = 0x6d;
	if (!drgqst_state_save(core, state, drgqst_state_serialized_size(),
		&written) || written != drgqst_state_serialized_size())
		goto done;
	core->machine.state.input0 = 0;
	if (!drgqst_state_load(core, state, written) ||
		core->game_profile != DRGQST_CORE_XAVIX2000_PLAIN ||
		core->machine.rom_size != TEST_OMT_ROM_SIZE ||
		core->machine.state.input0 != 0x6d ||
		xavix_machine_read_low(&core->machine, 0x7b00) != 0xff)
		goto done;

	drgqst_core_reset(core);
	if (core->machine.state.main_ram[
		XAVIX_PARALLEL_NVRAM_BASE + 0x123] != 0xff ||
		core->machine.state.input0 ||
		xavix_machine_read_low(&core->machine, 0x7b00) != 0xff ||
		xavix_machine_read_low(&core->machine, 0x7b80) != 0xff)
		goto done;
	ok = 1;
done:
	free(state);
	free(core);
	free(rom);
	return ok;
}

static int test_epo_hamc_sensor_profile(void)
{
	static const uint8_t sync_values[4] = { 0x81, 0x83, 0x87, 0x85 };
	uint8_t *rom = (uint8_t *)malloc(TEST_OMT_ROM_SIZE);
	drgqst_core *core = (drgqst_core *)malloc(sizeof(*core));
	uint8_t *state = (uint8_t *)malloc(drgqst_state_serialized_size());
	size_t written = 0;
	unsigned index;
	int ok = 0;

	if (!rom || !core || !state)
		goto done;
	make_test_rom_sized(rom, TEST_OMT_ROM_SIZE);
	if (!drgqst_core_init_profile(core, rom, TEST_OMT_ROM_SIZE,
		DRGQST_CORE_EPO_HAMC_SENSOR) ||
		core->game_profile != DRGQST_CORE_EPO_HAMC_SENSOR ||
		core->machine.rom_size != TEST_OMT_ROM_SIZE)
		goto done;

	core->machine.state.input0 = 0xa5;
	core->machine.state.input1 = 0x81;
	if (xavix_machine_read_low(&core->machine, 0x7a00) != 0xa5)
		goto done;
	for (index = 0; index < sizeof(sync_values); ++index)
	{
		if (xavix_machine_read_low(&core->machine, 0x7a01) !=
			sync_values[index])
			goto done;
	}
	for (index = 0; index < 4; ++index)
	{
		const uint16_t address = (uint16_t)(index < 2 ?
			0x7b00 + index : 0x7b10 + index - 2);
		if (xavix_machine_read_low(&core->machine, address) != 0xff)
			goto done;
		xavix_machine_write_low(&core->machine, address,
			(uint8_t)(0x20 + index));
		if (xavix_machine_read_low(&core->machine, address) != 0xff ||
			core->machine.state.anport_regs[index] != 0x00)
			goto done;
	}

	core_i2c_send_control(core, 0xa2);
	if (core->machine.state.peripherals.eeprom.selected ||
		core->machine.state.peripherals.eeprom.protocol_state != XAVIX_I2C_IDLE)
		goto done;
	if (xavix_machine_read_low(&core->machine, 0x7b80) != 0xff)
		goto done;
	drgqst_core_set_mouse(core, 0x12, 0xe4, 1, 1);
	for (index = 0; index < 8; ++index)
	{
		const uint8_t control = (uint8_t)((index & 3) |
			(index >= 4 ? 0x10 : 0));
		xavix_machine_write_low(&core->machine, 0x7b81, control);
		if (xavix_machine_read_low(&core->machine, 0x7b80) != 0 ||
			core->machine.state.peripherals.sensor.pixel ||
			core->machine.state.peripherals.sensor.adc_phase ||
			core->machine.state.peripherals.sensor.host_x != 0x80 ||
			core->machine.state.peripherals.sensor.host_y != 0x80)
			goto done;
	}

	core->machine.state.main_ram[XAVIX_PARALLEL_NVRAM_BASE + 0x123] = 0x5a;
	(void)xavix_machine_read_low(&core->machine, 0x7a01);
	if (!drgqst_state_save(core, state, drgqst_state_serialized_size(),
		&written) || written != drgqst_state_serialized_size())
		goto done;
	core->ban_onep_sync_phase = 3;
	if (!drgqst_state_load(core, state, written) ||
		core->game_profile != DRGQST_CORE_EPO_HAMC_SENSOR ||
		core->ban_onep_sync_phase || core->ban_onep_sync_divider)
		goto done;

	drgqst_core_reset(core);
	if (core->machine.state.main_ram[
		XAVIX_PARALLEL_NVRAM_BASE + 0x123] != 0xff ||
		core->machine.state.input0 ||
		core->machine.state.peripherals.eeprom.selected ||
		core->machine.state.peripherals.sensor.pixel ||
		core->machine.state.peripherals.sensor.adc_phase)
		goto done;
	ok = 1;
done:
	free(state);
	free(core);
	free(rom);
	return ok;
}

static int test_tom_dpgm_sensor_24c08_profile(void)
{
	static const uint8_t sync_values[4] = { 0x89, 0x8b, 0x8f, 0x8d };
	uint8_t *rom = (uint8_t *)malloc(TEST_OMT_ROM_SIZE);
	drgqst_core *core = (drgqst_core *)malloc(sizeof(*core));
	uint8_t *state = (uint8_t *)malloc(drgqst_state_serialized_size());
	size_t written = 0;
	unsigned index;
	int ok = 0;

	if (!rom || !core || !state)
		goto done;
	make_test_rom_sized(rom, TEST_OMT_ROM_SIZE);
	if (!drgqst_core_init_profile(core, rom, TEST_OMT_ROM_SIZE,
		DRGQST_CORE_TOM_DPGM_SENSOR_24C08) ||
		core->game_profile != DRGQST_CORE_TOM_DPGM_SENSOR_24C08 ||
		core->machine.rom_size != TEST_OMT_ROM_SIZE ||
		core->video.sprite_watch.enabled)
		goto done;

	core->machine.state.input1 = 0x81;
	for (index = 0; index < sizeof(sync_values); ++index)
	{
		if (xavix_machine_read_low(&core->machine, 0x7a01) !=
			sync_values[index])
			goto done;
	}

	/* 24C08 accepts A4 as a bank-select control byte.  A 24C04 profile
	 * rejects that same control byte because its second select pin is low. */
	core_i2c_send_control(core, 0xa4);
	if (!core->machine.state.peripherals.eeprom.selected ||
		core->machine.state.peripherals.eeprom.pending_state !=
			XAVIX_I2C_RECEIVE_ADDRESS)
		goto done;

	/* This profile uses the generic CU5501 write strobe, but deliberately has
	 * no title-specific sprite watch or gameplay shortcut. */
	drgqst_core_set_mouse(core, 0x31, 0xb7, 1, 0);
	core->machine.state.peripherals.sensor.pixel = 37;
	xavix_machine_write_low(&core->machine, 0x7a03, 0x21);
	xavix_machine_write_low(&core->machine, 0x7a01, 0x21);
	if (core->machine.state.peripherals.sensor.pixel ||
		!core->machine.state.peripherals.sensor.illuminated ||
		core->machine.state.peripherals.sensor.scan_x != 0x31 ||
		core->machine.state.peripherals.sensor.scan_y != 0xb7 ||
		core->machine.state.peripherals.sensor.scan_mode !=
			XAVIX_SENSOR_BROADSIDE)
		goto done;

	if (!drgqst_state_save(core, state, drgqst_state_serialized_size(),
		&written) || written != drgqst_state_serialized_size())
		goto done;
	core->ban_onep_sync_divider = 1;
	core->ban_onep_sync_phase = 3;
	if (!drgqst_state_load(core, state, written) ||
		core->game_profile != DRGQST_CORE_TOM_DPGM_SENSOR_24C08 ||
		core->ban_onep_sync_divider || core->ban_onep_sync_phase)
		goto done;
	ok = 1;
done:
	free(state);
	free(core);
	free(rom);
	return ok;
}

int main(void)
{
	RUN_TEST(test_firmware_cursor_position());
	RUN_TEST(test_internal_cursor_watch_profiles());
	RUN_TEST(test_round_trip_and_continuation());
	RUN_TEST(test_audio_register_writes_are_timed_within_frame());
	RUN_TEST(test_repeated_one_piece_load_resets_host_input());
	RUN_TEST(test_one_piece_bazooka_gesture());
	RUN_TEST(test_onmyou_4mb_sensor_profile());
	RUN_TEST(test_early_xavix_profiles());
	RUN_TEST(test_super_dash_ball_anport_and_nvram());
	RUN_TEST(test_generic_parallel_nvram_profile());
	RUN_TEST(test_plain_xavix2000_profile());
	RUN_TEST(test_epo_hamc_sensor_profile());
	RUN_TEST(test_tom_dpgm_sensor_24c08_profile());
	printf("drgqst_state_test: all tests passed (%llu-byte state)\n",
		(unsigned long long)drgqst_state_serialized_size());
	return EXIT_SUCCESS;
}
