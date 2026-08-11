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

#ifndef DRGQST_PLAYER_XAVIX_PERIPHERALS_H
#define DRGQST_PLAYER_XAVIX_PERIPHERALS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	XAVIX_EEPROM24C08_SIZE = 1024,
	XAVIX_EEPROM24C16_SIZE = 2048,
	XAVIX_EEPROM24C08_PAGE_SIZE = 16,
	XAVIX_CU5501A_WIDTH = 32,
	XAVIX_CU5501A_HEIGHT = 31,
	XAVIX_CU5501A_PIXELS = XAVIX_CU5501A_WIDTH * XAVIX_CU5501A_HEIGHT
};

#define XAVIX_MASTER_CLOCK_NTSC UINT32_C(21477272)

enum xavix_sensor_mode
{
	XAVIX_SENSOR_NARROW = 0,       /* ordinary sword edge: 3 by 3 pixels */
	XAVIX_SENSOR_BROADSIDE = 1,    /* held left mouse button: 5 by 5 */
	XAVIX_SENSOR_STEP_FORWARD = 2, /* held right mouse button: 9 by 9 */
	XAVIX_SENSOR_VERTICAL = 4,     /* broad face held upright: 5 by 13 */
	XAVIX_SENSOR_DIAGONAL_DOWN = 5,
	XAVIX_SENSOR_HORIZONTAL = 6,
	XAVIX_SENSOR_DIAGONAL_UP = 7,
	XAVIX_SENSOR_NONE = 8,         /* no reflective target in view */
	XAVIX_SENSOR_POINT = 9         /* one-pixel moving edge reflection */
};

enum xavix_i2c_protocol_state
{
	XAVIX_I2C_IDLE = 0,
	XAVIX_I2C_RECEIVE_CONTROL,
	XAVIX_I2C_RECEIVE_ADDRESS,
	XAVIX_I2C_RECEIVE_DATA,
	XAVIX_I2C_SEND_DATA,
	XAVIX_I2C_IGNORE
};

/*
 * These structures intentionally contain no pointers.  A machine may keep
 * them directly in its state object, and xavix_peripherals_serialize() emits
 * a canonical, versioned little-endian representation rather than relying on
 * compiler padding or host endianness.
 *
 * Treat fields after the public data arrays as implementation state.  Use the
 * helpers below rather than changing them directly.
 */
typedef struct xavix_eeprom24c08
{
	/* The shared implementation reserves the largest supported device.  The
	 * 24C02/04/08 helpers mask addresses to their physical capacity. */
	uint8_t data[XAVIX_EEPROM24C16_SIZE];
	uint8_t page[XAVIX_EEPROM24C08_PAGE_SIZE];
	uint16_t page_dirty_mask;
	uint16_t address;
	uint16_t page_base;
	uint32_t write_generation;
	uint8_t page_position;
	uint8_t scl;
	uint8_t master_sda;
	uint8_t device_sda;
	uint8_t protocol_state;
	uint8_t pending_state;
	uint8_t bit_count;
	uint8_t acknowledge_stage;
	uint8_t shift;
	uint8_t control;
	uint8_t selected;
	uint8_t write_protect;
	uint8_t dirty;
} xavix_eeprom24c08;

typedef struct xavix_cu5501a
{
	uint16_t pixel;
	uint8_t adc_phase;
	uint8_t sync_phase;
	uint8_t illuminated;
	uint8_t host_x;
	uint8_t host_y;
	uint8_t host_mode;
	uint8_t scan_x;
	uint8_t scan_y;
	uint8_t scan_mode;
} xavix_cu5501a;

typedef struct xavix_timer
{
	uint64_t prescale_cycles;
	uint32_t master_clock_hz;
	uint8_t control;
	uint8_t base_value;
	uint8_t frequency;
	uint8_t current_value;
	uint8_t running;
	uint8_t irq_pending;
} xavix_timer;

typedef struct xavix_math
{
	uint8_t multiplier;
	uint8_t multiplicand;
	uint8_t result[2];
	uint8_t add_mode;
	uint8_t signed_multiplicand;
	uint8_t signed_multiplier;
} xavix_math;

typedef struct xavix_dma
{
	uint8_t source[3];
	uint8_t destination[2];
	uint8_t length[2];
	uint8_t last_control;
	uint8_t irq_pending;
} xavix_dma;

typedef struct xavix_peripherals
{
	xavix_eeprom24c08 eeprom;
	xavix_cu5501a sensor;
	xavix_timer timer;
	xavix_math math;
	xavix_dma dma;
} xavix_peripherals;

typedef uint8_t (*xavix_dma_read_callback)(void *opaque, uint32_t address);
typedef void (*xavix_dma_write_callback)(void *opaque, uint16_t address, uint8_t data);

