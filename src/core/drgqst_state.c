/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 Billy Jr. and contributors */
/*
 * Portable save states for the game-focused Dragon Quest runtime.
 * Multi-byte values are encoded explicitly in little-endian order; no C
 * structure layout or host pointer is part of the format.
 */
#include "drgqst_state.h"

#include <limits.h>
#include <string.h>

enum
{
	STATE_HEADER_SIZE = 24
};

typedef struct state_writer
{
	uint8_t *output;
	size_t capacity;
	size_t position;
	int valid;
} state_writer;

typedef struct state_reader
{
	const uint8_t *input;
	size_t size;
	size_t position;
	int valid;
} state_reader;

static const uint8_t s_magic[8] = { 'D', 'R', 'G', 'Q', 'S', 'T', 'S', '1' };
static const uint8_t s_cpu_tag[4] = { 'C', 'P', 'U', '1' };
static const uint8_t s_machine_tag[4] = { 'M', 'A', 'C', '1' };
static const uint8_t s_audio_tag[4] = { 'A', 'U', 'D', '1' };
static const uint8_t s_timing_tag[4] = { 'T', 'I', 'M', '1' };

static void writer_bytes(state_writer *writer, const uint8_t *data, size_t size)
{
	if (!writer->valid || writer->position > SIZE_MAX - size)
	{
		writer->valid = 0;
		return;
	}
	if (writer->output)
	{
		if (writer->position > writer->capacity || size > writer->capacity - writer->position)
		{
			writer->valid = 0;
			return;
		}
		memcpy(writer->output + writer->position, data, size);
	}
	writer->position += size;
}

static void writer_u8(state_writer *writer, uint8_t value)
{
	writer_bytes(writer, &value, 1);
}

static void writer_u16(state_writer *writer, uint16_t value)
{
	uint8_t bytes[2];
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	writer_bytes(writer, bytes, sizeof(bytes));
}

static void writer_u32(state_writer *writer, uint32_t value)
{
	uint8_t bytes[4];
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
	writer_bytes(writer, bytes, sizeof(bytes));
}

static void writer_u64(state_writer *writer, uint64_t value)
{
	uint8_t bytes[8];
	unsigned index;
	for (index = 0; index < sizeof(bytes); ++index)
		bytes[index] = (uint8_t)(value >> (index * 8));
	writer_bytes(writer, bytes, sizeof(bytes));
}

static void reader_bytes(state_reader *reader, uint8_t *output, size_t size)
{
	if (!reader->valid || reader->position > reader->size || size > reader->size - reader->position)
	{
		reader->valid = 0;
		if (output)
			memset(output, 0, size);
		return;
	}
	if (output)
		memcpy(output, reader->input + reader->position, size);
	reader->position += size;
}

static uint8_t reader_u8(state_reader *reader)
{
	uint8_t value = 0;
	reader_bytes(reader, &value, 1);
	return value;
}

