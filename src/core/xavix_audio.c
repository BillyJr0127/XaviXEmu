// SPDX-License-Identifier: BSD-3-Clause
// MAME-derived portions copyright-holders: ramacat, David Haywood
// XaviXEmu port and modifications:
// Copyright (c) 2026 Billy Jr. and contributors
/*
 * Compact C port of the game-used XaviX audio engine.
 *
 * Voice, envelope, tempo and mixer behaviour is derived from the
 * BSD-3-Clause MAME xavix_sound.cpp/xavix_m.cpp implementations by ramacat
 * and David Haywood.  Host-rate conversion integrates the native zero-order
 * hold waveform over exact output periods.  It is deterministic and avoids
 * the modulation caused by averaging alternating groups of three and four
 * native samples without weighting their fractional endpoints.
 */

#include "xavix_audio.h"

#include <limits.h>
#include <string.h>

static const uint8_t s_mixer_order_multiplex[XAVIX_AUDIO_VOICES] =
{
	0x0, 0xa, 0x7, 0xd, 0xc, 0x6, 0xb, 0x1,
	0x4, 0xe, 0x3, 0x9, 0x8, 0x2, 0xf, 0x5
};

static const uint8_t s_mixer_order_broadcast[XAVIX_AUDIO_VOICES] =
{
	0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
	0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf
};

static const int16_t s_amplifier_gain[8] = { 2, 4, 8, 12, 16, 20, 20, 20 };

static uint8_t register_read(const xavix_audio *audio,
	const xavix_audio_bus *bus, unsigned offset)
{
	uint16_t address;

	if (!audio->register_page || !bus || !bus->read_register_byte)
		return 0;
	address = (uint16_t)(((uint16_t)(audio->register_page & 0x3fU) << 8) |
		(uint16_t)(offset & 0xffU));
	return bus->read_register_byte(bus->context, address);
}

static void register_write(const xavix_audio *audio,
	const xavix_audio_bus *bus, unsigned offset, uint8_t data)
{
	uint16_t address;

	if (!audio->register_page || !bus || !bus->write_register_byte)
		return;
	address = (uint16_t)(((uint16_t)(audio->register_page & 0x3fU) << 8) |
		(uint16_t)(offset & 0xffU));
	bus->write_register_byte(bus->context, address, data);
}

static uint8_t program_read(const xavix_audio_bus *bus, uint32_t address)
{
	if (!bus || !bus->read_program_byte)
		return 0x80;
	return bus->read_program_byte(bus->context, address & UINT32_C(0x00ffffff));
}

static uint32_t tempo_period_ticks(const xavix_audio *audio, uint8_t tempo)
{
	uint64_t ticks;

	if (!tempo)
		return 0;
	ticks = ((uint64_t)(audio->cycle_rate_divider + 1U) * UINT64_C(1024) +
		(tempo / 2U)) / tempo;
	if (ticks < 1U)
		ticks = 1U;
	if (ticks > UINT64_C(2000000))
		ticks = UINT64_C(2000000);
	return (uint32_t)ticks;
}

static uint32_t envelope_period_ticks(const xavix_audio *audio, uint8_t tempo)
{
	uint64_t ticks = tempo_period_ticks(audio, tempo);

	if (!ticks)
		return 0;
	ticks *= 16U;
	if (ticks > UINT64_C(64000000))
		ticks = UINT64_C(64000000);
	return (uint32_t)ticks;
}

static void reprogram_irq_timer(xavix_audio *audio, unsigned group)
{
	const uint8_t mask = (uint8_t)(1U << group);
	uint32_t period;

	if (group >= XAVIX_AUDIO_TEMPO_GROUPS || !(audio->irq_status & mask) ||
		!audio->tempo_register[group])
	{
		if (group < XAVIX_AUDIO_TEMPO_GROUPS)
		{
			audio->tempo_irq_period[group] = 0;
			audio->tempo_irq_countdown[group] = 0;
		}
		return;
	}

	period = tempo_period_ticks(audio, audio->tempo_register[group]);
	audio->tempo_irq_period[group] = period;
	audio->tempo_irq_countdown[group] = period;
}

