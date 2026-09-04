/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 Billy Jr. and contributors
 *
 * Wiring for the game-focused SSD 2000 CPU, XaviX bus and video renderer.
 */
#include "drgqst_core.h"

#include <string.h>

static void sync_frame_audio(drgqst_core *core, uint32_t elapsed_cycles);
static void flush_video_to(drgqst_core *core, unsigned end_y);

static void sync_position_irq(drgqst_core *core, uint32_t previous_cycles,
	uint32_t elapsed_cycles)
{
	xavix_machine_state *state;
	uint32_t raster_position;
	uint32_t target_cycles;

	if (!core || !core->audio_frame_active || !core->audio_frame_cycles)
		return;
	state = &core->machine.state;
	if (!(state->video_control & 0x10U))
		return;

	/* The XaviX display timer is programmed in native 256-by-256 raster
	 * coordinates.  Convert that position to the current frame's master-clock
	 * budget and latch source 0x40 when an instruction crosses it. */
	raster_position = ((uint32_t)state->position_irq_y << 8) |
		state->position_irq_x;
	target_cycles = (uint32_t)(((uint64_t)raster_position *
		core->audio_frame_cycles) / UINT32_C(0x10000));
	if ((previous_cycles < target_cycles ||
		(target_cycles == 0 && previous_cycles == 0)) &&
		elapsed_cycles >= target_cycles)
	{
		flush_video_to(core, state->position_irq_y);
		state->video_control |= 0x40;
		state->irq_source |= 0x40;
		state->irq_asserted = 1;
	}
}
static uint8_t cpu_read(void *opaque, xavix_cpu_bus_t bus, uint32_t address)
{
	xavix_machine *machine = (xavix_machine *)opaque;
	switch (bus)
	{
	case XAVIX_CPU_BUS_LOW:
		return xavix_machine_read_low(machine, (uint16_t)address);
	case XAVIX_CPU_BUS_VECTOR:
		return xavix_machine_read_vector(machine, (uint16_t)address);
	case XAVIX_CPU_BUS_EXTERNAL:
	default:
		return xavix_machine_read_external(machine, address);
	}
}

static void cpu_write(void *opaque, xavix_cpu_bus_t bus, uint32_t address, uint8_t data)
{
	xavix_machine *machine = (xavix_machine *)opaque;
	if (bus == XAVIX_CPU_BUS_LOW)
		xavix_machine_write_low(machine, (uint16_t)address, data);
	else if (bus == XAVIX_CPU_BUS_EXTERNAL)
		xavix_machine_write_external(machine, address, data);
}

static uint8_t video_read(void *opaque, uint32_t address)
{
	return xavix_machine_read_full((const xavix_machine *)opaque, address);
}

static uint8_t audio_register_read(void *opaque, uint16_t address)
{
	drgqst_core *core = (drgqst_core *)opaque;
	return address < XAVIX_MAIN_RAM_SIZE ?
		core->machine.state.main_ram[address] : 0xff;
}

static void audio_register_write(void *opaque, uint16_t address, uint8_t data)
{
	drgqst_core *core = (drgqst_core *)opaque;
	if (address < XAVIX_MAIN_RAM_SIZE)
		core->machine.state.main_ram[address] = data;
}

static uint8_t audio_program_read(void *opaque, uint32_t address)
{
	drgqst_core *core = (drgqst_core *)opaque;
	return xavix_machine_read_full(&core->machine, address);
}

static xavix_audio_bus audio_bus(drgqst_core *core)
{
	xavix_audio_bus bus;
	bus.read_register_byte = audio_register_read;
	bus.write_register_byte = audio_register_write;
	bus.read_program_byte = audio_program_read;
	bus.context = core;
	return bus;
}

static void sync_audio_irq(drgqst_core *core)
{
	xavix_machine_state *state = &core->machine.state;
	state->sound_irq_status = core->audio.irq_status;
	if (xavix_audio_irq_pending(&core->audio))
		state->irq_source |= 0x80;
	else
		state->irq_source &= (uint8_t)~0x80;
	state->irq_asserted = state->irq_source != 0;
}

static uint8_t sound_read(void *opaque, unsigned offset)
{
	drgqst_core *core = (drgqst_core *)opaque;
	xavix_audio_bus bus = audio_bus(core);
	sync_frame_audio(core, core->machine.state.frame_cycles);
	uint8_t value = xavix_audio_read(&core->audio, &bus, offset);
	sync_audio_irq(core);
	return value;
}

static void sound_write(void *opaque, unsigned offset, uint8_t data)
{
	drgqst_core *core = (drgqst_core *)opaque;
	xavix_audio_bus bus = audio_bus(core);
	sync_frame_audio(core, core->machine.state.frame_cycles);
	xavix_audio_write(&core->audio, &bus, offset, data);
	if (offset == XAVIX_AUDIO_REGISTER_PAGE)
		core->machine.state.sound_regbase = core->audio.register_page;
	sync_audio_irq(core);
}

static uint8_t xavix_base_io1_read(void *opaque, uint8_t direction)
{
	drgqst_core *core = (drgqst_core *)opaque;
	uint8_t input = core->machine.state.input1;
	(void)direction;
	if (core->game_profile == DRGQST_CORE_XAVIX_BASE &&
		core->epo_hamd_packet_mask)
	{
		input &= (uint8_t)~0x01;
		if (core->epo_hamd_packet & core->epo_hamd_packet_mask)
			input |= 0x01;
		core->epo_hamd_packet_mask >>= 1;
	}
	return input;
}

static void xavix_base_io1_write(void *opaque, uint8_t data,
	uint8_t direction)
{
	(void)opaque;
	(void)data;
	(void)direction;
}

static uint8_t unused_adc_read(void *opaque, unsigned channel)
{
	(void)opaque;
	(void)channel;
	return 0x00;
}

static uint8_t open_anport_read(void *opaque, unsigned channel)
{
	(void)opaque;
	(void)channel;
	return 0xff;
}

