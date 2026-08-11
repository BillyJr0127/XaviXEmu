// SPDX-License-Identifier: BSD-3-Clause
// MAME-derived portions copyright-holders: smf, David Haywood
// XaviXEmu adaptation, sensor work, and modifications:
// Copyright (c) 2026 Billy Jr. and contributors
//
// Deterministic peripherals for the game-focused SSD 2000/XaviX runtime.
// Behaviour is adapted from the BSD-3-Clause MAME implementations in
// src/devices/machine/i2cmem.cpp and src/mame/tvgames/xavix_math.cpp,
// xavix_m.cpp and xavix_2000.cpp.  Original MAME copyright holders include
// smf and David Haywood.

#include "xavix_peripherals.h"

#include <limits.h>
#include <string.h>

#define XAVIX_STATE_VERSION UINT16_C(1)

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

static void eeprom_commit_page(xavix_eeprom24c08 *eeprom,
	uint16_t address_mask, uint8_t page_mask)
{
	unsigned index;

	if (!eeprom || !eeprom->page_dirty_mask)
		return;

	for (index = 0; index <= page_mask; index++)
	{
		if (eeprom->page_dirty_mask & (UINT16_C(1) << index))
			eeprom->data[(eeprom->page_base | index) & address_mask] = eeprom->page[index];
	}

	eeprom->page_dirty_mask = 0;
	eeprom->dirty = 1;
	eeprom->write_generation++;
}

static void eeprom_start(xavix_eeprom24c08 *eeprom)
{
	eeprom->protocol_state = XAVIX_I2C_RECEIVE_CONTROL;
	eeprom->pending_state = XAVIX_I2C_IDLE;
	eeprom->bit_count = 0;
	eeprom->acknowledge_stage = 0;
	eeprom->shift = 0;
	eeprom->selected = 0;
	eeprom->device_sda = 1;
}

static void eeprom_stop(xavix_eeprom24c08 *eeprom, uint16_t address_mask,
	uint8_t page_mask)
{
	eeprom_commit_page(eeprom, address_mask, page_mask);
	eeprom->protocol_state = XAVIX_I2C_IDLE;
	eeprom->pending_state = XAVIX_I2C_IDLE;
	eeprom->bit_count = 0;
	eeprom->acknowledge_stage = 0;
	eeprom->shift = 0;
	eeprom->selected = 0;
	eeprom->device_sda = 1;
}

static void eeprom_prepare_send(xavix_eeprom24c08 *eeprom,
	uint16_t address_mask)
{
	eeprom->shift = eeprom->data[eeprom->address & address_mask];
	eeprom->address = (eeprom->address + 1) & address_mask;
	eeprom->bit_count = 0;
	eeprom->acknowledge_stage = 0;
	eeprom->device_sda = (eeprom->shift >> 7) & 1;
}

static void eeprom_receive_byte(xavix_eeprom24c08 *eeprom,
	uint16_t address_mask, uint8_t control_mask, uint8_t page_mask)
{
	unsigned position;

	eeprom->selected = 1;
	switch (eeprom->protocol_state)
	{
	case XAVIX_I2C_RECEIVE_CONTROL:
		eeprom->control = eeprom->shift;
		if ((eeprom->control & control_mask) != 0xa0)
		{
			eeprom->selected = 0;
			eeprom->pending_state = XAVIX_I2C_IGNORE;
		}
		else if (eeprom->control & 1)
		{
			eeprom->pending_state = XAVIX_I2C_SEND_DATA;
		}
		else
		{
			eeprom->pending_state = XAVIX_I2C_RECEIVE_ADDRESS;
		}
		break;

	case XAVIX_I2C_RECEIVE_ADDRESS:
		eeprom->address = ((((uint16_t)eeprom->control & UINT16_C(0x06)) << 7) | eeprom->shift) &
			address_mask;
		eeprom->page_base = eeprom->address & (uint16_t)~(uint16_t)page_mask;
		eeprom->page_position = eeprom->address & page_mask;
		eeprom->page_dirty_mask = 0;
		eeprom->pending_state = XAVIX_I2C_RECEIVE_DATA;
		break;

	case XAVIX_I2C_RECEIVE_DATA:
		if (eeprom->write_protect)
		{
			eeprom->selected = 0;
			eeprom->pending_state = XAVIX_I2C_IGNORE;
			break;
		}

		position = eeprom->page_position & page_mask;
		eeprom->page[position] = eeprom->shift;
		eeprom->page_dirty_mask |= UINT16_C(1) << position;
		eeprom->page_position = (position + 1) & page_mask;
		eeprom->pending_state = XAVIX_I2C_RECEIVE_DATA;
		break;

	default:
		eeprom->selected = 0;
		eeprom->pending_state = XAVIX_I2C_IGNORE;
		break;
	}

	eeprom->acknowledge_stage = 1;
}