static void set_tempo(xavix_audio *audio, unsigned group, uint8_t value)
{
	unsigned voice_index;

	if (group >= XAVIX_AUDIO_TEMPO_GROUPS)
		return;
	audio->tempo_divider[group] = value;
	for (voice_index = group; voice_index < XAVIX_AUDIO_VOICES;
		voice_index += XAVIX_AUDIO_TEMPO_GROUPS)
	{
		xavix_audio_voice *voice = &audio->voice[voice_index];
		const uint32_t period = value ? envelope_period_ticks(audio, value) : 0;

		voice->env_period_ticks = period;
		if (period)
			voice->env_countdown = period;
	}
}

static void set_cycle_rate(xavix_audio *audio, uint8_t value)
{
	unsigned voice_index;

	audio->cycle_rate_divider = value;
	for (voice_index = 0; voice_index < XAVIX_AUDIO_VOICES; ++voice_index)
	{
		xavix_audio_voice *voice = &audio->voice[voice_index];
		const uint32_t old_period = voice->env_period_ticks;
		const uint8_t tempo = audio->tempo_divider[voice_index & 3U];
		const uint32_t new_period = tempo ? envelope_period_ticks(audio, tempo) : 0;

		if (old_period && new_period)
			voice->env_countdown = (uint32_t)(
				((uint64_t)voice->env_countdown * new_period) / old_period);
		else if (!old_period && new_period)
			voice->env_countdown = new_period;
		voice->env_period_ticks = new_period;
	}
}

void xavix_audio_reset(xavix_audio *audio)
{
	uint32_t master_clock;
	uint32_t host_rate;
	uint8_t default_tempo;
	unsigned group;

	if (!audio)
		return;
	master_clock = audio->master_clock_hz ? audio->master_clock_hz :
		XAVIX_AUDIO_DEFAULT_MASTER_CLOCK;
	host_rate = audio->host_rate_hz ? audio->host_rate_hz :
		XAVIX_AUDIO_DEFAULT_HOST_RATE;
	default_tempo = audio->default_tempo;
	memset(audio, 0, sizeof(*audio));
	audio->master_clock_hz = master_clock;
	audio->native_rate_hz = master_clock / 128U;
	if (!audio->native_rate_hz)
		audio->native_rate_hz = 1;
	audio->host_rate_hz = host_rate;
	audio->default_tempo = default_tempo;
	audio->cycle_rate_register = 0;
	audio->cycle_rate_divider = 0x0f;
	audio->register_page = 0x02;
	audio->master_volume = 0xff;
	audio->mixer_amp = 2;
	/* MAME's reset gain is 2 until the first mixer-register write. */
	audio->mixer_gain = 2;

	for (group = 0; group < XAVIX_AUDIO_TEMPO_GROUPS; ++group)
		set_tempo(audio, group, default_tempo);
}

void xavix_audio_init(xavix_audio *audio, uint32_t master_clock_hz,
	uint32_t host_rate_hz, uint8_t default_tempo)
{
	if (!audio)
		return;
	memset(audio, 0, sizeof(*audio));
	audio->master_clock_hz = master_clock_hz ? master_clock_hz :
		XAVIX_AUDIO_DEFAULT_MASTER_CLOCK;
	audio->host_rate_hz = host_rate_hz ? host_rate_hz :
		XAVIX_AUDIO_DEFAULT_HOST_RATE;
	audio->default_tempo = default_tempo;
	xavix_audio_reset(audio);
}

static uint8_t envelope_fetch(const xavix_audio_voice *voice,
	const xavix_audio_bus *bus, uint16_t address)
{
	return program_read(bus, ((uint32_t)voice->env_bank << 16) | address);
}

static uint16_t next_low_nibble(uint16_t value)
{
	return (uint16_t)((value & 0xfff0U) | ((value + 1U) & 0x000fU));
}

