// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "xavix_audio.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct test_bus_context
{
	uint8_t ram[0x4000];
	const uint8_t *rom;
	size_t rom_size;
} test_bus_context;

static uint8_t read_register(void *opaque, uint16_t address)
{
	test_bus_context *context = (test_bus_context *)opaque;
	return address < sizeof(context->ram) ? context->ram[address] : 0;
}

static void write_register(void *opaque, uint16_t address, uint8_t data)
{
	test_bus_context *context = (test_bus_context *)opaque;
	if (address < sizeof(context->ram))
		context->ram[address] = data;
}

static uint8_t read_program(void *opaque, uint32_t address)
{
	test_bus_context *context = (test_bus_context *)opaque;
	if (address >= 0x010000U && address - 0x010000U < context->rom_size)
		return context->rom[address - 0x010000U];
	return 0x80;
}

static xavix_audio_bus make_bus(test_bus_context *context)
{
	xavix_audio_bus bus;
	bus.read_register_byte = read_register;
	bus.write_register_byte = write_register;
	bus.read_program_byte = read_program;
	bus.context = context;
	return bus;
}

static void write_u16(uint8_t *registers, unsigned offset, uint16_t value)
{
	registers[offset] = (uint8_t)value;
	registers[offset + 1U] = (uint8_t)(value >> 8);
}

static void configure_voice(uint8_t *registers, unsigned voice,
	uint16_t rate, unsigned type, uint16_t start, uint16_t loop,
	uint8_t volume, uint8_t left, uint8_t right)
{
	uint8_t *reg = registers + voice * 0x10U;
	write_u16(reg, 0x00, (uint16_t)((rate << 2) | (type & 3U)));
	write_u16(reg, 0x02, start);
	write_u16(reg, 0x04, loop);
	reg[0x06] = 0x01;
	reg[0x08] = volume & 0x0fU; /* VM0 direct left/right levels */
	reg[0x0a] = left;
	reg[0x0c] = right;
}