static void eeprom_rising_edge(xavix_eeprom24c08 *eeprom,
	uint16_t address_mask, uint8_t control_mask, uint8_t page_mask)
{
	if (eeprom->protocol_state == XAVIX_I2C_SEND_DATA)
	{
		if (!eeprom->acknowledge_stage && eeprom->bit_count < 8)
		{
			eeprom->bit_count++;
		}
		else if (eeprom->acknowledge_stage == 1)
		{
			eeprom->pending_state = eeprom->master_sda ? XAVIX_I2C_IGNORE : XAVIX_I2C_SEND_DATA;
			eeprom->acknowledge_stage = 2;
		}
		return;
	}

	if (eeprom->protocol_state == XAVIX_I2C_RECEIVE_CONTROL ||
		eeprom->protocol_state == XAVIX_I2C_RECEIVE_ADDRESS ||
		eeprom->protocol_state == XAVIX_I2C_RECEIVE_DATA)
	{
		if (!eeprom->acknowledge_stage && eeprom->bit_count < 8)
		{
			eeprom->shift = (uint8_t)((eeprom->shift << 1) | eeprom->master_sda);
			eeprom->bit_count++;
			if (eeprom->bit_count == 8)
				eeprom_receive_byte(eeprom, address_mask, control_mask, page_mask);
		}
	}
}

static void eeprom_falling_edge(xavix_eeprom24c08 *eeprom,
	uint16_t address_mask)
{
	if (eeprom->protocol_state == XAVIX_I2C_SEND_DATA)
	{
		if (!eeprom->acknowledge_stage)
		{
			if (eeprom->bit_count < 8)
			{
				eeprom->shift <<= 1;
				eeprom->device_sda = (eeprom->shift >> 7) & 1;
			}
			else
			{
				eeprom->device_sda = 1;
				eeprom->acknowledge_stage = 1;
			}
		}
		else if (eeprom->acknowledge_stage == 2)
		{
			eeprom->protocol_state = eeprom->pending_state;
			eeprom->bit_count = 0;
			eeprom->acknowledge_stage = 0;
			if (eeprom->protocol_state == XAVIX_I2C_SEND_DATA)
				eeprom_prepare_send(eeprom, address_mask);
			else
				eeprom->device_sda = 1;
		}
		return;
	}

	if (eeprom->protocol_state == XAVIX_I2C_RECEIVE_CONTROL ||
		eeprom->protocol_state == XAVIX_I2C_RECEIVE_ADDRESS ||
		eeprom->protocol_state == XAVIX_I2C_RECEIVE_DATA)
	{
		if (eeprom->acknowledge_stage == 1)
		{
			eeprom->device_sda = eeprom->selected ? 0 : 1;
			eeprom->acknowledge_stage = 2;
		}
		else if (eeprom->acknowledge_stage == 2)
		{
			eeprom->device_sda = 1;
			eeprom->protocol_state = eeprom->pending_state;
			eeprom->bit_count = 0;
			eeprom->acknowledge_stage = 0;
			eeprom->shift = 0;
			if (eeprom->protocol_state == XAVIX_I2C_SEND_DATA)
				eeprom_prepare_send(eeprom, address_mask);
		}
	}
}

void xavix_eeprom24c08_init(xavix_eeprom24c08 *eeprom, const uint8_t *initial, size_t initial_size)
{
	if (!eeprom)
		return;

	memset(eeprom, 0, sizeof(*eeprom));
	memset(eeprom->data, 0xff, sizeof(eeprom->data));
	if (initial && initial_size)
	{
		if (initial_size > sizeof(eeprom->data))
			initial_size = sizeof(eeprom->data);
		memcpy(eeprom->data, initial, initial_size);
	}
	xavix_eeprom24c08_reset_bus(eeprom);
}

void xavix_eeprom24c08_reset_bus(xavix_eeprom24c08 *eeprom)
{
	if (!eeprom)
		return;

	eeprom->page_dirty_mask = 0;
	eeprom->address = 0;
	eeprom->page_base = 0;
	eeprom->page_position = 0;
	eeprom->scl = 1;
	eeprom->master_sda = 1;
	eeprom->device_sda = 1;
	eeprom->protocol_state = XAVIX_I2C_IDLE;
	eeprom->pending_state = XAVIX_I2C_IDLE;
	eeprom->bit_count = 0;
	eeprom->acknowledge_stage = 0;
	eeprom->shift = 0;
	eeprom->control = 0;
	eeprom->selected = 0;
}

int xavix_eeprom24c08_load_image(xavix_eeprom24c08 *eeprom, const uint8_t *data, size_t size)
{
	if (!eeprom || !data || size != XAVIX_EEPROM24C08_SIZE)
		return 0;

	memcpy(eeprom->data, data, XAVIX_EEPROM24C08_SIZE);
	eeprom->dirty = 0;
	eeprom->write_generation = 0;
	xavix_eeprom24c08_reset_bus(eeprom);
	return 1;
}