static void load_voice(xavix_audio *audio, const xavix_audio_bus *bus,
	unsigned voice_index, int update_only)
{
	xavix_audio_voice *voice;
	unsigned base;
	uint16_t wave_control;
	uint16_t wave_address;
	uint16_t loop_address;
	uint16_t env_address_left;
	uint16_t env_address_right;
	uint8_t wave_bank;
	uint8_t env_config;
	uint8_t env_bank;
	uint8_t new_type;
	uint32_t new_rate;
	uint8_t tempo;

	if (voice_index >= XAVIX_AUDIO_VOICES)
		return;
	voice = &audio->voice[voice_index];
	base = voice_index * 0x10U;
	wave_control = (uint16_t)(register_read(audio, bus, base + 0x00U) |
		((uint16_t)register_read(audio, bus, base + 0x01U) << 8));
	wave_address = (uint16_t)(register_read(audio, bus, base + 0x02U) |
		((uint16_t)register_read(audio, bus, base + 0x03U) << 8));
	loop_address = (uint16_t)(register_read(audio, bus, base + 0x04U) |
		((uint16_t)register_read(audio, bus, base + 0x05U) << 8));
	wave_bank = register_read(audio, bus, base + 0x06U);
	env_config = register_read(audio, bus, base + 0x08U);
	env_address_left = (uint16_t)(register_read(audio, bus, base + 0x0aU) |
		((uint16_t)register_read(audio, bus, base + 0x0bU) << 8));
	env_address_right = (uint16_t)(register_read(audio, bus, base + 0x0cU) |
		((uint16_t)register_read(audio, bus, base + 0x0dU) << 8));
	env_bank = register_read(audio, bus, base + 0x0eU);
	new_type = (uint8_t)(wave_control & 0x03U);
	new_rate = wave_control >> 2;

	voice->volume = env_config & 0x0fU;
	voice->env_mode = (env_config >> 4) & 0x03U;
	voice->env_bank = env_bank;
	voice->env_rom_base_left = env_address_left;
	voice->env_rom_base_right = env_address_right;
	voice->type = new_type;
	voice->rate = new_rate;
	if (new_type == 1U && !voice->noise_state)
		voice->noise_state = wave_address ? wave_address : 0xace1U;
	if (update_only)
		return;

	voice->enabled = 1;
	voice->bank = wave_bank;
	voice->position = (uint32_t)wave_address << 14;
	voice->loop_position = loop_address ?
		(uint32_t)(loop_address - 1U) << 14 : 0;
	voice->start_position = voice->position;
	voice->noise_state = wave_address ? wave_address : 0xace1U;
	tempo = audio->tempo_divider[voice_index & 3U];
	voice->env_period_ticks = tempo ? envelope_period_ticks(audio, tempo) : 0;
	voice->env_countdown = voice->env_period_ticks;
	voice->env_active_left = 1;
	voice->env_active_right = 1;

	switch (voice->env_mode)
	{
	case 0:
		voice->env_volume_left = register_read(audio, bus, base + 0x0aU);
		voice->env_volume_right = register_read(audio, bus, base + 0x0cU);
		break;
	case 1:
		voice->env_volume_left = envelope_fetch(voice, bus, env_address_left);
		voice->env_volume_right = envelope_fetch(voice, bus, env_address_right);
		voice->env_pos_left = next_low_nibble(env_address_left);
		voice->env_pos_right = next_low_nibble(env_address_right);
		register_write(audio, bus, base + 0x0aU, (uint8_t)env_address_left);
		register_write(audio, bus, base + 0x0cU, (uint8_t)env_address_right);
		break;
	case 2:
		voice->env_pos_left = env_address_left;
		voice->env_pos_right = env_address_right;
		voice->env_volume_left = envelope_fetch(voice, bus, env_address_left);
		voice->env_volume_right = envelope_fetch(voice, bus, env_address_right);
		register_write(audio, bus, base + 0x0aU, (uint8_t)env_address_left);
		register_write(audio, bus, base + 0x0cU, (uint8_t)env_address_right);
		voice->env_pos_left = (uint16_t)(env_address_left + 1U);
		voice->env_pos_right = (uint16_t)(env_address_right + 1U);
		break;
	case 3:
		voice->env_volume_left = register_read(audio, bus, base + 0x0aU);
		voice->env_volume_right = register_read(audio, bus, base + 0x0cU);
		break;
	default:
		break;
	}
}

static void disable_voice(xavix_audio *audio, unsigned voice_index)
{
	if (voice_index < XAVIX_AUDIO_VOICES)
		audio->voice[voice_index].enabled = 0;
}