static uint64_t pcm_hash(const int16_t *samples, size_t frames)
{
	uint64_t hash = UINT64_C(1469598103934665603);
	size_t sample;

	for (sample = 0; sample < frames * 2U; ++sample)
	{
		const uint16_t value = (uint16_t)samples[sample];
		hash ^= value & 0xffU;
		hash *= UINT64_C(1099511628211);
		hash ^= value >> 8;
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

static void fill_test_rom(uint8_t *rom, size_t size)
{
	size_t index;

	for (index = 0; index < size; ++index)
	{
		uint8_t value = (uint8_t)(((index * 37U) + (index >> 2) + 1U) & 0xffU);
		if (value == 0x80U)
			value = 0x7fU;
		rom[index] = value;
	}
}

static void configure_pcm_and_noise(xavix_audio *audio,
	test_bus_context *context, const xavix_audio_bus *bus)
{
	uint8_t *registers = context->ram + 0x0200;

	configure_voice(registers, 0, 8191, 2, 0x0000, 0x0001,
		15, 0xff, 0x70);
	configure_voice(registers, 1, 4096, 1, 0x1234, 0,
		10, 0x50, 0xe0);
	xavix_audio_write(audio, bus, XAVIX_AUDIO_MASTER_VOLUME, 0xd0);
	xavix_audio_write(audio, bus, XAVIX_AUDIO_MIXER, 0x02);
	xavix_audio_write(audio, bus, XAVIX_AUDIO_VOICE_START_LO, 0x03);
	assert((xavix_audio_read(audio, bus, XAVIX_AUDIO_VOICE_STATUS_LO) & 3U) == 3U);
}

static void test_deterministic_pcm_and_restore(void)
{
	enum { FRAME_COUNT = 4096 };
	static uint8_t rom[0x10000];
	static int16_t first[FRAME_COUNT * 2U];
	static int16_t continuation[2048 * 2U];
	static int16_t restored[2048 * 2U];
	xavix_audio audio;
	xavix_audio checkpoint;
	xavix_audio restored_audio;
	test_bus_context context;
	test_bus_context restored_context;
	xavix_audio_bus bus;
	xavix_audio_bus restored_bus;
	uint64_t hash;

	memset(&context, 0, sizeof(context));
	fill_test_rom(rom, sizeof(rom));
	context.rom = rom;
	context.rom_size = sizeof(rom);
	bus = make_bus(&context);
	xavix_audio_init(&audio, XAVIX_AUDIO_DEFAULT_MASTER_CLOCK,
		XAVIX_AUDIO_DEFAULT_HOST_RATE, 0x80);
	assert(xavix_audio_native_rate(&audio) == 167791U);
	configure_pcm_and_noise(&audio, &context, &bus);
	assert(xavix_audio_generate(&audio, &bus, first, FRAME_COUNT) == FRAME_COUNT);
	hash = pcm_hash(first, FRAME_COUNT);
	fprintf(stderr, "pcm hash: %016llx\n", (unsigned long long)hash);
	assert(hash == UINT64_C(0xca262e242151b570));

	checkpoint = audio;
	restored_context = context;
	restored_context.rom = rom;
	restored_bus = make_bus(&restored_context);
	assert(xavix_audio_generate(&audio, &bus, continuation, 2048) == 2048);
	restored_audio = checkpoint;
	assert(xavix_audio_generate(&restored_audio, &restored_bus, restored, 2048) == 2048);
	assert(memcmp(continuation, restored, sizeof(continuation)) == 0);
	assert(memcmp(&audio, &restored_audio, sizeof(audio)) == 0);
}

static void test_fractional_resampler_starts_without_silence(void)
{
	uint8_t rom[32];
	int16_t output[49 * 2];
	xavix_audio audio;
	test_bus_context context;
	xavix_audio_bus bus;
	size_t frame;

	memset(&context, 0, sizeof(context));
	memset(rom, 0x7f, sizeof(rom));
	context.rom = rom;
	context.rom_size = sizeof(rom);
	bus = make_bus(&context);
	/* native_rate=1 kHz, host_rate=48 kHz exercises fractional holding. */
	xavix_audio_init(&audio, 128000U, 48000U, 0x80);
	configure_voice(context.ram + 0x0200, 0, 1, 2, 0, 1,
		15, 0xff, 0xff);
	xavix_audio_write(&audio, &bus, XAVIX_AUDIO_VOICE_START_LO, 1);
	assert(xavix_audio_generate(&audio, &bus, output, 49) == 49);
	for (frame = 0; frame < 49; ++frame)
	{
		assert(output[frame * 2U] > 0);
		assert(output[frame * 2U] == output[frame * 2U + 1U]);
	}
	/* The first native DAC value is available at time zero and is held for
	 * exactly 48 host frames; the 49th frame begins the second native tick. */
	assert(audio.native_ticks_generated == 2);
	assert(audio.resample_phase == 47000U);
}

static void test_one_shot_and_irq(void)
{
	uint8_t rom[32];
	xavix_audio audio;
	test_bus_context context;
	xavix_audio_bus bus;

	memset(&context, 0, sizeof(context));
	memset(rom, 0x80, sizeof(rom));
	context.rom = rom;
	context.rom_size = sizeof(rom);
	bus = make_bus(&context);
	xavix_audio_init(&audio, 128000U, 48000U, 0x80);
	configure_voice(context.ram + 0x0200, 0, 1, 3, 0, 0,
		15, 0xff, 0xff);
	xavix_audio_write(&audio, &bus, XAVIX_AUDIO_VOICE_START_LO, 1);
	xavix_audio_generate(&audio, &bus, NULL, 64);
	assert((xavix_audio_active_voices(&audio) & 1U) == 0);

	/* At this test clock, native_rate=1000.  Tempo 0xff and cycle rate zero
	 * produce a four-tick IRQ period. */
	xavix_audio_write(&audio, &bus, XAVIX_AUDIO_CYCLE_RATE, 0);
	xavix_audio_write(&audio, &bus, XAVIX_AUDIO_TEMPO_0, 0xff);
	xavix_audio_write(&audio, &bus, XAVIX_AUDIO_IRQ_STATUS, 0x01);
	xavix_audio_generate(&audio, &bus, NULL, 240);
	assert(xavix_audio_irq_pending(&audio));
	assert(xavix_audio_read(&audio, &bus, XAVIX_AUDIO_IRQ_STATUS) & 0x10U);
	xavix_audio_write(&audio, &bus, XAVIX_AUDIO_IRQ_STATUS, 0x11);
	assert(!xavix_audio_irq_pending(&audio));
}

static void test_register_page_and_decay_envelope(void)
{
	uint8_t rom[32];
	xavix_audio audio;
	test_bus_context context;
	xavix_audio_bus bus;
	uint8_t *registers;

	memset(&context, 0, sizeof(context));
	memset(rom, 1, sizeof(rom));
	context.rom = rom;
	context.rom_size = sizeof(rom);
	bus = make_bus(&context);
	xavix_audio_init(&audio, 128000U, 1000U, 0);
	xavix_audio_write(&audio, &bus, XAVIX_AUDIO_REGISTER_PAGE, 3);
	registers = context.ram + 0x0300;
	configure_voice(registers, 0, 1, 1, 0x2345, 0,
		15, 0x20, 0x10);
	registers[0x08] = 0x3f; /* VM3 decay, gain 15 */
	xavix_audio_write(&audio, &bus, XAVIX_AUDIO_CYCLE_RATE, 0);
	xavix_audio_write(&audio, &bus, XAVIX_AUDIO_TEMPO_0, 0xff);
	xavix_audio_write(&audio, &bus, XAVIX_AUDIO_VOICE_START_LO, 1);
	assert(audio.voice[0].noise_state == 0x2345U);
	assert(audio.voice[0].env_volume_left == 0x20U);
	assert(audio.voice[0].env_volume_right == 0x10U);
	xavix_audio_generate(&audio, &bus, NULL, 65);
	assert(audio.voice[0].env_volume_left == 0x1eU);
	assert(audio.voice[0].env_volume_right == 0x0fU);
	assert(xavix_audio_read(&audio, &bus, XAVIX_AUDIO_REGISTER_PAGE) == 3U);
	assert(xavix_audio_read(&audio, &bus, XAVIX_AUDIO_TEMPO_0) == 0xffU);
}

int main(void)
{
	test_deterministic_pcm_and_restore();
	test_one_shot_and_irq();
	test_register_page_and_decay_envelope();
	test_fractional_resampler_starts_without_silence();
	puts("xavix_audio_test: ok");
	return 0;
}