void xavix_eeprom24c08_copy_image(const xavix_eeprom24c08 *eeprom, uint8_t output[XAVIX_EEPROM24C08_SIZE])
{
	if (eeprom && output)
		memcpy(output, eeprom->data, XAVIX_EEPROM24C08_SIZE);
}

void xavix_eeprom24c08_set_write_protect(xavix_eeprom24c08 *eeprom, int enabled)
{
	if (eeprom)
		eeprom->write_protect = enabled ? 1 : 0;
}

static void eeprom_set_lines(xavix_eeprom24c08 *eeprom, int scl,
	int master_sda, uint16_t address_mask, uint8_t control_mask,
	uint8_t page_mask)
{
	uint8_t old_scl;
	uint8_t old_sda;
	uint8_t new_scl;
	uint8_t new_sda;

	if (!eeprom)
		return;

	old_scl = eeprom->scl;
	old_sda = eeprom->master_sda;
	new_scl = scl ? 1 : 0;
	new_sda = master_sda ? 1 : 0;
	eeprom->scl = new_scl;
	eeprom->master_sda = new_sda;

	if (old_scl && new_scl && old_sda != new_sda && eeprom->device_sda)
	{
		if (new_sda)
			eeprom_stop(eeprom, address_mask, page_mask);
		else
			eeprom_start(eeprom);
		return;
	}

	if (!old_scl && new_scl)
		eeprom_rising_edge(eeprom, address_mask, control_mask, page_mask);
	else if (old_scl && !new_scl)
		eeprom_falling_edge(eeprom, address_mask);
}

void xavix_eeprom24c08_set_lines(xavix_eeprom24c08 *eeprom, int scl,
	int master_sda)
{
	eeprom_set_lines(eeprom, scl, master_sda,
		XAVIX_EEPROM24C08_SIZE - 1, 0xf8,
		XAVIX_EEPROM24C08_PAGE_SIZE - 1);
}

void xavix_eeprom24c04_set_lines(xavix_eeprom24c08 *eeprom, int scl,
	int master_sda)
{
	/* A 24C04 uses bit 1 of the control byte as address bit 8.  Bits 2
	 * and 3 still select the physical device, so A4/A6 must not ACK. */
	eeprom_set_lines(eeprom, scl, master_sda, UINT16_C(0x01ff), 0xfc,
		XAVIX_EEPROM24C08_PAGE_SIZE - 1);
}

void xavix_eeprom24c02_set_lines(xavix_eeprom24c08 *eeprom, int scl,
	int master_sda)
{
	/* The 24C02 has a 256-byte address space and an 8-byte write page.
	 * Its A0-A2 device-select pins are tied low on these game boards. */
	eeprom_set_lines(eeprom, scl, master_sda, UINT16_C(0x00ff), 0xfe,
		UINT8_C(0x07));
}

int xavix_eeprom24c08_read_sda(const xavix_eeprom24c08 *eeprom)
{
	return eeprom ? (eeprom->master_sda & eeprom->device_sda) : 1;
}

int xavix_eeprom24c08_is_dirty(const xavix_eeprom24c08 *eeprom)
{
	return eeprom ? !!eeprom->dirty : 0;
}

void xavix_eeprom24c08_clear_dirty(xavix_eeprom24c08 *eeprom)
{
	if (eeprom)
		eeprom->dirty = 0;
}

static int sensor_coordinate(uint8_t value, int extent, int invert)
{
	const int input = invert ? 0xff - value : value;
	return (input * (extent - 1) + 0x7f) / 0xff;
}

void xavix_cu5501a_init(xavix_cu5501a *sensor)
{
	if (!sensor)
		return;

	memset(sensor, 0, sizeof(*sensor));
	sensor->host_x = 0x80;
	sensor->host_y = 0x80;
	sensor->scan_x = sensor->host_x;
	sensor->scan_y = sensor->host_y;
}

void xavix_cu5501a_reset(xavix_cu5501a *sensor)
{
	if (!sensor)
		return;

	sensor->pixel = 0;
	sensor->adc_phase = 0;
	sensor->sync_phase = 0;
	sensor->illuminated = 0;
	sensor->scan_x = sensor->host_x;
	sensor->scan_y = sensor->host_y;
	sensor->scan_mode = sensor->host_mode & 3;
}

void xavix_cu5501a_set_input(xavix_cu5501a *sensor, uint8_t x, uint8_t y, uint8_t mode)
{
	if (!sensor)
		return;

	sensor->host_x = x;
	sensor->host_y = y;
	sensor->host_mode = mode & 3;
}

void xavix_cu5501a_begin_scan(xavix_cu5501a *sensor, int illuminated)
{
	if (!sensor)
		return;

	sensor->pixel = 0;
	sensor->adc_phase = 0;
	sensor->sync_phase = 0;
	sensor->illuminated = !!illuminated;
	sensor->scan_x = sensor->host_x;
	sensor->scan_y = sensor->host_y;
	sensor->scan_mode = sensor->host_mode & 3;
}