static void write_start_mask(xavix_audio *audio, const xavix_audio_bus *bus,
	unsigned half, uint8_t data)
{
	unsigned bit;
	const uint8_t old = audio->voice_start[half];

	for (bit = 0; bit < 8U; ++bit)
	{
		const uint8_t mask = (uint8_t)(1U << bit);
		if ((data & mask) != (old & mask))
		{
			const unsigned voice = half * 8U + bit;
			if (data & mask)
				load_voice(audio, bus, voice, 0);
			else
				disable_voice(audio, voice);
		}
	}
	audio->voice_start[half] = data;
}

static uint8_t voice_status(const xavix_audio *audio, unsigned half)
{
	uint8_t result = 0;
	unsigned bit;

	for (bit = 0; bit < 8U; ++bit)
		if (audio->voice[half * 8U + bit].enabled)
			result = (uint8_t)(result | (1U << bit));
	return result;
}

uint8_t xavix_audio_read(xavix_audio *audio, const xavix_audio_bus *bus,
	unsigned offset)
{
	(void)bus;
	if (!audio || offset >= XAVIX_AUDIO_MMIO_BYTES)
		return 0xff;

	switch (offset)
	{
	case XAVIX_AUDIO_VOICE_START_LO:
	case XAVIX_AUDIO_VOICE_START_HI:
		return audio->voice_start[offset & 1U];
	case XAVIX_AUDIO_VOICE_UPDATE_LO:
	case XAVIX_AUDIO_VOICE_UPDATE_HI:
		return 0;
	case XAVIX_AUDIO_VOICE_STATUS_LO:
	case XAVIX_AUDIO_VOICE_STATUS_HI:
		return voice_status(audio, offset - XAVIX_AUDIO_VOICE_STATUS_LO);
	case XAVIX_AUDIO_MASTER_VOLUME:
		return audio->master_volume;
	case XAVIX_AUDIO_REGISTER_PAGE:
		return audio->register_page & 0x3fU;
	case XAVIX_AUDIO_CYCLE_RATE:
		return audio->cycle_rate_register;
	case XAVIX_AUDIO_MIXER:
		return (uint8_t)((audio->mixer_monaural ? 0x80U : 0U) |
			((audio->mixer_capacity & 3U) << 4) | (audio->mixer_amp & 7U));
	case XAVIX_AUDIO_TEMPO_0:
	case XAVIX_AUDIO_TEMPO_1:
	case XAVIX_AUDIO_TEMPO_2:
	case XAVIX_AUDIO_TEMPO_3:
		return audio->tempo_register[offset - XAVIX_AUDIO_TEMPO_0];
	case XAVIX_AUDIO_IRQ_STATUS:
		return audio->irq_status;
	case XAVIX_AUDIO_DAC_CONTROL:
		return (uint8_t)((audio->dac_broadcast ? 0x80U : 0U) |
			((audio->dac_gap & 3U) << 5) | ((audio->dac_lead & 7U) << 2) |
			(audio->dac_lag & 3U));
	default:
		return 0;
	}
}