static int profile_uses_parallel_nvram(enum drgqst_core_profile profile)
{
	return profile == DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM ||
		profile == DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM_SDB;
}

static int profile_uses_ssd2000_cpu(enum drgqst_core_profile profile)
{
	switch (profile)
	{
	case DRGQST_CORE_XAVIX2000_I2C_24C04:
	case DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM_SDB:
	case DRGQST_CORE_EPO_BOWL_SENSOR_24C04:
	case DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM:
	case DRGQST_CORE_XAVIX2000_PLAIN:
	case DRGQST_CORE_EPO_HAMC_SENSOR:
	case DRGQST_CORE_TOM_DPGM_SENSOR_24C08:
	case DRGQST_CORE_XAVIX2000_SENSOR_24C04:
	case DRGQST_CORE_DUELMAST_24C04:
		return 1;
	default:
		return 0;
	}
}

static uint8_t xavix_i2c_io1_read(void *opaque, uint8_t direction)
{
	drgqst_core *core = (drgqst_core *)opaque;
	uint8_t input = core->machine.state.input1 & (uint8_t)~0x08;
	(void)direction;
	if (xavix_eeprom24c08_read_sda(
		&core->machine.state.peripherals.eeprom))
		input |= 0x08;
	return input;
}

static void xavix_i2c24c16_io1_write(void *opaque, uint8_t data,
	uint8_t direction)
{
	drgqst_core *core = (drgqst_core *)opaque;
	const int scl = (direction & 0x10) ? !!(data & 0x10) : 0;
	const int sda = (direction & 0x08) ? !!(data & 0x08) : 1;
	xavix_eeprom24c16_set_lines(
		&core->machine.state.peripherals.eeprom, scl, sda);
}

static void xavix_i2c24c04_io1_write(void *opaque, uint8_t data,
	uint8_t direction)
{
	drgqst_core *core = (drgqst_core *)opaque;
	const int scl = (direction & 0x10) ? !!(data & 0x10) : 0;
	const int sda = (direction & 0x08) ? !!(data & 0x08) : 1;
	xavix_eeprom24c04_set_lines(
		&core->machine.state.peripherals.eeprom, scl, sda);
}

static void xavix_i2c24c02_io1_write(void *opaque, uint8_t data,
	uint8_t direction)
{
	drgqst_core *core = (drgqst_core *)opaque;
	const int scl = (direction & 0x10) ? !!(data & 0x10) : 0;
	const int sda = (direction & 0x08) ? !!(data & 0x08) : 1;
	xavix_eeprom24c02_set_lines(
		&core->machine.state.peripherals.eeprom, scl, sda);
}

static uint8_t early_motion_adc_read(void *opaque, unsigned channel)
{
	drgqst_core *core = (drgqst_core *)opaque;
	uint8_t motion;

	/* Zuba's ROM stores controls 51/11 and 53/13 as successive samples of
	 * ADC5 and ADC7, then subtracts each pair to measure blade motion. Host
	 * input is a signed motion value centered on 80; present it on opposite
	 * acquisition phases so a held deflection survives sub-frame sampling. */
	if (channel == 5)
		motion = core->machine.state.anport_regs[0];
	else if (channel == 7)
		motion = core->machine.state.anport_regs[1];
	else
		return 0xff;
	return (core->machine.state.adc_control & 0x40) ?
		motion : (uint8_t)(0x100U - motion);
}
static uint8_t sdb_anport_read(void *opaque, unsigned channel)
{
	drgqst_core *core = (drgqst_core *)opaque;
	const uint8_t raw = channel < 4 ?
		core->machine.state.anport_regs[channel] : 0x01;

	return (uint8_t)((raw ^ 0x7fU) + 1U);
}

static uint8_t tvpc_external_read(void *opaque, uint32_t address,
	int *handled)
{
	drgqst_core *core = (drgqst_core *)opaque;
	uint8_t select;
	unsigned row;

	if ((address & 0x7fff00) != 0x600000)
		return 0xff;
	select = (uint8_t)address;
	if (!select || (select & (uint8_t)(select - 1)))
		return 0xff;
	for (row = 0; row < 8 && select != (uint8_t)(1U << row); ++row)
		;
	if (row >= 8)
		return 0xff;
	*handled = 1;
	return core->tvpc_keyboard_rows[row];
}

static uint8_t ban_onep_io1_read(void *opaque, uint8_t direction)
{
	static const uint8_t phase_bits[4] = { 0x00, 0x02, 0x06, 0x04 };
	drgqst_core *core = (drgqst_core *)opaque;
	uint8_t input = core->machine.state.input1 & (uint8_t)~0x0e;
	(void)direction;
	if (xavix_eeprom24c08_read_sda(&core->machine.state.peripherals.eeprom))
		input |= 0x08;
	input |= phase_bits[core->ban_onep_sync_phase & 3];
	if (++core->ban_onep_sync_divider >= core->ban_onep_sync_period)
	{
		core->ban_onep_sync_divider = 0;
		core->ban_onep_sync_phase = (core->ban_onep_sync_phase + 1) & 3;
	}
	return input;
}

static uint8_t epo_hamc_io1_read(void *opaque, uint8_t direction)
{
	static const uint8_t phase_bits[4] = { 0x00, 0x02, 0x06, 0x04 };
	drgqst_core *core = (drgqst_core *)opaque;
	uint8_t input = core->machine.state.input1 & (uint8_t)~0x06;
	(void)direction;
	input |= phase_bits[core->ban_onep_sync_phase & 3];
	if (++core->ban_onep_sync_divider >= core->ban_onep_sync_period)
	{
		core->ban_onep_sync_divider = 0;
		core->ban_onep_sync_phase = (core->ban_onep_sync_phase + 1) & 3;
	}
	return input;
}

static unsigned ban_onep_punch_progress(uint8_t phase)
{
	if (!phase || phase >= 19)
		return 0;
	if (phase <= 4)
		return 0;
	if (phase <= 9)
		return (unsigned)(phase - 4) * 255U / 5U;
	if (phase <= 12)
		return 255;
	return (unsigned)(19 - phase) * 255U / 7U;
}