void xavix_cu5501a_write_io1(xavix_cu5501a *sensor, uint8_t data, uint8_t direction)
{
	if (!sensor)
		return;

	if ((direction & 0x21) == 0x21 && (data & 1))
		xavix_cu5501a_begin_scan(sensor, (data >> 5) & 1);
}

uint8_t xavix_cu5501a_read_io1(xavix_cu5501a *sensor, uint8_t input_bits)
{
	uint8_t result;

	if (!sensor)
		return input_bits;

	result = input_bits & (uint8_t)~0x06;
	result |= sensor->sync_phase ? 0x02 : 0x04;
	sensor->sync_phase ^= 1;
	return result;
}

uint8_t xavix_cu5501a_pixel_at(const xavix_cu5501a *sensor, unsigned column, unsigned row)
{
	int sword_x;
	int sword_y;
	int image_x;
	int image_y;
	int radius;

	if (!sensor || column >= XAVIX_CU5501A_WIDTH || row >= XAVIX_CU5501A_HEIGHT || !sensor->illuminated)
		return 0;

	sword_x = sensor_coordinate(sensor->scan_x, XAVIX_CU5501A_WIDTH, 1);
	sword_y = sensor_coordinate(sensor->scan_y, XAVIX_CU5501A_HEIGHT, 0);
	radius = (sensor->scan_mode & XAVIX_SENSOR_STEP_FORWARD) ? 4 :
		(sensor->scan_mode & XAVIX_SENSOR_BROADSIDE) ? 2 : 1;
	image_x = sword_x;
	image_y = sword_y;
	if (radius > 1)
	{
		if (image_x < radius)
			image_x = radius;
		else if (image_x > XAVIX_CU5501A_WIDTH - radius - 1)
			image_x = XAVIX_CU5501A_WIDTH - radius - 1;
		if (image_y < radius)
			image_y = radius;
		else if (image_y > XAVIX_CU5501A_HEIGHT - radius - 1)
			image_y = XAVIX_CU5501A_HEIGHT - radius - 1;
	}

	return ((int)column >= image_x - radius && (int)column <= image_x + radius &&
		(int)row >= image_y - radius && (int)row <= image_y + radius) ? 0x40 : 0;
}

uint8_t xavix_cu5501a_read_adc(xavix_cu5501a *sensor)
{
	unsigned pixel;
	uint8_t result;

	if (!sensor)
		return 0;

	pixel = sensor->pixel % XAVIX_CU5501A_PIXELS;
	result = xavix_cu5501a_pixel_at(sensor, pixel % XAVIX_CU5501A_WIDTH, pixel / XAVIX_CU5501A_WIDTH);
	if (sensor->adc_phase)
		sensor->pixel = (uint16_t)((sensor->pixel + 1) % XAVIX_CU5501A_PIXELS);
	sensor->adc_phase++;
	if (sensor->adc_phase > XAVIX_CU5501A_WIDTH)
		sensor->adc_phase = 0;
	return result;
}

void xavix_timer_init(xavix_timer *timer, uint32_t master_clock_hz)
{
	if (!timer)
		return;

	memset(timer, 0, sizeof(*timer));
	timer->master_clock_hz = master_clock_hz ? master_clock_hz : XAVIX_MASTER_CLOCK_NTSC;
}

void xavix_timer_reset(xavix_timer *timer)
{
	uint32_t clock;

	if (!timer)
		return;

	clock = timer->master_clock_hz ? timer->master_clock_hz : XAVIX_MASTER_CLOCK_NTSC;
	memset(timer, 0, sizeof(*timer));
	timer->master_clock_hz = clock;
}

uint8_t xavix_timer_read(const xavix_timer *timer, unsigned offset)
{
	if (!timer)
		return 0;

	switch (offset)
	{
	case 0: return timer->control;
	case 1: return timer->base_value;
	case 2: return timer->frequency;
	case 3: return timer->current_value;
	default: return 0;
	}
}

void xavix_timer_write(xavix_timer *timer, unsigned offset, uint8_t data)
{
	if (!timer)
		return;

	switch (offset)
	{
	case 0:
		timer->control = data;
		if (data & 0x80)
			timer->irq_pending = 0;
		timer->prescale_cycles = 0;
		if (data & 1)
		{
			timer->current_value = timer->base_value;
			timer->running = 1;
		}
		else
		{
			timer->running = 0;
		}
		break;

	case 1:
		timer->base_value = data;
		break;

	case 2:
		timer->frequency = data & 0x0f;
		/* Keep the cycle accumulator valid if software retunes a live timer. */
		timer->prescale_cycles = 0;
		break;

	default:
		break;
	}
}