void xavix_audio_write(xavix_audio *audio, const xavix_audio_bus *bus,
	unsigned offset, uint8_t data)
{
	unsigned bit;

	if (!audio || offset >= XAVIX_AUDIO_MMIO_BYTES)
		return;
	switch (offset)
	{
	case XAVIX_AUDIO_VOICE_START_LO:
	case XAVIX_AUDIO_VOICE_START_HI:
		write_start_mask(audio, bus, offset & 1U, data);
		break;
	case XAVIX_AUDIO_VOICE_UPDATE_LO:
	case XAVIX_AUDIO_VOICE_UPDATE_HI:
		for (bit = 0; bit < 8U; ++bit)
			if (data & (1U << bit))
				load_voice(audio, bus, (offset & 1U) * 8U + bit, 1);
		break;
	case XAVIX_AUDIO_MASTER_VOLUME:
		audio->master_volume = data;
		break;
	case XAVIX_AUDIO_REGISTER_PAGE:
		audio->register_page = data & 0x3fU;
		break;
	case XAVIX_AUDIO_CYCLE_RATE:
		audio->cycle_rate_register = data;
		set_cycle_rate(audio, data);
		break;
	case XAVIX_AUDIO_MIXER:
		audio->mixer_monaural = (data >> 7) & 1U;
		audio->mixer_capacity = (data >> 4) & 3U;
		audio->mixer_amp = data & 7U;
		audio->mixer_gain = s_amplifier_gain[audio->mixer_amp];
		break;
	case XAVIX_AUDIO_TEMPO_0:
	case XAVIX_AUDIO_TEMPO_1:
	case XAVIX_AUDIO_TEMPO_2:
	case XAVIX_AUDIO_TEMPO_3:
		bit = offset - XAVIX_AUDIO_TEMPO_0;
		audio->tempo_register[bit] = data;
		set_tempo(audio, bit, data);
		reprogram_irq_timer(audio, bit);
		break;
	case XAVIX_AUDIO_IRQ_STATUS:
		{
			const uint8_t old_enable = audio->irq_status & 0x0fU;
			const uint8_t clear = (data >> 4) & 0x0fU;
			const uint8_t new_enable = data & 0x0fU;
			const uint8_t changed = old_enable ^ new_enable;

			audio->irq_status &= (uint8_t)~(clear << 4);
			audio->irq_status = (uint8_t)((audio->irq_status & 0xf0U) |
				new_enable);
			for (bit = 0; bit < XAVIX_AUDIO_TEMPO_GROUPS; ++bit)
				if (changed & (1U << bit))
					reprogram_irq_timer(audio, bit);
		}
		break;
	case XAVIX_AUDIO_DAC_CONTROL:
		audio->dac_broadcast = (data >> 7) & 1U;
		audio->dac_gap = (data >> 5) & 3U;
		audio->dac_lead = (data >> 2) & 7U;
		audio->dac_lag = data & 3U;
		break;
	default:
		break;
	}
}

static uint8_t decay_level(uint8_t value)
{
	const uint8_t high = value >> 4;
	const uint8_t low = value & 0x0fU;
	const uint8_t subtract = (uint8_t)(high + (low ? 1U : 0U));

	return subtract >= value ? 0 : (uint8_t)(value - subtract);
}

static void step_envelope(xavix_audio *audio, const xavix_audio_bus *bus,
	unsigned voice_index)
{
	xavix_audio_voice *voice = &audio->voice[voice_index];
	const uint8_t tempo = audio->tempo_divider[voice_index & 3U];
	const uint32_t target_period = envelope_period_ticks(audio, tempo);
	unsigned base = voice_index * 0x10U;

	if (!voice->enabled)
		return;
	if (voice->env_period_ticks != target_period)
	{
		const uint32_t old_period = voice->env_period_ticks ?
			voice->env_period_ticks : 1U;
		voice->env_countdown = (uint32_t)(
			((uint64_t)voice->env_countdown * target_period) / old_period);
		voice->env_period_ticks = target_period;
	}
	if (!voice->env_period_ticks)
		return;
	if (voice->env_countdown)
	{
		voice->env_countdown--;
		return;
	}
	voice->env_countdown = voice->env_period_ticks;

	switch (voice->env_mode)
	{
	case 0:
		voice->env_volume_left = register_read(audio, bus, base + 0x0aU);
		voice->env_volume_right = register_read(audio, bus, base + 0x0cU);
		break;
	case 1:
		if (voice->env_active_left)
		{
			const uint16_t address = (uint16_t)voice->env_pos_left;
			voice->env_volume_left = envelope_fetch(voice, bus, address);
			voice->env_pos_left = next_low_nibble(address);
			register_write(audio, bus, base + 0x0aU,
				(uint8_t)voice->env_pos_left);
		}
		if (voice->env_active_right)
		{
			const uint16_t address = (uint16_t)voice->env_pos_right;
			voice->env_volume_right = envelope_fetch(voice, bus, address);
			voice->env_pos_right = next_low_nibble(address);
			register_write(audio, bus, base + 0x0cU,
				(uint8_t)voice->env_pos_right);
		}
		break;
	case 2:
		if (voice->env_active_left)
		{
			const uint16_t address = (uint16_t)voice->env_pos_left;
			voice->env_volume_left = envelope_fetch(voice, bus, address);
			register_write(audio, bus, base + 0x0aU, (uint8_t)address);
			voice->env_pos_left = (uint16_t)(address + 1U);
		}
		if (voice->env_active_right)
		{
			const uint16_t address = (uint16_t)voice->env_pos_right;
			voice->env_volume_right = envelope_fetch(voice, bus, address);
			register_write(audio, bus, base + 0x0cU, (uint8_t)address);
			voice->env_pos_right = (uint16_t)(address + 1U);
		}
		break;
	case 3:
		voice->env_volume_left = decay_level(voice->env_volume_left);
		voice->env_volume_right = decay_level(voice->env_volume_right);
		break;
	default:
		break;
	}

	if ((voice->env_volume_left | voice->env_volume_right) == 0)
	{
		voice->env_active_left = 0;
		voice->env_active_right = 0;
	}
}