static void ban_onep_hand_position(const drgqst_core *core, unsigned hand,
	int *x, int *y, int *radius)
{
	if (core->ban_onep_dual_reflectors &&
		core->ban_onep_reflector_visible[hand])
	{
		*x = 3 + (core->ban_onep_reflector_x[hand] * 26 + 127) / 255;
		*y = 3 + (core->ban_onep_reflector_y[hand] * 15 + 127) / 255;
		*radius = core->ban_onep_reflector_area[hand] >= 0x30 ? 3 :
			core->ban_onep_reflector_area[hand] >= 0x18 ? 2 : 1;
		return;
	}
	const int base_x = hand ? 23 : 8;
	const int base_y = 27;
	const uint8_t phase = hand ? core->ban_onep_right_punch :
		core->ban_onep_left_punch;
	const unsigned progress = ban_onep_punch_progress(phase);
	const int target_offset = core->ban_onep_left_punch &&
		core->ban_onep_right_punch ? 6 : 2;
	int target_x = 3 + (core->ban_onep_aim_x * 26 + 127) / 255;
	const int target_y = 3 + (core->ban_onep_aim_y * 15 + 127) / 255;
	target_x += hand ? target_offset : -target_offset;
	if (target_x < 1)
		target_x = 1;
	else if (target_x > XAVIX_CU5501A_WIDTH - 2)
		target_x = XAVIX_CU5501A_WIDTH - 2;
	*x = base_x + (target_x - base_x) * (int)progress / 255;
	*y = base_y + (target_y - base_y) * (int)progress / 255;
	*radius = progress >= 180 ?
		(core->ban_onep_left_punch && core->ban_onep_right_punch ? 3 : 2) : 1;
}

static uint8_t ban_onep_adc_read(void *opaque, unsigned channel)
{
	drgqst_core *core = (drgqst_core *)opaque;
	xavix_cu5501a *sensor = &core->machine.state.peripherals.sensor;
	const unsigned pixel = sensor->pixel % XAVIX_CU5501A_PIXELS;
	const int column = (int)(pixel % XAVIX_CU5501A_WIDTH);
	const int row = (int)(pixel / XAVIX_CU5501A_WIDTH);
	uint8_t result = 0;
	unsigned hand;
	if (channel != 0)
		return 0xff;
	if (!sensor->illuminated)
		goto advance;
	for (hand = 0; hand < 2; ++hand)
	{
		int x;
		int y;
		int radius;
		ban_onep_hand_position(core, hand, &x, &y, &radius);
		if (column >= x - radius && column <= x + radius &&
			row >= y - radius && row <= y + radius)
			result = 0x40;
	}
advance:
	if (sensor->adc_phase)
		sensor->pixel = (uint16_t)((sensor->pixel + 1) % XAVIX_CU5501A_PIXELS);
	if (++sensor->adc_phase > XAVIX_CU5501A_WIDTH)
		sensor->adc_phase = 0;
	return result;
}

static uint8_t epo_hamc_adc_read(void *opaque, unsigned channel)
{
	drgqst_core *core = (drgqst_core *)opaque;
	if (channel != 0)
		return 0x00;
	/* The glove firmware performs the same 32-by-32 illuminated acquisition
	 * used by the other optical accessories.  The exact glove silhouette is
	 * unknown, so expose the host-selected reflector footprint while keeping
	 * this title's independently verified synchronization sequence. */
	return xavix_cu5501a_read_adc(&core->machine.state.peripherals.sensor);
}

static void epo_hamc_io1_write(void *opaque, uint8_t data,
	uint8_t direction)
{
	drgqst_core *core = (drgqst_core *)opaque;
	xavix_cu5501a_write_io1(&core->machine.state.peripherals.sensor,
		data, direction);
}

static void ban_onep_io1_write(void *opaque, uint8_t data, uint8_t direction)
{
	drgqst_core *core = (drgqst_core *)opaque;
	const int scl = (direction & 0x10) ? !!(data & 0x10) : 0;
	const int sda = (direction & 0x08) ? !!(data & 0x08) : 1;
	if (core->game_profile == DRGQST_CORE_TTV_CU5501_24C02 ||
		core->game_profile == DRGQST_CORE_TTV_CU5501A_24C02)
		xavix_eeprom24c02_set_lines(
			&core->machine.state.peripherals.eeprom, scl, sda);
	else if (core->game_profile == DRGQST_CORE_TOM_DPGM_SENSOR_24C08)
		xavix_eeprom24c08_set_lines(
			&core->machine.state.peripherals.eeprom, scl, sda);
	else
		xavix_eeprom24c04_set_lines(
			&core->machine.state.peripherals.eeprom, scl, sda);
	if (core->game_profile == DRGQST_CORE_TTV_CU5501A_24C02 &&
		(direction & 0x21) == 0x21 && (data & 1))
	{
		/* Star Wars first scans an unlit reference frame.  P1.5 is
		 * then pulsed before P1.0 starts the reflected-light readout.  The
		 * exposure remains latched after the lamp pulse has ended. */
		if (data & 0x20)
			core->ttv_exposure_pending = 1;
		else
		{
			xavix_cu5501a_begin_scan(
				&core->machine.state.peripherals.sensor,
				core->ttv_exposure_pending);
			core->ttv_exposure_pending = 0;
		}
	}
	else
		xavix_cu5501a_write_io1(&core->machine.state.peripherals.sensor,
			data, direction);
}

static void sync_interrupt_lines(drgqst_core *core)
{
	xavix_cpu_set_irq(&core->cpu, core->machine.state.irq_asserted);
	xavix_cpu_set_nmi(&core->cpu, core->machine.state.nmi_asserted);
}