int xavix_timer_advance(xavix_timer *timer, uint64_t master_cycles)
{
	uint64_t divider;
	uint64_t first_tick;
	uint64_t ticks;
	uint64_t remainder;
	uint64_t until_underflow;

	if (!timer || !timer->running || !master_cycles)
		return 0;

	divider = UINT64_C(1) << (timer->frequency + 1);
	first_tick = divider - timer->prescale_cycles;
	if (master_cycles < first_tick)
	{
		timer->prescale_cycles += master_cycles;
		return 0;
	}

	master_cycles -= first_tick;
	ticks = UINT64_C(1) + master_cycles / divider;
	remainder = master_cycles % divider;
	until_underflow = (uint64_t)timer->current_value + 1;
	if (ticks >= until_underflow)
	{
		timer->current_value = 0xff;
		timer->prescale_cycles = 0;
		timer->running = 0;
		if (timer->control & 0x40)
		{
			timer->control |= 0x80;
			timer->irq_pending = 1;
			return 1;
		}
		return 0;
	}

	timer->current_value = (uint8_t)(timer->current_value - ticks);
	timer->prescale_cycles = remainder;
	return 0;
}

int xavix_timer_irq_pending(const xavix_timer *timer)
{
	return timer ? !!timer->irq_pending : 0;
}

static void math_calculate(xavix_math *math, int barrel)
{
	int32_t multiplicand;
	int32_t multiplier;
	uint16_t result;
	uint16_t old_result;

	multiplicand = (math->signed_multiplicand && (math->multiplicand & 0x80)) ?
		(int32_t)math->multiplicand - 0x100 : math->multiplicand;
	if (barrel)
	{
		multiplier = 1 << (math->multiplier & 7);
	}
	else
	{
		multiplier = (math->signed_multiplier && (math->multiplier & 0x80)) ?
			(int32_t)math->multiplier - 0x100 : math->multiplier;
	}

	result = (uint16_t)(multiplicand * multiplier);
	if (math->add_mode)
	{
		old_result = (uint16_t)((uint16_t)math->result[0] | ((uint16_t)math->result[1] << 8));
		result = (uint16_t)(result + old_result);
	}
	math->result[0] = (uint8_t)(result & 0xff);
	math->result[1] = (uint8_t)(result >> 8);
}

void xavix_math_reset(xavix_math *math)
{
	if (math)
		memset(math, 0, sizeof(*math));
}

uint8_t xavix_math_read(const xavix_math *math, unsigned offset)
{
	if (!math)
		return 0;

	switch (offset)
	{
	case 0:
		return (math->signed_multiplicand ? 0x80 : 0) | (math->multiplier & 0x40) | (math->multiplier & 7);
	case 1:
		return (math->multiplier & 0x40) ? (math->result[1] ^ math->result[0]) :
			(math->multiplier & 0x08) ? math->result[1] : math->result[0];
	case 2:
		return (math->add_mode ? 0x80 : 0) | (math->signed_multiplicand ? 0x02 : 0) |
			(math->signed_multiplier ? 0x01 : 0);
	case 3:
		return math->multiplicand;
	case 4:
		return math->multiplier;
	case 5:
		return math->result[0];
	case 6:
		return math->result[1];
	default:
		return 0;
	}
}

void xavix_math_write(xavix_math *math, unsigned offset, uint8_t data)
{
	if (!math)
		return;

	switch (offset)
	{
	case 0:
		math->add_mode = 0;
		math->signed_multiplicand = !!(data & 0x80);
		math->signed_multiplier = 0;
		math->multiplier = data;
		math_calculate(math, 1);
		break;
	case 1:
		math->multiplicand = data;
		break;
	case 2:
		math->add_mode = !!(data & 0x80);
		math->signed_multiplicand = !!(data & 0x02);
		math->signed_multiplier = !!(data & 0x01);
		break;
	case 3:
		math->multiplicand = data;
		break;
	case 4:
		math->multiplier = data;
		math_calculate(math, 0);
		break;
	case 5:
		math->result[0] = data;
		break;
	case 6:
		math->result[1] = data;
		break;
	default:
		break;
	}
}

void xavix_dma_reset(xavix_dma *dma)
{
	if (dma)
		memset(dma, 0, sizeof(*dma));
}

uint8_t xavix_dma_read(const xavix_dma *dma, unsigned offset)
{
	if (!dma)
		return 0;

	switch (offset)
	{
	case 0: return 0;
	case 1: return dma->source[0];
	case 2: return dma->source[1];
	case 3: return dma->source[2];
	case 4: return dma->destination[0];
	case 5: return dma->destination[1];
	case 6: return dma->length[0];
	case 7: return dma->length[1];
	default: return 0;
	}
}