static void step_pitch(const xavix_audio *audio, const xavix_audio_bus *bus,
	xavix_audio_voice *voice, unsigned voice_index)
{
	const unsigned base = voice_index * 0x10U;
	const uint16_t control = (uint16_t)(register_read(audio, bus, base) |
		((uint16_t)register_read(audio, bus, base + 1U) << 8));
	const uint32_t target = control >> 2;

	if (voice->rate < target)
		voice->rate++;
	else if (voice->rate > target)
		voice->rate--;
}

static int32_t decode_pcm(uint8_t raw)
{
	uint8_t value;

	if ((raw & 0x7fU) == 0)
		value = 0x80;
	else
		value = (uint8_t)(((~raw) & 0x80U) | (raw & 0x7fU));
	return (int32_t)value - 128;
}

static int16_t clamp_sample(int64_t sample)
{
	if (sample > INT16_MAX)
		return INT16_MAX;
	if (sample < INT16_MIN)
		return INT16_MIN;
	return (int16_t)sample;
}

static int64_t final_gain(const xavix_audio *audio, int64_t sample,
	unsigned allowed_voices)
{
	const int64_t scaled = sample * audio->mixer_gain *
		(int64_t)allowed_voices * 3;

	return scaled >= 0 ? (scaled + 64) >> 7 : -(((-scaled) + 64) >> 7);
}

static void step_irq_timers(xavix_audio *audio)
{
	unsigned group;

	for (group = 0; group < XAVIX_AUDIO_TEMPO_GROUPS; ++group)
	{
		if (!audio->tempo_irq_period[group])
			continue;
		if (audio->tempo_irq_countdown[group])
			audio->tempo_irq_countdown[group]--;
		if (!audio->tempo_irq_countdown[group])
		{
			const uint8_t mask = (uint8_t)(1U << group);
			if (audio->irq_status & mask)
				audio->irq_status = (uint8_t)(audio->irq_status | (mask << 4));
			audio->tempo_irq_countdown[group] = audio->tempo_irq_period[group];
		}
	}
}