static void configure_internal_cursor_watch(drgqst_core *core)
{
	xavix_video_sprite_watch watch;

	memset(&watch, 0, sizeof(watch));
	watch.required_sprite_mode = 0x04;
	watch.enabled = 1;
	switch ((enum drgqst_core_profile)core->game_profile)
	{
	case DRGQST_CORE_DRAGON_QUEST:
		watch.first_address = UINT32_C(0xa17d80);
		watch.last_address = UINT32_C(0xa18080);
		watch.address_stride = 0x60;
		break;
	case DRGQST_CORE_TTV_CU5501_24C02:
		watch.first_address = UINT32_C(0xa14660);
		watch.last_address = UINT32_C(0xa14780);
		watch.address_stride = 0x60;
		break;
	case DRGQST_CORE_TTV_CU5501A_24C02:
		watch.first_address = UINT32_C(0xf6eee0);
		watch.last_address = UINT32_C(0xf6f060);
		watch.address_stride = 0x80;
		watch.second_first_address = UINT32_C(0xf01fe0);
		watch.second_last_address = UINT32_C(0xf02160);
		watch.second_address_stride = 0x80;
		break;
	case DRGQST_CORE_BAN_ONEP:
	case DRGQST_CORE_BAN_OMT:
	case DRGQST_CORE_XAVIX_BASE:
	case DRGQST_CORE_XAVIX_I2C_24C16:
	case DRGQST_CORE_XAVIX2000_I2C_24C04:
	case DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM_SDB:
	case DRGQST_CORE_EPO_BOWL_SENSOR_24C04:
	case DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM:
	case DRGQST_CORE_XAVIX2000_PLAIN:
	case DRGQST_CORE_EPO_HAMC_SENSOR:
	case DRGQST_CORE_TOM_DPGM_SENSOR_24C08:
	case DRGQST_CORE_XAVIX_PLAIN:
	case DRGQST_CORE_XAVIX_43MHZ_PLAIN:
	case DRGQST_CORE_XAVIX_I2C_24C04:
	case DRGQST_CORE_XAVIX_I2C_24C02:
	case DRGQST_CORE_XAVIX2000_SENSOR_24C04:
	case DRGQST_CORE_DUELMAST_24C04:
	default:
		watch.enabled = 0;
		break;
	}
	xavix_video_set_sprite_watch(&core->video, &watch);
}

int drgqst_core_init_profile(drgqst_core *core, const uint8_t *rom,
	size_t rom_size, enum drgqst_core_profile profile)
{
	xavix_machine_hooks hooks;
	if (!core || !rom ||
		(rom_size != UINT32_C(0x800000) &&
		 rom_size != UINT32_C(0x400000) &&
		 !(rom_size == UINT32_C(0x200000) &&
			(profile == DRGQST_CORE_EPO_BOWL_SENSOR_24C04 ||
			 profile == DRGQST_CORE_XAVIX_PLAIN ||
			 profile == DRGQST_CORE_DUELMAST_24C04)) &&
		 !(rom_size == UINT32_C(0x100000) &&
			profile == DRGQST_CORE_XAVIX_PLAIN)))
		return 0;
	memset(core, 0, sizeof(*core));
	core->game_profile = (uint8_t)profile;
	xavix_machine_init(&core->machine, rom, rom_size);
	xavix_cpu_init(&core->cpu, cpu_read, cpu_write, &core->machine);
	xavix_cpu_set_decimal_arithmetic(&core->cpu,
		!profile_uses_ssd2000_cpu(profile));
	xavix_audio_init(&core->audio, XAVIX_CLOCK_HZ,
		XAVIX_AUDIO_DEFAULT_HOST_RATE, 0x80);
	memset(&hooks, 0, sizeof(hooks));
	hooks.context = core;
	hooks.read_sound = sound_read;
	hooks.write_sound = sound_write;
	if (profile == DRGQST_CORE_XAVIX_BASE)
	{
		/* Baseline XaviX boards expose plain digital input on P1.  Do not
		 * attach the 24C08/CU5501 devices used by Dragon Quest. */
		hooks.read_io1 = xavix_base_io1_read;
		hooks.write_io1 = xavix_base_io1_write;
	}
	else if (profile == DRGQST_CORE_XAVIX_I2C_24C16)
	{
		hooks.read_io1 = xavix_i2c_io1_read;
		hooks.write_io1 = xavix_i2c24c16_io1_write;
		hooks.read_external = tvpc_external_read;
	}
	else if (profile == DRGQST_CORE_XAVIX_I2C_24C04 ||
		profile == DRGQST_CORE_XAVIX_I2C_24C02)
	{
		hooks.read_io1 = xavix_i2c_io1_read;
		hooks.write_io1 = profile == DRGQST_CORE_XAVIX_I2C_24C02 ?
			xavix_i2c24c02_io1_write : xavix_i2c24c04_io1_write;
		hooks.read_adc = early_motion_adc_read;
	}
	else if (profile == DRGQST_CORE_XAVIX2000_I2C_24C04)
	{
		/* MX Dirt Rebel and IDATEN Jump use plain digital controls on IN0.
		 * P1 carries only their 24C04 I2C EEPROM; no optical sensor or
		 * synthetic camera synchronization source is attached. */
		hooks.read_io1 = xavix_i2c_io1_read;
		hooks.write_io1 = xavix_i2c24c04_io1_write;
	}
	else if (profile == DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM_SDB)
	{
		/* This board has four controller channels and parallel-backed RAM;
		 * it does not have the serial EEPROM or optical sensor. */
		hooks.read_io1 = xavix_base_io1_read;
		hooks.write_io1 = xavix_base_io1_write;
		hooks.read_anport = sdb_anport_read;
	}
	else if (profile == DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM)
	{
		/* Excite Boxing uses only digital P0 controls and battery-backed
		 * internal RAM.  Its unconnected ANPORT callbacks read high, while its
		 * unused ADC input ports read low.  Neither may inherit the synthetic
		 * optical sensor installed by the Dragon Quest profile. */
		hooks.read_io1 = xavix_base_io1_read;
		hooks.write_io1 = xavix_base_io1_write;
		hooks.read_anport = open_anport_read;
		hooks.read_adc = unused_adc_read;
	}
	else if (profile == DRGQST_CORE_XAVIX2000_PLAIN)
	{
		/* Plain XaviX 2000 boards expose digital P0/P1 input with no serial
		 * EEPROM or optical device. */
		hooks.read_io1 = xavix_base_io1_read;
		hooks.write_io1 = xavix_base_io1_write;
		hooks.read_anport = open_anport_read;
		hooks.read_adc = unused_adc_read;
	}
	else if (profile == DRGQST_CORE_XAVIX_PLAIN ||
		profile == DRGQST_CORE_XAVIX_43MHZ_PLAIN)
	{
		/* Early digital boards need their writable ANPORT registers for such
		 * controls as the rescue vehicle microphone.  A distinct profile also
		 * prevents Hamtaro wireless packets from becoming steering interrupts. */
		hooks.read_io1 = xavix_base_io1_read;
		hooks.write_io1 = xavix_base_io1_write;
		hooks.read_adc = unused_adc_read;
	}
	else if (profile == DRGQST_CORE_EPO_HAMC_SENSOR)
	{
		/* Ham Ham Dai Circus waits for P1.1/P1.2 edges around a 32-by-32 ADC
		 * acquisition loop.  Preserve only those observed synchronization
		 * requirements here; the reflector geometry remains unmodelled. */
		hooks.read_io1 = epo_hamc_io1_read;
		hooks.write_io1 = epo_hamc_io1_write;
		hooks.read_anport = open_anport_read;
		hooks.read_adc = epo_hamc_adc_read;
	}
	else if (profile == DRGQST_CORE_BAN_ONEP ||
		profile == DRGQST_CORE_BAN_OMT ||
		profile == DRGQST_CORE_TTV_CU5501_24C02 ||
		profile == DRGQST_CORE_TTV_CU5501A_24C02 ||
		profile == DRGQST_CORE_EPO_BOWL_SENSOR_24C04 ||
		profile == DRGQST_CORE_XAVIX2000_SENSOR_24C04 ||
		profile == DRGQST_CORE_DUELMAST_24C04 ||
		profile == DRGQST_CORE_TOM_DPGM_SENSOR_24C08)
	{
		/* Excite Bowling, Disney Princess, Excite Golf and Duel Masters wait for the same
		 * two-bit sync sequence before reading a 32-by-32 sensor through ADC0.
		 * Keep that wiring in explicit EEPROM-sized profiles so plain digital
		 * boards do not inherit synthetic optical signals. */
		hooks.read_io1 = ban_onep_io1_read;
		hooks.write_io1 = ban_onep_io1_write;
		if (profile == DRGQST_CORE_BAN_ONEP)
			hooks.read_adc = ban_onep_adc_read;
	}
	xavix_machine_set_hooks(&core->machine, &hooks);
	if (profile == DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM_SDB)
		memset(core->machine.state.anport_regs, 0x01,
			sizeof(core->machine.state.anport_regs));
	else if (profile == DRGQST_CORE_XAVIX_I2C_24C04 ||
		profile == DRGQST_CORE_XAVIX_I2C_24C02)
		memset(core->machine.state.anport_regs, 0x80,
			sizeof(core->machine.state.anport_regs));
	xavix_video_init(&core->video);
	configure_internal_cursor_watch(core);
	core->ban_onep_aim_x = 0x80;
	core->ban_onep_aim_y = 0x80;
	/* The acquisition loop consumes one state per read.  Holding a phase for
	 * sixteen reads made each optical pass take several host frames and slowed
	 * the game clock. */
	core->ban_onep_sync_period = 1;
	return 1;
}