size_t xavix_dma_write(xavix_dma *dma, unsigned offset, uint8_t data, const xavix_dma_bus *bus)
{
	uint32_t source;
	uint16_t destination;
	uint16_t length;
	uint32_t index;

	if (!dma)
		return 0;

	switch (offset)
	{
	case 0:
		dma->last_control = data;
		if (data & 0x80)
			dma->irq_pending = 0;
		if (!(data & 1) || !bus || !bus->read || !bus->write)
			return 0;

		source = (uint32_t)dma->source[0] | ((uint32_t)dma->source[1] << 8) |
			((uint32_t)dma->source[2] << 16);
		destination = (uint16_t)((uint16_t)dma->destination[0] | ((uint16_t)dma->destination[1] << 8));
		length = (uint16_t)((uint16_t)dma->length[0] | ((uint16_t)dma->length[1] << 8));
		for (index = 0; index < length; index++)
		{
			const uint32_t read_address = (source + index) & UINT32_C(0x00ffffff);
			const uint16_t write_address = (uint16_t)(destination + index);
			bus->write(bus->opaque, write_address, bus->read(bus->opaque, read_address));
		}
		dma->length[0] = 0;
		dma->length[1] = 0;
		if (data & 0x40)
			dma->irq_pending = 1;
		return length;

	case 1: dma->source[0] = data; break;
	case 2: dma->source[1] = data; break;
	case 3: dma->source[2] = data; break;
	case 4: dma->destination[0] = data; break;
	case 5: dma->destination[1] = data; break;
	case 6: dma->length[0] = data; break;
	case 7: dma->length[1] = data; break;
	default: break;
	}
	return 0;
}

int xavix_dma_irq_pending(const xavix_dma *dma)
{
	return dma ? !!dma->irq_pending : 0;
}

void xavix_peripherals_init(
	xavix_peripherals *peripherals,
	const uint8_t *initial_eeprom,
	size_t initial_eeprom_size,
	uint32_t master_clock_hz)
{
	if (!peripherals)
		return;

	memset(peripherals, 0, sizeof(*peripherals));
	xavix_eeprom24c08_init(&peripherals->eeprom, initial_eeprom, initial_eeprom_size);
	xavix_cu5501a_init(&peripherals->sensor);
	xavix_timer_init(&peripherals->timer, master_clock_hz);
	xavix_math_reset(&peripherals->math);
	xavix_dma_reset(&peripherals->dma);
}

void xavix_peripherals_reset(xavix_peripherals *peripherals)
{
	if (!peripherals)
		return;

	xavix_eeprom24c08_reset_bus(&peripherals->eeprom);
	xavix_cu5501a_reset(&peripherals->sensor);
	xavix_timer_reset(&peripherals->timer);
	xavix_math_reset(&peripherals->math);
	xavix_dma_reset(&peripherals->dma);
}