static void generate_native_tick(xavix_audio *audio, const xavix_audio_bus *bus,
	int16_t *left_output, int16_t *right_output)
{
	const uint8_t *order = audio->dac_broadcast ?
		s_mixer_order_broadcast : s_mixer_order_multiplex;
	const uint8_t capacity = audio->mixer_capacity & 3U;
	const unsigned allowed = (capacity & 2U) ? 4U : (capacity & 1U) ? 8U : 16U;
	int64_t total_left = 0;
	int64_t total_right = 0;
	unsigned order_index;

	step_irq_timers(audio);
	for (order_index = 0; order_index < allowed; ++order_index)
	{
		const unsigned voice_index = order[order_index];
		xavix_audio_voice *voice = &audio->voice[voice_index];
		int32_t sample = 0;

		if (!voice->enabled)
			continue;
		if (voice->type == 1U)
		{
			uint16_t noise = voice->noise_state ? voice->noise_state : 0xace1U;
			const uint16_t feedback = (uint16_t)(((noise >> 0) ^ (noise >> 2) ^
				(noise >> 3) ^ (noise >> 5)) & 1U);
			uint8_t raw_noise;

			noise = (uint16_t)((noise >> 1) | (feedback << 15));
			if (!noise)
				noise = 0xace1U;
			voice->noise_state = noise;
			raw_noise = (uint8_t)(((noise >> 8) & 0xffU) ^ 0x80U);
			sample = raw_noise < 0x80U ? raw_noise : (int32_t)raw_noise - 256;
		}
		else
		{
			uint8_t raw = 0x80;
			if (voice->type != 0U)
				raw = program_read(bus, ((uint32_t)voice->bank << 16) |
					(voice->position >> 14));
			if (raw == 0x80U)
			{
				if (voice->type == 3U)
				{
					voice->enabled = 0;
					continue;
				}
				if ((voice->type == 2U || voice->type == 0U) &&
					(voice->position >> 14) != (voice->loop_position >> 14))
					voice->position = voice->loop_position;
			}
			else
			{
				sample = decode_pcm(raw);
			}
		}

		{
			const int64_t base = (int64_t)sample * (voice->volume & 0x0fU);
			const int64_t scaled = (base * audio->master_volume) / 255;
			total_left += (scaled * voice->env_volume_left) / 255;
			total_right += (scaled * voice->env_volume_right) / 255;
		}
		voice->position += voice->rate;
		step_envelope(audio, bus, voice_index);
		step_pitch(audio, bus, voice, voice_index);
		if (!voice->env_volume_left && !voice->env_volume_right)
			voice->enabled = 0;
	}

	total_left = final_gain(audio, total_left, allowed);
	total_right = final_gain(audio, total_right, allowed);
	*left_output = clamp_sample(total_left);
	*right_output = clamp_sample(total_right);
	if (audio->mixer_monaural)
	{
		const int32_t mono = ((int32_t)*left_output + *right_output) / 2;
		*left_output = (int16_t)mono;
		*right_output = (int16_t)mono;
	}
	audio->last_native_left = *left_output;
	audio->last_native_right = *right_output;
	audio->native_ticks_generated++;
}

size_t xavix_audio_generate(xavix_audio *audio, const xavix_audio_bus *bus,
	int16_t *interleaved_stereo, size_t frames)
{
	size_t frame;

	if (!audio || !audio->host_rate_hz || !audio->native_rate_hz)
		return 0;
	for (frame = 0; frame < frames; ++frame)
	{
		uint64_t remaining = audio->native_rate_hz;
		int64_t weighted_left = 0;
		int64_t weighted_right = 0;
		int16_t output_left;
		int16_t output_right;

		while (remaining)
		{
			uint64_t duration;

			if (!audio->resample_phase)
			{
				generate_native_tick(audio, bus, &audio->last_native_left,
					&audio->last_native_right);
				audio->resample_phase = audio->host_rate_hz;
			}
			duration = remaining < audio->resample_phase ?
				remaining : audio->resample_phase;
			weighted_left += (int64_t)audio->last_native_left *
				(int64_t)duration;
			weighted_right += (int64_t)audio->last_native_right *
				(int64_t)duration;
			remaining -= duration;
			audio->resample_phase -= duration;
		}
		output_left = (int16_t)(weighted_left / audio->native_rate_hz);
		output_right = (int16_t)(weighted_right / audio->native_rate_hz);
		if (interleaved_stereo)
		{
			interleaved_stereo[frame * 2U] = output_left;
			interleaved_stereo[frame * 2U + 1U] = output_right;
		}
		audio->host_frames_generated++;
	}
	return frames;
}

uint16_t xavix_audio_active_voices(const xavix_audio *audio)
{
	uint16_t result = 0;
	unsigned voice;

	if (!audio)
		return 0;
	for (voice = 0; voice < XAVIX_AUDIO_VOICES; ++voice)
		if (audio->voice[voice].enabled)
			result = (uint16_t)(result | (1U << voice));
	return result;
}

int xavix_audio_irq_pending(const xavix_audio *audio)
{
	if (!audio)
		return 0;
	return ((audio->irq_status & 0x0fU) &
		((audio->irq_status >> 4) & 0x0fU)) != 0;
}

uint32_t xavix_audio_native_rate(const xavix_audio *audio)
{
	return audio ? audio->native_rate_hz : 0;
}