typedef struct xavix_dma_bus
{
	xavix_dma_read_callback read;
	xavix_dma_write_callback write;
	void *opaque;
} xavix_dma_bus;

/* 24C08: in-memory storage plus an open-drain, edge-driven I2C interface. */
void xavix_eeprom24c08_init(xavix_eeprom24c08 *eeprom, const uint8_t *initial, size_t initial_size);
void xavix_eeprom24c08_reset_bus(xavix_eeprom24c08 *eeprom);
int xavix_eeprom24c08_load_image(xavix_eeprom24c08 *eeprom, const uint8_t *data, size_t size);
void xavix_eeprom24c08_copy_image(const xavix_eeprom24c08 *eeprom, uint8_t output[XAVIX_EEPROM24C08_SIZE]);
int xavix_eeprom_load_image(xavix_eeprom24c08 *eeprom, const uint8_t *data, size_t size);
void xavix_eeprom_copy_image(const xavix_eeprom24c08 *eeprom, uint8_t *output, size_t size);
void xavix_eeprom24c08_set_write_protect(xavix_eeprom24c08 *eeprom, int enabled);
void xavix_eeprom24c02_set_lines(xavix_eeprom24c08 *eeprom, int scl, int master_sda);
void xavix_eeprom24c08_set_lines(xavix_eeprom24c08 *eeprom, int scl, int master_sda);
void xavix_eeprom24c04_set_lines(xavix_eeprom24c08 *eeprom, int scl, int master_sda);
void xavix_eeprom24c16_set_lines(xavix_eeprom24c08 *eeprom, int scl, int master_sda);
int xavix_eeprom24c08_read_sda(const xavix_eeprom24c08 *eeprom);
int xavix_eeprom24c08_is_dirty(const xavix_eeprom24c08 *eeprom);
void xavix_eeprom24c08_clear_dirty(xavix_eeprom24c08 *eeprom);

/* CU5501A virtual optical sensor and the acquisition framing used by drgqst.
 * mode is a bit mask; STEP_FORWARD takes precedence if both bits are held. */
void xavix_cu5501a_init(xavix_cu5501a *sensor);
void xavix_cu5501a_reset(xavix_cu5501a *sensor);
void xavix_cu5501a_set_input(xavix_cu5501a *sensor, uint8_t x, uint8_t y, uint8_t mode);
void xavix_cu5501a_begin_scan(xavix_cu5501a *sensor, int illuminated);
void xavix_cu5501a_write_io1(xavix_cu5501a *sensor, uint8_t data, uint8_t direction);
uint8_t xavix_cu5501a_read_io1(xavix_cu5501a *sensor, uint8_t input_bits);
uint8_t xavix_cu5501a_read_adc(xavix_cu5501a *sensor);
uint8_t xavix_cu5501a_pixel_at(const xavix_cu5501a *sensor, unsigned column, unsigned row);

/* Universal timer registers correspond to 0x7c00-0x7c03. */
void xavix_timer_init(xavix_timer *timer, uint32_t master_clock_hz);
void xavix_timer_reset(xavix_timer *timer);
uint8_t xavix_timer_read(const xavix_timer *timer, unsigned offset);
void xavix_timer_write(xavix_timer *timer, unsigned offset, uint8_t data);
int xavix_timer_advance(xavix_timer *timer, uint64_t master_cycles);
int xavix_timer_irq_pending(const xavix_timer *timer);

/* Math unit registers correspond to 0x7ff0-0x7ff6. */
void xavix_math_reset(xavix_math *math);
uint8_t xavix_math_read(const xavix_math *math, unsigned offset);
void xavix_math_write(xavix_math *math, unsigned offset, uint8_t data);

/* General ROM DMA registers correspond to 0x7980-0x7987. */
void xavix_dma_reset(xavix_dma *dma);
uint8_t xavix_dma_read(const xavix_dma *dma, unsigned offset);
size_t xavix_dma_write(xavix_dma *dma, unsigned offset, uint8_t data, const xavix_dma_bus *bus);
int xavix_dma_irq_pending(const xavix_dma *dma);

/* Aggregate lifecycle and portable save-state support. */
void xavix_peripherals_init(
	xavix_peripherals *peripherals,
	const uint8_t *initial_eeprom,
	size_t initial_eeprom_size,
	uint32_t master_clock_hz);
void xavix_peripherals_reset(xavix_peripherals *peripherals);
size_t xavix_peripherals_serialized_size(void);
int xavix_peripherals_serialize(
	const xavix_peripherals *peripherals,
	uint8_t *output,
	size_t output_size,
	size_t *written);
int xavix_peripherals_deserialize(
	xavix_peripherals *peripherals,
	const uint8_t *input,
	size_t input_size);

#ifdef __cplusplus
}
#endif

#endif