static void writer_bytes(state_writer *writer, const uint8_t *data, size_t size)
{
	if (!writer->valid || size > SIZE_MAX - writer->position)
	{
		writer->valid = 0;
		return;
	}
	if (writer->output)
	{
		if (writer->position + size > writer->capacity)
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
	bytes[0] = (uint8_t)(value & 0xff);
	bytes[1] = (uint8_t)(value >> 8);
	writer_bytes(writer, bytes, sizeof(bytes));
}

static void writer_u32(state_writer *writer, uint32_t value)
{
	uint8_t bytes[4];
	bytes[0] = value & 0xff;
	bytes[1] = (value >> 8) & 0xff;
	bytes[2] = (value >> 16) & 0xff;
	bytes[3] = (uint8_t)(value >> 24);
	writer_bytes(writer, bytes, sizeof(bytes));
}

static void writer_u64(state_writer *writer, uint64_t value)
{
	uint8_t bytes[8];
	unsigned index;
	for (index = 0; index < sizeof(bytes); index++)
		bytes[index] = (uint8_t)(value >> (index * 8));
	writer_bytes(writer, bytes, sizeof(bytes));
}

static void serialize_body(state_writer *writer, const xavix_peripherals *peripherals, uint32_t total_size)
{
	static const uint8_t magic[4] = { 'X', 'V', 'P', '1' };
	const xavix_eeprom24c08 *eeprom = &peripherals->eeprom;
	const xavix_cu5501a *sensor = &peripherals->sensor;
	const xavix_timer *timer = &peripherals->timer;
	const xavix_math *math = &peripherals->math;
	const xavix_dma *dma = &peripherals->dma;

	writer_bytes(writer, magic, sizeof(magic));
	writer_u16(writer, XAVIX_STATE_VERSION);
	writer_u16(writer, 0);
	writer_u32(writer, total_size);

	writer_bytes(writer, eeprom->data, sizeof(eeprom->data));
	writer_bytes(writer, eeprom->page, sizeof(eeprom->page));
	writer_u16(writer, eeprom->page_dirty_mask);
	writer_u16(writer, eeprom->address);
	writer_u16(writer, eeprom->page_base);
	writer_u32(writer, eeprom->write_generation);
	writer_u8(writer, eeprom->page_position);
	writer_u8(writer, eeprom->scl);
	writer_u8(writer, eeprom->master_sda);
	writer_u8(writer, eeprom->device_sda);
	writer_u8(writer, eeprom->protocol_state);
	writer_u8(writer, eeprom->pending_state);
	writer_u8(writer, eeprom->bit_count);
	writer_u8(writer, eeprom->acknowledge_stage);
	writer_u8(writer, eeprom->shift);
	writer_u8(writer, eeprom->control);
	writer_u8(writer, eeprom->selected);
	writer_u8(writer, eeprom->write_protect);
	writer_u8(writer, eeprom->dirty);

	writer_u16(writer, sensor->pixel);
	writer_u8(writer, sensor->adc_phase);
	writer_u8(writer, sensor->sync_phase);
	writer_u8(writer, sensor->illuminated);
	writer_u8(writer, sensor->host_x);
	writer_u8(writer, sensor->host_y);
	writer_u8(writer, sensor->host_mode);
	writer_u8(writer, sensor->scan_x);
	writer_u8(writer, sensor->scan_y);
	writer_u8(writer, sensor->scan_mode);

	writer_u64(writer, timer->prescale_cycles);
	writer_u32(writer, timer->master_clock_hz);
	writer_u8(writer, timer->control);
	writer_u8(writer, timer->base_value);
	writer_u8(writer, timer->frequency);
	writer_u8(writer, timer->current_value);
	writer_u8(writer, timer->running);
	writer_u8(writer, timer->irq_pending);

	writer_u8(writer, math->multiplier);
	writer_u8(writer, math->multiplicand);
	writer_bytes(writer, math->result, sizeof(math->result));
	writer_u8(writer, math->add_mode);
	writer_u8(writer, math->signed_multiplicand);
	writer_u8(writer, math->signed_multiplier);

	writer_bytes(writer, dma->source, sizeof(dma->source));
	writer_bytes(writer, dma->destination, sizeof(dma->destination));
	writer_bytes(writer, dma->length, sizeof(dma->length));
	writer_u8(writer, dma->last_control);
	writer_u8(writer, dma->irq_pending);
}

size_t xavix_peripherals_serialized_size(void)
{
	xavix_peripherals empty;
	state_writer writer;

	memset(&empty, 0, sizeof(empty));
	writer.output = NULL;
	writer.capacity = 0;
	writer.position = 0;
	writer.valid = 1;
	serialize_body(&writer, &empty, 0);
	return writer.valid ? writer.position : 0;
}

int xavix_peripherals_serialize(
	const xavix_peripherals *peripherals,
	uint8_t *output,
	size_t output_size,
	size_t *written)
{
	state_writer writer;
	const size_t required = xavix_peripherals_serialized_size();

	if (written)
		*written = required;
	if (!peripherals || !output || !required || required > UINT32_MAX || output_size < required)
		return 0;

	writer.output = output;
	writer.capacity = output_size;
	writer.position = 0;
	writer.valid = 1;
	serialize_body(&writer, peripherals, (uint32_t)required);
	return writer.valid && writer.position == required;
}

static void reader_bytes(state_reader *reader, uint8_t *output, size_t size)
{
	if (!reader->valid || reader->position > reader->size || size > reader->size - reader->position)
	{
		reader->valid = 0;
		return;
	}
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
	uint8_t bytes[2] = { 0, 0 };
	reader_bytes(reader, bytes, sizeof(bytes));
	return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t reader_u32(state_reader *reader)
{
	uint8_t bytes[4] = { 0, 0, 0, 0 };
	reader_bytes(reader, bytes, sizeof(bytes));
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
		((uint32_t)bytes[3] << 24);
}

static uint64_t reader_u64(state_reader *reader)
{
	uint8_t bytes[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	uint64_t value = 0;
	unsigned index;

	reader_bytes(reader, bytes, sizeof(bytes));
	for (index = 0; index < sizeof(bytes); index++)
		value |= (uint64_t)bytes[index] << (index * 8);
	return value;
}

static int valid_boolean(uint8_t value)
{
	return value <= 1;
}

int xavix_peripherals_deserialize(
	xavix_peripherals *peripherals,
	const uint8_t *input,
	size_t input_size)
{
	static const uint8_t expected_magic[4] = { 'X', 'V', 'P', '1' };
	uint8_t magic[4];
	xavix_peripherals restored;
	state_reader reader;
	uint16_t version;
	uint16_t reserved;
	uint32_t encoded_size;
	const size_t expected_size = xavix_peripherals_serialized_size();

	if (!peripherals || !input || !expected_size || input_size != expected_size)
		return 0;

	memset(&restored, 0, sizeof(restored));
	reader.input = input;
	reader.size = input_size;
	reader.position = 0;
	reader.valid = 1;
	reader_bytes(&reader, magic, sizeof(magic));
	version = reader_u16(&reader);
	reserved = reader_u16(&reader);
	encoded_size = reader_u32(&reader);
	if (!reader.valid || memcmp(magic, expected_magic, sizeof(magic)) || version != XAVIX_STATE_VERSION ||
		reserved || encoded_size != expected_size)
		return 0;

	reader_bytes(&reader, restored.eeprom.data, sizeof(restored.eeprom.data));
	reader_bytes(&reader, restored.eeprom.page, sizeof(restored.eeprom.page));
	restored.eeprom.page_dirty_mask = reader_u16(&reader);
	restored.eeprom.address = reader_u16(&reader);
	restored.eeprom.page_base = reader_u16(&reader);
	restored.eeprom.write_generation = reader_u32(&reader);
	restored.eeprom.page_position = reader_u8(&reader);
	restored.eeprom.scl = reader_u8(&reader);
	restored.eeprom.master_sda = reader_u8(&reader);
	restored.eeprom.device_sda = reader_u8(&reader);
	restored.eeprom.protocol_state = reader_u8(&reader);
	restored.eeprom.pending_state = reader_u8(&reader);
	restored.eeprom.bit_count = reader_u8(&reader);
	restored.eeprom.acknowledge_stage = reader_u8(&reader);
	restored.eeprom.shift = reader_u8(&reader);
	restored.eeprom.control = reader_u8(&reader);
	restored.eeprom.selected = reader_u8(&reader);
	restored.eeprom.write_protect = reader_u8(&reader);
	restored.eeprom.dirty = reader_u8(&reader);

	restored.sensor.pixel = reader_u16(&reader);
	restored.sensor.adc_phase = reader_u8(&reader);
	restored.sensor.sync_phase = reader_u8(&reader);
	restored.sensor.illuminated = reader_u8(&reader);
	restored.sensor.host_x = reader_u8(&reader);
	restored.sensor.host_y = reader_u8(&reader);
	restored.sensor.host_mode = reader_u8(&reader);
	restored.sensor.scan_x = reader_u8(&reader);
	restored.sensor.scan_y = reader_u8(&reader);
	restored.sensor.scan_mode = reader_u8(&reader);

	restored.timer.prescale_cycles = reader_u64(&reader);
	restored.timer.master_clock_hz = reader_u32(&reader);
	restored.timer.control = reader_u8(&reader);
	restored.timer.base_value = reader_u8(&reader);
	restored.timer.frequency = reader_u8(&reader);
	restored.timer.current_value = reader_u8(&reader);
	restored.timer.running = reader_u8(&reader);
	restored.timer.irq_pending = reader_u8(&reader);

	restored.math.multiplier = reader_u8(&reader);
	restored.math.multiplicand = reader_u8(&reader);
	reader_bytes(&reader, restored.math.result, sizeof(restored.math.result));
	restored.math.add_mode = reader_u8(&reader);
	restored.math.signed_multiplicand = reader_u8(&reader);
	restored.math.signed_multiplier = reader_u8(&reader);

	reader_bytes(&reader, restored.dma.source, sizeof(restored.dma.source));
	reader_bytes(&reader, restored.dma.destination, sizeof(restored.dma.destination));
	reader_bytes(&reader, restored.dma.length, sizeof(restored.dma.length));
	restored.dma.last_control = reader_u8(&reader);
	restored.dma.irq_pending = reader_u8(&reader);

	if (!reader.valid || reader.position != reader.size ||
		restored.eeprom.address >= XAVIX_EEPROM24C08_SIZE ||
		restored.eeprom.page_base >= XAVIX_EEPROM24C08_SIZE ||
		(restored.eeprom.page_base & UINT16_C(0x0007)) ||
		restored.eeprom.page_position >= XAVIX_EEPROM24C08_PAGE_SIZE ||
		restored.eeprom.protocol_state > XAVIX_I2C_IGNORE ||
		restored.eeprom.pending_state > XAVIX_I2C_IGNORE ||
		restored.eeprom.bit_count > 8 || restored.eeprom.acknowledge_stage > 2 ||
		!valid_boolean(restored.eeprom.scl) || !valid_boolean(restored.eeprom.master_sda) ||
		!valid_boolean(restored.eeprom.device_sda) || !valid_boolean(restored.eeprom.selected) ||
		!valid_boolean(restored.eeprom.write_protect) || !valid_boolean(restored.eeprom.dirty) ||
		restored.sensor.pixel >= XAVIX_CU5501A_PIXELS || restored.sensor.adc_phase > XAVIX_CU5501A_WIDTH ||
		!valid_boolean(restored.sensor.sync_phase) || !valid_boolean(restored.sensor.illuminated) ||
		restored.sensor.host_mode > 3 || restored.sensor.scan_mode > 3 ||
		!restored.timer.master_clock_hz || restored.timer.frequency > 0x0f ||
		restored.timer.prescale_cycles >= (UINT64_C(1) << (restored.timer.frequency + 1)) ||
		!valid_boolean(restored.timer.running) || !valid_boolean(restored.timer.irq_pending) ||
		!valid_boolean(restored.math.add_mode) || !valid_boolean(restored.math.signed_multiplicand) ||
		!valid_boolean(restored.math.signed_multiplier) || !valid_boolean(restored.dma.irq_pending))
		return 0;

	*peripherals = restored;
	return 1;
}