int drgqst_core_init(drgqst_core *core, const uint8_t *rom, size_t rom_size)
{
	return drgqst_core_init_profile(core, rom, rom_size,
		DRGQST_CORE_DRAGON_QUEST);
}

void drgqst_core_reset(drgqst_core *core)
{
	uint8_t parallel_nvram[XAVIX_PARALLEL_NVRAM_SIZE];
	const int preserve_parallel_nvram = core && profile_uses_parallel_nvram(
		(enum drgqst_core_profile)core->game_profile);

	if (!core)
		return;
	if (preserve_parallel_nvram)
		memcpy(parallel_nvram,
			core->machine.state.main_ram + XAVIX_PARALLEL_NVRAM_BASE,
			sizeof(parallel_nvram));
	xavix_machine_reset(&core->machine);
	if (preserve_parallel_nvram)
	{
		memcpy(core->machine.state.main_ram + XAVIX_PARALLEL_NVRAM_BASE,
			parallel_nvram, sizeof(parallel_nvram));
		if (core->game_profile == DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM_SDB)
			memset(core->machine.state.anport_regs, 0x01,
				sizeof(core->machine.state.anport_regs));
	}
	xavix_cpu_reset(&core->cpu);
	xavix_audio_reset(&core->audio);
	xavix_video_reset(&core->video);
	core->video_frame_active = 0;
	configure_internal_cursor_watch(core);
	core->frame_fraction = 0;
	core->audio_frame_cycles = 0;
	core->audio_frame_position = 0;
	core->audio_frame_active = 0;
	core->ban_onep_sync_divider = 0;
	if (!core->ban_onep_sync_period)
		core->ban_onep_sync_period = 1;
	core->ban_onep_sync_phase = 0;
	core->ban_onep_buttons = 0;
	core->ban_onep_drag_active = 0;
	core->ban_onep_drag_origin_x = 0x80;
	core->ban_onep_left_punch = 0;
	core->ban_onep_right_punch = 0;
	core->ban_onep_aim_x = 0x80;
	core->ban_onep_aim_y = 0x80;
	core->ban_onep_dual_reflectors = 0;
	memset(core->ban_onep_reflector_visible, 0,
		sizeof(core->ban_onep_reflector_visible));
	core->ban_onep_bazooka_phase = 0;
	core->epo_hamd_packet = 0;
	core->epo_hamd_packet_mask = 0;
	memset(core->epo_hamd_packet_queue, 0,
		sizeof(core->epo_hamd_packet_queue));
	core->epo_hamd_packet_queue_head = 0;
	core->epo_hamd_packet_queue_count = 0;
	memset(core->tvpc_keyboard_rows, 0, sizeof(core->tvpc_keyboard_rows));
	memset(core->frame_audio, 0, sizeof(core->frame_audio));
}