static uint16_t reader_u16(state_reader *reader)
{
	uint8_t bytes[2];
	reader_bytes(reader, bytes, sizeof(bytes));
	return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t reader_u32(state_reader *reader)
{
	uint8_t bytes[4];
	reader_bytes(reader, bytes, sizeof(bytes));
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
		((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t reader_u64(state_reader *reader)
{
	uint8_t bytes[8];
	uint64_t value = 0;
	unsigned index;
	reader_bytes(reader, bytes, sizeof(bytes));
	for (index = 0; index < sizeof(bytes); ++index)
		value |= (uint64_t)bytes[index] << (index * 8);
	return value;
}

static uint32_t crc32_bytes(const uint8_t *data, size_t size)
{
	uint32_t crc = UINT32_MAX;
	size_t index;
	for (index = 0; index < size; ++index)
	{
		unsigned bit;
		crc ^= data[index];
		for (bit = 0; bit < 8; ++bit)
		{
			const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
			crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
		}
	}
	return ~crc;
}

static void store_u32(uint8_t output[4], uint32_t value)
{
	output[0] = (uint8_t)value;
	output[1] = (uint8_t)(value >> 8);
	output[2] = (uint8_t)(value >> 16);
	output[3] = (uint8_t)(value >> 24);
}

static int reader_tag(state_reader *reader, const uint8_t expected[4])
{
	uint8_t actual[4];
	reader_bytes(reader, actual, sizeof(actual));
	return reader->valid && memcmp(actual, expected, sizeof(actual)) == 0;
}

static void serialize_cpu(state_writer *writer, const xavix_cpu_t *cpu)
{
	writer_bytes(writer, s_cpu_tag, sizeof(s_cpu_tag));
	writer_u8(writer, cpu->a);
	writer_u8(writer, cpu->x);
	writer_u8(writer, cpu->y);
	writer_u8(writer, cpu->s);
	writer_u8(writer, cpu->p);
	writer_u16(writer, cpu->pc);
	writer_u8(writer, cpu->code_bank);
	writer_u8(writer, cpu->data_bank);
	writer_u8(writer, cpu->j);
	writer_u8(writer, cpu->k);
	writer_u8(writer, cpu->l);
	writer_u8(writer, cpu->m);
	writer_u32(writer, cpu->pa);
	writer_u32(writer, cpu->pb);
	writer_u8(writer, cpu->irq_line);
	writer_u8(writer, cpu->nmi_line);
	writer_u8(writer, cpu->nmi_pending);
	writer_u8(writer, cpu->irq_inhibit);
	writer_u8(writer, cpu->stopped);
	writer_u64(writer, cpu->total_cycles);
}

static int deserialize_cpu(state_reader *reader, xavix_cpu_t *cpu)
{
	if (!reader_tag(reader, s_cpu_tag))
		return 0;
	cpu->a = reader_u8(reader);
	cpu->x = reader_u8(reader);
	cpu->y = reader_u8(reader);
	cpu->s = reader_u8(reader);
	cpu->p = reader_u8(reader);
	cpu->pc = reader_u16(reader);
	cpu->code_bank = reader_u8(reader);
	cpu->data_bank = reader_u8(reader);
	cpu->j = reader_u8(reader);
	cpu->k = reader_u8(reader);
	cpu->l = reader_u8(reader);
	cpu->m = reader_u8(reader);
	cpu->pa = reader_u32(reader);
	cpu->pb = reader_u32(reader);
	cpu->irq_line = reader_u8(reader);
	cpu->nmi_line = reader_u8(reader);
	cpu->nmi_pending = reader_u8(reader);
	cpu->irq_inhibit = reader_u8(reader);
	cpu->stopped = reader_u8(reader);
	cpu->total_cycles = reader_u64(reader);
	return reader->valid;
}

static void writer_peripherals(state_writer *writer, const xavix_peripherals *peripherals)
{
	const size_t size = xavix_peripherals_serialized_size();
	size_t written = 0;

	if (!size || size > UINT32_MAX)
	{
		writer->valid = 0;
		return;
	}
	writer_u32(writer, (uint32_t)size);
	if (!writer->valid)
		return;
	if (writer->output)
	{
		if (writer->position > writer->capacity || size > writer->capacity - writer->position ||
			!xavix_peripherals_serialize(peripherals, writer->output + writer->position,
				writer->capacity - writer->position, &written) || written != size)
		{
			writer->valid = 0;
			return;
		}
	}
	if (writer->position > SIZE_MAX - size)
		writer->valid = 0;
	else
		writer->position += size;
}

static int reader_peripherals(state_reader *reader, xavix_peripherals *peripherals)
{
	const uint32_t encoded = reader_u32(reader);
	if (!reader->valid || !encoded || reader->position > reader->size ||
		encoded > reader->size - reader->position ||
		!xavix_peripherals_deserialize(peripherals,
			reader->input + reader->position, encoded))
	{
		reader->valid = 0;
		return 0;
	}
	reader->position += encoded;
	return 1;
}

static void serialize_machine(state_writer *writer, const xavix_machine_state *state)
{
	writer_bytes(writer, s_machine_tag, sizeof(s_machine_tag));
	writer_bytes(writer, state->main_ram, sizeof(state->main_ram));
	writer_bytes(writer, state->txarray, sizeof(state->txarray));
	writer_bytes(writer, state->fragment_ram, sizeof(state->fragment_ram));
	writer_bytes(writer, state->palette_sh, sizeof(state->palette_sh));
	writer_bytes(writer, state->palette_l, sizeof(state->palette_l));
	writer_bytes(writer, state->segment_regs, sizeof(state->segment_regs));
	writer_bytes(writer, state->tile_regs[0], sizeof(state->tile_regs));
	writer_u8(writer, state->sprite_reg);
	writer_bytes(writer, state->sprite_dma_param1, sizeof(state->sprite_dma_param1));
	writer_bytes(writer, state->sprite_dma_param2, sizeof(state->sprite_dma_param2));
	writer_u8(writer, state->arena_start);
	writer_u8(writer, state->arena_end);
	writer_u8(writer, state->arena_control);
	writer_u8(writer, state->colmix_sh);
	writer_u8(writer, state->colmix_l);
	writer_u8(writer, state->colmix_control);
	writer_u8(writer, state->video_control);
	writer_u8(writer, state->position_irq_x);
	writer_u8(writer, state->position_irq_y);
	writer_bytes(writer, state->sound_ram, sizeof(state->sound_ram));
	writer_bytes(writer, state->sound_regs, sizeof(state->sound_regs));
	writer_u8(writer, state->sound_regbase);
	writer_u8(writer, state->sound_irq_status);
	writer_bytes(writer, state->extbus_control, sizeof(state->extbus_control));
	writer_bytes(writer, state->io_data, sizeof(state->io_data));
	writer_bytes(writer, state->io_direction, sizeof(state->io_direction));
	writer_u8(writer, state->ioevent_enable);
	writer_u8(writer, state->ioevent_active);
	writer_u8(writer, state->input0);
	writer_u8(writer, state->input1);
	writer_bytes(writer, state->anport_regs, sizeof(state->anport_regs));
	writer_u8(writer, state->adc_control);
	writer_u8(writer, state->adc_latch);
	writer_peripherals(writer, &state->peripherals);
	writer_u8(writer, state->vector_enable);
	writer_bytes(writer, state->nmi_vector, sizeof(state->nmi_vector));
	writer_bytes(writer, state->irq_vector, sizeof(state->irq_vector));
	writer_u8(writer, state->irq_source);
	writer_u8(writer, state->irq_asserted);
	writer_u8(writer, state->nmi_asserted);
	writer_u64(writer, state->total_cycles);
	writer_u32(writer, state->frame_cycles);
}

static int deserialize_machine(state_reader *reader, xavix_machine_state *state)
{
	if (!reader_tag(reader, s_machine_tag))
		return 0;
	reader_bytes(reader, state->main_ram, sizeof(state->main_ram));
	reader_bytes(reader, state->txarray, sizeof(state->txarray));
	reader_bytes(reader, state->fragment_ram, sizeof(state->fragment_ram));
	reader_bytes(reader, state->palette_sh, sizeof(state->palette_sh));
	reader_bytes(reader, state->palette_l, sizeof(state->palette_l));
	reader_bytes(reader, state->segment_regs, sizeof(state->segment_regs));
	reader_bytes(reader, state->tile_regs[0], sizeof(state->tile_regs));
	state->sprite_reg = reader_u8(reader);
	reader_bytes(reader, state->sprite_dma_param1, sizeof(state->sprite_dma_param1));
	reader_bytes(reader, state->sprite_dma_param2, sizeof(state->sprite_dma_param2));
	state->arena_start = reader_u8(reader);
	state->arena_end = reader_u8(reader);
	state->arena_control = reader_u8(reader);
	state->colmix_sh = reader_u8(reader);
	state->colmix_l = reader_u8(reader);
	state->colmix_control = reader_u8(reader);
	state->video_control = reader_u8(reader);
	state->position_irq_x = reader_u8(reader);
	state->position_irq_y = reader_u8(reader);
	reader_bytes(reader, state->sound_ram, sizeof(state->sound_ram));
	reader_bytes(reader, state->sound_regs, sizeof(state->sound_regs));
	state->sound_regbase = reader_u8(reader);
	state->sound_irq_status = reader_u8(reader);
	reader_bytes(reader, state->extbus_control, sizeof(state->extbus_control));
	reader_bytes(reader, state->io_data, sizeof(state->io_data));
	reader_bytes(reader, state->io_direction, sizeof(state->io_direction));
	state->ioevent_enable = reader_u8(reader);
	state->ioevent_active = reader_u8(reader);
	state->input0 = reader_u8(reader);
	state->input1 = reader_u8(reader);
	reader_bytes(reader, state->anport_regs, sizeof(state->anport_regs));
	state->adc_control = reader_u8(reader);
	state->adc_latch = reader_u8(reader);
	if (!reader_peripherals(reader, &state->peripherals))
		return 0;
	state->vector_enable = reader_u8(reader);
	reader_bytes(reader, state->nmi_vector, sizeof(state->nmi_vector));
	reader_bytes(reader, state->irq_vector, sizeof(state->irq_vector));
	state->irq_source = reader_u8(reader);
	state->irq_asserted = reader_u8(reader);
	state->nmi_asserted = reader_u8(reader);
	state->total_cycles = reader_u64(reader);
	state->frame_cycles = reader_u32(reader);
	return reader->valid;
}

static void serialize_audio(state_writer *writer, const xavix_audio *audio)
{
	unsigned voice_index;
	writer_bytes(writer, s_audio_tag, sizeof(s_audio_tag));
	for (voice_index = 0; voice_index < XAVIX_AUDIO_VOICES; ++voice_index)
	{
		const xavix_audio_voice *voice = &audio->voice[voice_index];
		writer_u32(writer, voice->position);
		writer_u32(writer, voice->loop_position);
		writer_u32(writer, voice->start_position);
		writer_u32(writer, voice->rate);
		writer_u32(writer, voice->env_pos_left);
		writer_u32(writer, voice->env_pos_right);
		writer_u32(writer, voice->env_period_ticks);
		writer_u32(writer, voice->env_countdown);
		writer_u16(writer, voice->env_rom_base_left);
		writer_u16(writer, voice->env_rom_base_right);
		writer_u16(writer, voice->noise_state);
		writer_u8(writer, voice->enabled);
		writer_u8(writer, voice->bank);
		writer_u8(writer, voice->type);
		writer_u8(writer, voice->volume);
		writer_u8(writer, voice->env_bank);
		writer_u8(writer, voice->env_mode);
		writer_u8(writer, voice->env_volume_left);
		writer_u8(writer, voice->env_volume_right);
		writer_u8(writer, voice->env_active_left);
		writer_u8(writer, voice->env_active_right);
	}
	writer_u64(writer, audio->resample_phase);
	writer_u64(writer, audio->native_ticks_generated);
	writer_u64(writer, audio->host_frames_generated);
	writer_u32(writer, audio->master_clock_hz);
	writer_u32(writer, audio->native_rate_hz);
	writer_u32(writer, audio->host_rate_hz);
	for (voice_index = 0; voice_index < XAVIX_AUDIO_TEMPO_GROUPS; ++voice_index)
		writer_u32(writer, audio->tempo_irq_period[voice_index]);
	for (voice_index = 0; voice_index < XAVIX_AUDIO_TEMPO_GROUPS; ++voice_index)
		writer_u32(writer, audio->tempo_irq_countdown[voice_index]);
	writer_u16(writer, (uint16_t)audio->last_native_left);
	writer_u16(writer, (uint16_t)audio->last_native_right);
	writer_u16(writer, (uint16_t)audio->mixer_gain);
	writer_u8(writer, audio->default_tempo);
	writer_bytes(writer, audio->tempo_register, sizeof(audio->tempo_register));
	writer_bytes(writer, audio->tempo_divider, sizeof(audio->tempo_divider));
	writer_u8(writer, audio->cycle_rate_register);
	writer_u8(writer, audio->cycle_rate_divider);
	writer_bytes(writer, audio->voice_start, sizeof(audio->voice_start));
	writer_u8(writer, audio->register_page);
	writer_u8(writer, audio->irq_status);
	writer_u8(writer, audio->master_volume);
	writer_u8(writer, audio->mixer_monaural);
	writer_u8(writer, audio->mixer_capacity);
	writer_u8(writer, audio->mixer_amp);
	writer_u8(writer, audio->dac_broadcast);
	writer_u8(writer, audio->dac_gap);
	writer_u8(writer, audio->dac_lead);
	writer_u8(writer, audio->dac_lag);
}

static int deserialize_audio(state_reader *reader, xavix_audio *audio)
{
	unsigned voice_index;
	if (!reader_tag(reader, s_audio_tag))
		return 0;
	for (voice_index = 0; voice_index < XAVIX_AUDIO_VOICES; ++voice_index)
	{
		xavix_audio_voice *voice = &audio->voice[voice_index];
		voice->position = reader_u32(reader);
		voice->loop_position = reader_u32(reader);
		voice->start_position = reader_u32(reader);
		voice->rate = reader_u32(reader);
		voice->env_pos_left = reader_u32(reader);
		voice->env_pos_right = reader_u32(reader);
		voice->env_period_ticks = reader_u32(reader);
		voice->env_countdown = reader_u32(reader);
		voice->env_rom_base_left = reader_u16(reader);
		voice->env_rom_base_right = reader_u16(reader);
		voice->noise_state = reader_u16(reader);
		voice->enabled = reader_u8(reader);
		voice->bank = reader_u8(reader);
		voice->type = reader_u8(reader);
		voice->volume = reader_u8(reader);
		voice->env_bank = reader_u8(reader);
		voice->env_mode = reader_u8(reader);
		voice->env_volume_left = reader_u8(reader);
		voice->env_volume_right = reader_u8(reader);
		voice->env_active_left = reader_u8(reader);
		voice->env_active_right = reader_u8(reader);
	}
	audio->resample_phase = reader_u64(reader);
	audio->native_ticks_generated = reader_u64(reader);
	audio->host_frames_generated = reader_u64(reader);
	audio->master_clock_hz = reader_u32(reader);
	audio->native_rate_hz = reader_u32(reader);
	audio->host_rate_hz = reader_u32(reader);
	for (voice_index = 0; voice_index < XAVIX_AUDIO_TEMPO_GROUPS; ++voice_index)
		audio->tempo_irq_period[voice_index] = reader_u32(reader);
	for (voice_index = 0; voice_index < XAVIX_AUDIO_TEMPO_GROUPS; ++voice_index)
		audio->tempo_irq_countdown[voice_index] = reader_u32(reader);
	audio->last_native_left = (int16_t)reader_u16(reader);
	audio->last_native_right = (int16_t)reader_u16(reader);
	audio->mixer_gain = (int16_t)reader_u16(reader);
	audio->default_tempo = reader_u8(reader);
	reader_bytes(reader, audio->tempo_register, sizeof(audio->tempo_register));
	reader_bytes(reader, audio->tempo_divider, sizeof(audio->tempo_divider));
	audio->cycle_rate_register = reader_u8(reader);
	audio->cycle_rate_divider = reader_u8(reader);
	reader_bytes(reader, audio->voice_start, sizeof(audio->voice_start));
	audio->register_page = reader_u8(reader);
	audio->irq_status = reader_u8(reader);
	audio->master_volume = reader_u8(reader);
	audio->mixer_monaural = reader_u8(reader);
	audio->mixer_capacity = reader_u8(reader);
	audio->mixer_amp = reader_u8(reader);
	audio->dac_broadcast = reader_u8(reader);
	audio->dac_gap = reader_u8(reader);
	audio->dac_lead = reader_u8(reader);
	audio->dac_lag = reader_u8(reader);
	return reader->valid;
}

static void serialize_file(state_writer *writer, const xavix_cpu_t *cpu,
	const xavix_machine_state *machine, const xavix_audio *audio,
	uint32_t frame_fraction, uint32_t total_size)
{
	writer_bytes(writer, s_magic, sizeof(s_magic));
	writer_u16(writer, DRGQST_STATE_FORMAT_VERSION);
	writer_u16(writer, STATE_HEADER_SIZE);
	writer_u32(writer, total_size);
	writer_u32(writer, 0); /* payload CRC32, patched after serialization */
	writer_u32(writer, 0); /* reserved */
	serialize_cpu(writer, cpu);
	serialize_machine(writer, machine);
	serialize_audio(writer, audio);
	writer_bytes(writer, s_timing_tag, sizeof(s_timing_tag));
	writer_u32(writer, frame_fraction);
}

static int valid_boolean(uint8_t value)
{
	return value <= 1;
}

static int validate_cpu(const xavix_cpu_t *cpu)
{
	return cpu->pa <= UINT32_C(0x00ffffff) && cpu->pb <= UINT32_C(0x00ffffff) &&
		(cpu->p & (XAVIX_CPU_B | XAVIX_CPU_U)) == (XAVIX_CPU_B | XAVIX_CPU_U) &&
		valid_boolean(cpu->irq_line) && valid_boolean(cpu->nmi_line) &&
		valid_boolean(cpu->nmi_pending) && valid_boolean(cpu->irq_inhibit) &&
		valid_boolean(cpu->stopped);
}

static int validate_machine(const xavix_machine_state *state)
{
	return valid_boolean(state->irq_asserted) && valid_boolean(state->nmi_asserted);
}

static int validate_audio(const xavix_audio *audio)
{
	unsigned voice_index;
	if (!audio->master_clock_hz || !audio->native_rate_hz || !audio->host_rate_hz ||
		!valid_boolean(audio->mixer_monaural) || audio->mixer_capacity > 3 ||
		audio->mixer_amp > 7 || !valid_boolean(audio->dac_broadcast) ||
		audio->dac_gap > 3 || audio->dac_lead > 7 || audio->dac_lag > 3)
		return 0;
	for (voice_index = 0; voice_index < XAVIX_AUDIO_VOICES; ++voice_index)
	{
		const xavix_audio_voice *voice = &audio->voice[voice_index];
		if (!valid_boolean(voice->enabled) || voice->type > 3 || voice->volume > 15 ||
			voice->env_mode > 3 || !valid_boolean(voice->env_active_left) ||
			!valid_boolean(voice->env_active_right))
			return 0;
	}
	return 1;
}

size_t drgqst_state_serialized_size(void)
{
	xavix_cpu_t cpu;
	xavix_machine_state machine;
	xavix_audio audio;
	state_writer writer;

	memset(&cpu, 0, sizeof(cpu));
	memset(&machine, 0, sizeof(machine));
	memset(&audio, 0, sizeof(audio));
	writer.output = NULL;
	writer.capacity = 0;
	writer.position = 0;
	writer.valid = 1;
	serialize_file(&writer, &cpu, &machine, &audio, 0, 0);
	return writer.valid ? writer.position : 0;
}

int drgqst_state_save(const drgqst_core *core, uint8_t *output,
	size_t output_size, size_t *written)
{
	state_writer writer;
	const size_t required = drgqst_state_serialized_size();

	if (written)
		*written = required;
	if (!core || !output || !required || required > UINT32_MAX || output_size < required)
		return 0;
	if (core->frame_fraction >= XAVIX_FRAME_RATE || !validate_cpu(&core->cpu) ||
		!validate_machine(&core->machine.state) || !validate_audio(&core->audio))
		return 0;
	writer.output = output;
	writer.capacity = output_size;
	writer.position = 0;
	writer.valid = 1;
	serialize_file(&writer, &core->cpu, &core->machine.state, &core->audio,
		core->frame_fraction, (uint32_t)required);
	if (!writer.valid || writer.position != required)
		return 0;
	store_u32(output + 16, crc32_bytes(output + STATE_HEADER_SIZE,
		required - STATE_HEADER_SIZE));
	return 1;
}

int drgqst_state_load(drgqst_core *core, const uint8_t *input,
	size_t input_size)
{
	xavix_cpu_t restored_cpu;
	xavix_machine_state restored_machine;
	xavix_audio restored_audio;
	state_reader reader;
	uint8_t magic[8];
	uint16_t version;
	uint16_t header_size;
	uint32_t encoded_size;
	uint32_t encoded_crc;
	uint32_t reserved;
	uint32_t frame_fraction;
	xavix_cpu_read8_fn read8;
	xavix_cpu_write8_fn write8;
	void *cpu_opaque;
	const uint8_t *rom;
	size_t rom_size;
	xavix_machine_hooks hooks;
	const size_t expected_size = drgqst_state_serialized_size();

	if (!core || !input || !expected_size || input_size < STATE_HEADER_SIZE)
		return 0;
	memset(&restored_cpu, 0, sizeof(restored_cpu));
	memset(&restored_machine, 0, sizeof(restored_machine));
	memset(&restored_audio, 0, sizeof(restored_audio));
	reader.input = input;
	reader.size = input_size;
	reader.position = 0;
	reader.valid = 1;
	reader_bytes(&reader, magic, sizeof(magic));
	version = reader_u16(&reader);
	header_size = reader_u16(&reader);
	encoded_size = reader_u32(&reader);
	encoded_crc = reader_u32(&reader);
	reserved = reader_u32(&reader);
	if (!reader.valid || memcmp(magic, s_magic, sizeof(magic)) ||
		version != DRGQST_STATE_FORMAT_VERSION || header_size != STATE_HEADER_SIZE ||
		encoded_size != input_size || reserved ||
		encoded_crc != crc32_bytes(input + STATE_HEADER_SIZE,
			input_size - STATE_HEADER_SIZE) ||
		!deserialize_cpu(&reader, &restored_cpu) ||
		!deserialize_machine(&reader, &restored_machine) ||
		!deserialize_audio(&reader, &restored_audio) ||
		!reader_tag(&reader, s_timing_tag))
		return 0;
	frame_fraction = reader_u32(&reader);
	if (!reader.valid || reader.position != reader.size ||
		frame_fraction >= XAVIX_FRAME_RATE || !validate_cpu(&restored_cpu) ||
		!validate_machine(&restored_machine) || !validate_audio(&restored_audio))
		return 0;

	/* Preserve every destination-owned resource and callback binding. */
	read8 = core->cpu.read8;
	write8 = core->cpu.write8;
	cpu_opaque = core->cpu.opaque;
	rom = core->machine.rom;
	rom_size = core->machine.rom_size;
	hooks = core->machine.hooks;
	restored_cpu.read8 = read8;
	restored_cpu.write8 = write8;
	restored_cpu.opaque = cpu_opaque;
	core->cpu = restored_cpu;
	core->machine.state = restored_machine;
	core->machine.rom = rom;
	core->machine.rom_size = rom_size;
	core->machine.hooks = hooks;
	core->audio = restored_audio;
	core->frame_fraction = frame_fraction;

	/* Presentation data is derived.  Keep the configured sprite watch, clear
	 * stale output, and let the next host frame render the restored machine. */
	xavix_video_reset(&core->video);
	memset(core->frame_audio, 0, sizeof(core->frame_audio));
	core->audio_frame_cycles = 0;
	core->audio_frame_position = 0;
	core->audio_frame_active = 0;
	if (core->game_profile == DRGQST_CORE_BAN_ONEP ||
		core->game_profile == DRGQST_CORE_BAN_OMT ||
		core->game_profile == DRGQST_CORE_TTV_CU5501_24C02 ||
		core->game_profile == DRGQST_CORE_TTV_CU5501A_24C02)
	{
		/* The synthetic camera sync source is live host input, not part of the
		 * emulated machine checkpoint. */
		core->ban_onep_sync_divider = 0;
		core->ban_onep_sync_phase = 0;
		core->ttv_exposure_pending = 0;
	}
	if (core->game_profile == DRGQST_CORE_BAN_ONEP)
	{
		/* Button edges and gesture progress are host input state as well. */
		core->ban_onep_buttons = 0;
		core->ban_onep_drag_active = 0;
		core->ban_onep_drag_origin_x = core->ban_onep_aim_x;
		core->ban_onep_left_punch = 0;
		core->ban_onep_right_punch = 0;
		core->ban_onep_bazooka_phase = 0;
	}
	core->epo_hamd_packet = 0;
	core->epo_hamd_packet_mask = 0;
	memset(core->epo_hamd_packet_queue, 0,
		sizeof(core->epo_hamd_packet_queue));
	core->epo_hamd_packet_queue_head = 0;
	core->epo_hamd_packet_queue_count = 0;
	memset(core->tvpc_keyboard_rows, 0, sizeof(core->tvpc_keyboard_rows));
	return 1;
}