static void sync_frame_audio(drgqst_core *core, uint32_t elapsed_cycles)
{
	xavix_audio_bus bus;
	uint32_t target;

	if (!core->audio_frame_active || !core->audio_frame_cycles)
		return;
	target = (uint32_t)(((uint64_t)elapsed_cycles *
		DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME) / core->audio_frame_cycles);
	if (target > DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME)
		target = DRGQST_AUDIO_FRAMES_PER_VIDEO_FRAME;
	if (target <= core->audio_frame_position)
		return;
	bus = audio_bus(core);
	xavix_audio_generate(&core->audio, &bus,
		core->frame_audio + core->audio_frame_position * 2U,
		target - core->audio_frame_position);
	core->audio_frame_position = target;
	sync_audio_irq(core);
}

static unsigned profile_cpu_clock_multiplier(const drgqst_core *core)
{
	return core && core->game_profile == DRGQST_CORE_XAVIX_43MHZ_PLAIN ? 2U : 1U;
}

int drgqst_core_step(drgqst_core *core)
{
	int cycles;
	uint32_t previous_frame_cycles;
	uint64_t previous_cpu_cycles;
	unsigned machine_cycles;
	if (!core)
		return 0;
	sync_interrupt_lines(core);
	previous_frame_cycles = core->machine.state.frame_cycles;
	previous_cpu_cycles = core->cpu.total_cycles;
	cycles = xavix_cpu_execute(&core->cpu, 1);
	if (cycles > 0)
	{
		if (profile_cpu_clock_multiplier(core) == 2U)
			machine_cycles = (unsigned)((core->cpu.total_cycles >> 1) -
				(previous_cpu_cycles >> 1));
		else
			machine_cycles = (unsigned)cycles;
		if (machine_cycles)
			xavix_machine_advance(&core->machine, machine_cycles);
		sync_position_irq(core, previous_frame_cycles,
			core->machine.state.frame_cycles);
		if (core->game_profile == DRGQST_CORE_XAVIX_BASE &&
			!(core->machine.state.ioevent_active & 0x01))
		{
			if (!core->epo_hamd_packet_mask &&
				core->epo_hamd_packet_queue_count)
			{
				core->epo_hamd_packet = core->epo_hamd_packet_queue[
					core->epo_hamd_packet_queue_head];
				core->epo_hamd_packet_queue_head =
					(core->epo_hamd_packet_queue_head + 1) & 3;
				--core->epo_hamd_packet_queue_count;
				core->epo_hamd_packet_mask = 0x80;
			}
			if (core->epo_hamd_packet_mask)
				xavix_machine_trigger_ioevent(&core->machine, 0x01);
		}
		sync_frame_audio(core, core->machine.state.frame_cycles);
		sync_interrupt_lines(core);
	}
	return cycles;
}

uint64_t drgqst_core_run_instructions(drgqst_core *core, uint32_t instructions)
{
	uint64_t cycles = 0;
	uint32_t i;
	for (i = 0; i < instructions && !core->cpu.stopped; ++i)
	{
		int step_cycles = drgqst_core_step(core);
		if (step_cycles <= 0)
			break;
		cycles += (unsigned)step_cycles;
	}
	return cycles;
}

uint64_t drgqst_core_run_cycles(drgqst_core *core, uint32_t cycles)
{
	uint64_t elapsed = 0;
	while (elapsed < cycles && !core->cpu.stopped)
	{
		int step_cycles = drgqst_core_step(core);
		if (step_cycles <= 0)
			break;
		elapsed += (unsigned)step_cycles;
	}
	return elapsed;
}

static void populate_video_inputs(drgqst_core *core, xavix_video_inputs *inputs)
{
	xavix_machine_state *state = &core->machine.state;
	memset(inputs, 0, sizeof(*inputs));
	inputs->main_ram = state->main_ram;
	inputs->main_ram_size = sizeof(state->main_ram);
	inputs->fragment_ram = state->fragment_ram;
	inputs->palette_sh = state->palette_sh;
	inputs->palette_l = state->palette_l;
	inputs->palette_entries = 256;
	inputs->segment_regs = state->segment_regs;
	inputs->tilemap_regs[0] = state->tile_regs[0];
	inputs->tilemap_regs[1] = state->tile_regs[1];
	inputs->sprite_mode = state->sprite_reg;
	inputs->arena_start = state->arena_start;
	inputs->arena_end = state->arena_end;
	inputs->arena_control = state->arena_control;
	inputs->colmix_sh = state->colmix_sh;
	inputs->colmix_l = state->colmix_l;
	inputs->colmix_control = state->colmix_control;
	inputs->read_program_byte = video_read;
	inputs->read_program_opaque = &core->machine;
}

static void begin_video_frame(drgqst_core *core)
{
	xavix_video_begin_frame(&core->video);
	core->video_segment_start_y = 0;
	core->video_frame_active = 1;
}


static void flush_video_to(drgqst_core *core, unsigned end_y)
{
	xavix_video_inputs inputs;

	if (!core || !core->video_frame_active ||
		core->video_segment_start_y > end_y)
		return;
	if (end_y > 255U)
		end_y = 255U;
	populate_video_inputs(core, &inputs);
	xavix_video_render_range(&core->video, &inputs,
		(int)core->video_segment_start_y, (int)end_y);
	core->video_segment_start_y = (uint16_t)(end_y + 1U);
}

static const uint32_t *render_frame(drgqst_core *core)
{
	if (core->video_frame_active)
	{
		flush_video_to(core, XAVIX_VIDEO_VISIBLE_Y_END);
		xavix_video_end_frame(&core->video);
		core->video_frame_active = 0;
	}
	else
	{
		xavix_video_inputs inputs;
		populate_video_inputs(core, &inputs);
		xavix_video_render(&core->video, &inputs);
	}
	return xavix_video_framebuffer(&core->video);
}

const uint32_t *drgqst_core_run_frame(drgqst_core *core)
{
	uint32_t cycles;
	if (!core)
		return NULL;
	cycles = XAVIX_CLOCK_HZ / XAVIX_FRAME_RATE;
	core->frame_fraction += XAVIX_CLOCK_HZ % XAVIX_FRAME_RATE;
	if (core->frame_fraction >= XAVIX_FRAME_RATE)
	{
		core->frame_fraction -= XAVIX_FRAME_RATE;
		++cycles;
	}
	/* run_frame defines a fresh host/video interval even if diagnostic code
	 * executed individual instructions between calls. */
	core->machine.state.frame_cycles = 0;
	core->audio_frame_cycles = cycles;
	core->audio_frame_position = 0;
	core->audio_frame_active = 1;
	begin_video_frame(core);
	drgqst_core_run_cycles(core, cycles * profile_cpu_clock_multiplier(core));
	if (core->game_profile == DRGQST_CORE_BAN_ONEP)
	{
		if (core->ban_onep_left_punch && core->ban_onep_left_punch < 19)
			++core->ban_onep_left_punch;
		else
			core->ban_onep_left_punch = 0;
		if (core->ban_onep_right_punch && core->ban_onep_right_punch < 19)
			++core->ban_onep_right_punch;
		else
			core->ban_onep_right_punch = 0;
		if (core->ban_onep_bazooka_phase &&
			core->ban_onep_bazooka_phase < 12)
			++core->ban_onep_bazooka_phase;
		else
			core->ban_onep_bazooka_phase = 0;
	}
	sync_frame_audio(core, core->machine.state.frame_cycles);
	core->audio_frame_active = 0;
	core->machine.state.frame_cycles = 0;
	if (core->machine.state.video_control & 0x20)
	{
		core->machine.state.video_control |= 0x80;
		core->machine.state.nmi_asserted = 1;
		sync_interrupt_lines(core);
	}
	return render_frame(core);
}

const uint32_t *drgqst_core_framebuffer(const drgqst_core *core)
{
	return core ? xavix_video_framebuffer(&core->video) : NULL;
}

size_t drgqst_core_generate_audio(drgqst_core *core,
	int16_t *interleaved_stereo, size_t frames)
{
	xavix_audio_bus bus;
	size_t generated;
	if (!core)
		return 0;
	bus = audio_bus(core);
	generated = xavix_audio_generate(&core->audio, &bus,
		interleaved_stereo, frames);
	sync_audio_irq(core);
	return generated;
}

const int16_t *drgqst_core_frame_audio(const drgqst_core *core)
{
	return core ? core->frame_audio : NULL;
}

int drgqst_core_feather_visible(const drgqst_core *core)
{
	return core && xavix_video_feather_visible(&core->video);
}

int drgqst_core_internal_cursor_visible(const drgqst_core *core)
{
	return core && xavix_video_feather_visible(&core->video);
}

static int signed_main_ram_word(const drgqst_core *core, unsigned address)
{
	const uint16_t value = (uint16_t)core->machine.state.main_ram[address] |
		((uint16_t)core->machine.state.main_ram[address + 1] << 8);
	return value & 0x8000U ? (int)value - 0x10000 : (int)value;
}

int drgqst_core_sword_cursor_position(const drgqst_core *core, int *x, int *y)
{
	if (!core)
		return 0;
	if (x)
		*x = 128 + signed_main_ram_word(core, 0x026c);
	if (y)
		*y = 112 - signed_main_ram_word(core, 0x0270);
	return 1;
}

void drgqst_core_set_mouse(drgqst_core *core, uint8_t x, uint8_t y,
	int broadside, int step_forward)
{
	enum xavix_sensor_mode mode = XAVIX_SENSOR_NARROW;
	if (!core)
		return;
	if (core->game_profile == DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM ||
		core->game_profile == DRGQST_CORE_XAVIX2000_PLAIN)
		return;
	if (core->game_profile == DRGQST_CORE_BAN_ONEP)
	{
		core->ban_onep_dual_reflectors = 0;
		const uint8_t buttons = (broadside ? 1 : 0) |
			(step_forward ? 2 : 0);
		const int drag_x = x > core->ban_onep_drag_origin_x ?
			x - core->ban_onep_drag_origin_x :
			core->ban_onep_drag_origin_x - x;
		if (core->ban_onep_bazooka_phase)
		{
			const unsigned step = core->ban_onep_bazooka_phase - 1U;
			const unsigned low = 0x20;
			const unsigned high = 0xdf;
			const uint8_t forward = (uint8_t)(low +
				((high - low) * step + 5U) / 11U);
			/* The original two-glove exercise distinguishes a simultaneous
			 * forward thrust from two stationary reflectors.  This path is the
			 * smallest camera-space gesture known to satisfy that classifier. */
			core->ban_onep_aim_x = (uint8_t)(high - (forward - low));
			core->ban_onep_aim_y = (uint8_t)(high - (forward - low));
			/* One reflector reaches the camera first while the second follows.
			 * Keeping that leading hand fully extended reproduces the two-object
			 * path recognized by the glove-training program. */
			if (core->ban_onep_bazooka_phase > 1)
				core->ban_onep_left_punch = 10;
		}
		else
		{
			core->ban_onep_aim_x = x;
			core->ban_onep_aim_y = y;
		}
		/* The camera image is mirrored: the reflector on its right produces the
		 * left arm on screen, and vice versa. */
		if ((buttons & 1) && !(core->ban_onep_buttons & 1))
			core->ban_onep_right_punch = 1;
		if ((buttons & 2) && !(core->ban_onep_buttons & 2))
		{
			core->ban_onep_left_punch = 1;
			core->ban_onep_drag_active = 0;
			core->ban_onep_drag_origin_x = x;
		}
		else if ((buttons & 2) && !core->ban_onep_drag_active && drag_x >= 6)
		{
			core->ban_onep_drag_active = 1;
		}
		if (!(buttons & 2))
			core->ban_onep_drag_active = 0;
		else if (core->ban_onep_drag_active)
		{
			/* Zoro's sword stages classify the direction of the reflector path.
			 * Keep the right-button reflector at the raw mouse coordinate once
			 * a deliberate drag begins; a stationary click remains a normal
			 * pre-shaped right punch for Luffy. */
			core->ban_onep_left_punch = 10;
		}
		core->ban_onep_buttons = buttons;
		if (broadside)
			core->machine.state.input0 |= 0x01;
		else
			core->machine.state.input0 &= (uint8_t)~0x01;
		if (step_forward)
			core->machine.state.input0 |= 0x02;
		else
			core->machine.state.input0 &= (uint8_t)~0x02;
		xavix_machine_set_sword_input(&core->machine, x, y,
			broadside || step_forward ? XAVIX_SENSOR_BROADSIDE :
			XAVIX_SENSOR_NARROW);
		return;
	}
	if (core->game_profile == DRGQST_CORE_BAN_OMT)
	{
		if (broadside && step_forward)
		{
			/* Turning the Drive's broad grey back toward the camera is an
			 * optical gesture, not a press of both controller buttons. */
			core->machine.state.input0 &= (uint8_t)~0x03;
			mode = XAVIX_SENSOR_STEP_FORWARD;
		}
		else
		{
			if (broadside)
				core->machine.state.input0 |= 0x01;
			else
				core->machine.state.input0 &= (uint8_t)~0x01;
			if (step_forward)
				core->machine.state.input0 |= 0x02;
			else
				core->machine.state.input0 &= (uint8_t)~0x02;
			mode = XAVIX_SENSOR_NARROW;
		}
		xavix_machine_set_sword_input(&core->machine, x, y, mode);
		return;
	}
	if (step_forward)
		mode = core->game_profile == DRGQST_CORE_TTV_CU5501_24C02 ?
			XAVIX_SENSOR_VERTICAL : XAVIX_SENSOR_STEP_FORWARD;
	else if (broadside)
		mode = XAVIX_SENSOR_BROADSIDE;
	xavix_machine_set_sword_input(&core->machine, x, y, mode);
}

void drgqst_core_set_reflectors(drgqst_core *core,
	uint8_t x0, uint8_t y0, uint8_t area0, int visible0,
	uint8_t x1, uint8_t y1, uint8_t area1, int visible1)
{
	if (!core || core->game_profile != DRGQST_CORE_BAN_ONEP)
		return;
	core->ban_onep_dual_reflectors = visible0 || visible1;
	core->ban_onep_reflector_x[0] = x0;
	core->ban_onep_reflector_y[0] = y0;
	core->ban_onep_reflector_area[0] = area0;
	core->ban_onep_reflector_visible[0] = visible0 ? 1 : 0;
	core->ban_onep_reflector_x[1] = x1;
	core->ban_onep_reflector_y[1] = y1;
	core->ban_onep_reflector_area[1] = area1;
	core->ban_onep_reflector_visible[1] = visible1 ? 1 : 0;
}

void drgqst_core_set_sdb_input(drgqst_core *core, unsigned player,
	uint8_t raw_x, uint8_t raw_y, int button_pressed)
{
	if (!core || core->game_profile !=
		DRGQST_CORE_XAVIX2000_PARALLEL_NVRAM_SDB || player >= 2)
		return;
	if (raw_x < 0x01)
		raw_x = 0x01;
	else if (raw_x > 0xfe)
		raw_x = 0xfe;
	if (raw_y < 0x01)
		raw_y = 0x01;
	else if (raw_y > 0xfe)
		raw_y = 0xfe;
	core->machine.state.anport_regs[player * 2] = raw_x;
	core->machine.state.anport_regs[player * 2 + 1] = raw_y;
	if (!player)
	{
		if (button_pressed)
			core->machine.state.input1 |= 0x01;
		else
			core->machine.state.input1 &= (uint8_t)~0x01;
	}
	else
	{
		if (button_pressed)
			core->machine.state.input0 |= 0x10;
		else
			core->machine.state.input0 &= (uint8_t)~0x10;
	}
}

void drgqst_core_set_early_motion_input(drgqst_core *core,
	uint8_t channel5, uint8_t channel7)
{
	if (!core ||
		(core->game_profile != DRGQST_CORE_XAVIX_I2C_24C04 &&
		 core->game_profile != DRGQST_CORE_XAVIX_I2C_24C02))
		return;
	core->machine.state.anport_regs[0] = channel5;
	core->machine.state.anport_regs[1] = channel7;
}

void drgqst_core_trigger_bazooka(drgqst_core *core)
{
	if (!core || core->game_profile != DRGQST_CORE_BAN_ONEP)
		return;
	core->ban_onep_bazooka_phase = 1;
	core->ban_onep_left_punch = 1;
	core->ban_onep_right_punch = 1;
}

void drgqst_core_trigger_hamd_packet(drgqst_core *core, uint8_t packet)
{
	unsigned tail;
	if (!core || core->game_profile != DRGQST_CORE_XAVIX_BASE)
		return;
	if (!core->epo_hamd_packet_mask && !core->epo_hamd_packet_queue_count)
	{
		core->epo_hamd_packet = packet;
		core->epo_hamd_packet_mask = 0x80;
		xavix_machine_trigger_ioevent(&core->machine, 0x01);
		return;
	}
	if (core->epo_hamd_packet_queue_count >=
		sizeof(core->epo_hamd_packet_queue))
		return;
	tail = (core->epo_hamd_packet_queue_head +
		core->epo_hamd_packet_queue_count) & 3;
	core->epo_hamd_packet_queue[tail] = packet;
	++core->epo_hamd_packet_queue_count;
}

void drgqst_core_set_tvpc_keyboard_row(drgqst_core *core, unsigned row,
	uint8_t keys)
{
	if (!core || core->game_profile != DRGQST_CORE_XAVIX_I2C_24C16 ||
		row >= sizeof(core->tvpc_keyboard_rows))
		return;
	core->tvpc_keyboard_rows[row] = keys;
}
