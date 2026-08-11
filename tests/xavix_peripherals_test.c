// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "xavix_peripherals.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

static void i2c_start(xavix_eeprom24c08 *eeprom)
{
	xavix_eeprom24c08_set_lines(eeprom, 1, 1);
	xavix_eeprom24c08_set_lines(eeprom, 1, 0);
}

static void i2c_stop(xavix_eeprom24c08 *eeprom)
{
	xavix_eeprom24c08_set_lines(eeprom, 0, 0);
	xavix_eeprom24c08_set_lines(eeprom, 1, 0);
	xavix_eeprom24c08_set_lines(eeprom, 1, 1);
}

static int i2c_write_byte(xavix_eeprom24c08 *eeprom, uint8_t data)
{
	unsigned bit;
	int acknowledged;

	for (bit = 0; bit < 8; bit++)
	{
		const int value = !!(data & (uint8_t)(0x80U >> bit));
		xavix_eeprom24c08_set_lines(eeprom, 0, value);
		xavix_eeprom24c08_set_lines(eeprom, 1, value);
	}
	xavix_eeprom24c08_set_lines(eeprom, 0, 1);
	xavix_eeprom24c08_set_lines(eeprom, 1, 1);
	acknowledged = !xavix_eeprom24c08_read_sda(eeprom);
	xavix_eeprom24c08_set_lines(eeprom, 0, 1);
	return acknowledged;
}

static uint8_t i2c_read_byte(xavix_eeprom24c08 *eeprom, int acknowledge)
{
	uint8_t result = 0;
	unsigned bit;

	for (bit = 0; bit < 8; bit++)
	{
		xavix_eeprom24c08_set_lines(eeprom, 1, 1);
		result = (uint8_t)((result << 1) | xavix_eeprom24c08_read_sda(eeprom));
		xavix_eeprom24c08_set_lines(eeprom, 0, 1);
	}
	xavix_eeprom24c08_set_lines(eeprom, 0, acknowledge ? 0 : 1);
	xavix_eeprom24c08_set_lines(eeprom, 1, acknowledge ? 0 : 1);
	xavix_eeprom24c08_set_lines(eeprom, 0, acknowledge ? 0 : 1);
	xavix_eeprom24c08_set_lines(eeprom, 0, 1);
	return result;
}

static uint8_t i2c_control(uint16_t address, int read)
{
	return (uint8_t)(0xa0 | ((address >> 7) & 0x06) | (read ? 1 : 0));
}

static void i2c04_start(xavix_eeprom24c08 *eeprom)
{
	xavix_eeprom24c04_set_lines(eeprom, 1, 1);
	xavix_eeprom24c04_set_lines(eeprom, 1, 0);
}

static void i2c04_stop(xavix_eeprom24c08 *eeprom)
{
	xavix_eeprom24c04_set_lines(eeprom, 0, 0);
	xavix_eeprom24c04_set_lines(eeprom, 1, 0);
	xavix_eeprom24c04_set_lines(eeprom, 1, 1);
}

static int i2c04_write_byte(xavix_eeprom24c08 *eeprom, uint8_t data)
{
	unsigned bit;
	int acknowledged;
	for (bit = 0; bit < 8; ++bit)
	{
		const int value = !!(data & (uint8_t)(0x80U >> bit));
		xavix_eeprom24c04_set_lines(eeprom, 0, value);
		xavix_eeprom24c04_set_lines(eeprom, 1, value);
	}
	xavix_eeprom24c04_set_lines(eeprom, 0, 1);
	xavix_eeprom24c04_set_lines(eeprom, 1, 1);
	acknowledged = !xavix_eeprom24c08_read_sda(eeprom);
	xavix_eeprom24c04_set_lines(eeprom, 0, 1);
	return acknowledged;
}

static uint8_t i2c04_read_byte(xavix_eeprom24c08 *eeprom, int acknowledge)
{
	uint8_t result = 0;
	unsigned bit;
	for (bit = 0; bit < 8; ++bit)
	{
		xavix_eeprom24c04_set_lines(eeprom, 1, 1);
		result = (uint8_t)((result << 1) |
			xavix_eeprom24c08_read_sda(eeprom));
		xavix_eeprom24c04_set_lines(eeprom, 0, 1);
	}
	xavix_eeprom24c04_set_lines(eeprom, 0, acknowledge ? 0 : 1);
	xavix_eeprom24c04_set_lines(eeprom, 1, acknowledge ? 0 : 1);
	xavix_eeprom24c04_set_lines(eeprom, 0, acknowledge ? 0 : 1);
	xavix_eeprom24c04_set_lines(eeprom, 0, 1);
	return result;
}

static uint8_t i2c04_control(uint16_t address, int read)
{
	return (uint8_t)(0xa0 | ((address >> 7) & 0x02) |
		(read ? 1 : 0));
}

static void i2c02_start(xavix_eeprom24c08 *eeprom)
{
	xavix_eeprom24c02_set_lines(eeprom, 1, 1);
	xavix_eeprom24c02_set_lines(eeprom, 1, 0);
}

static void i2c02_stop(xavix_eeprom24c08 *eeprom)
{
	xavix_eeprom24c02_set_lines(eeprom, 0, 0);
	xavix_eeprom24c02_set_lines(eeprom, 1, 0);
	xavix_eeprom24c02_set_lines(eeprom, 1, 1);
}

static int i2c02_write_byte(xavix_eeprom24c08 *eeprom, uint8_t data)
{
	unsigned bit;
	int acknowledged;
	for (bit = 0; bit < 8; ++bit)
	{
		const int value = !!(data & (uint8_t)(0x80U >> bit));
		xavix_eeprom24c02_set_lines(eeprom, 0, value);
		xavix_eeprom24c02_set_lines(eeprom, 1, value);
	}
	xavix_eeprom24c02_set_lines(eeprom, 0, 1);
	xavix_eeprom24c02_set_lines(eeprom, 1, 1);
	acknowledged = !xavix_eeprom24c08_read_sda(eeprom);
	xavix_eeprom24c02_set_lines(eeprom, 0, 1);
	return acknowledged;
}

static uint8_t i2c02_read_byte(xavix_eeprom24c08 *eeprom, int acknowledge)
{
	uint8_t result = 0;
	unsigned bit;
	for (bit = 0; bit < 8; ++bit)
	{
		xavix_eeprom24c02_set_lines(eeprom, 1, 1);
		result = (uint8_t)((result << 1) |
			xavix_eeprom24c08_read_sda(eeprom));
		xavix_eeprom24c02_set_lines(eeprom, 0, 1);
	}
	xavix_eeprom24c02_set_lines(eeprom, 0, acknowledge ? 0 : 1);
	xavix_eeprom24c02_set_lines(eeprom, 1, acknowledge ? 0 : 1);
	xavix_eeprom24c02_set_lines(eeprom, 0, acknowledge ? 0 : 1);
	xavix_eeprom24c02_set_lines(eeprom, 0, 1);
	return result;
}

static void i2c02_set_address(xavix_eeprom24c08 *eeprom, uint8_t address)
{
	i2c02_start(eeprom);
	CHECK(i2c02_write_byte(eeprom, 0xa0));
	CHECK(i2c02_write_byte(eeprom, address));
}

static void test_eeprom24c02(void)
{
	xavix_eeprom24c08 eeprom;
	uint8_t image[XAVIX_EEPROM24C08_SIZE];
	unsigned index;
	memset(image, 0x5a, sizeof(image));
	xavix_eeprom24c08_init(&eeprom, image, sizeof(image));

	i2c02_start(&eeprom);
	CHECK(!i2c02_write_byte(&eeprom, 0xa2));
	i2c02_stop(&eeprom);

	i2c02_set_address(&eeprom, 0xf6);
	for (index = 0; index < 10; ++index)
		CHECK(i2c02_write_byte(&eeprom, (uint8_t)(0x80 + index)));
	i2c02_stop(&eeprom);
	for (index = 0; index < 6; ++index)
		CHECK(eeprom.data[0xf0 + index] == (uint8_t)(0x82 + index));
	CHECK(eeprom.data[0xf6] == 0x88 && eeprom.data[0xf7] == 0x89);
	CHECK(eeprom.data[0x1f0] == 0x5a);

	eeprom.data[0xff] = 0x12;
	eeprom.data[0x00] = 0x34;
	i2c02_set_address(&eeprom, 0xff);
	i2c02_start(&eeprom);
	CHECK(i2c02_write_byte(&eeprom, 0xa1));
	CHECK(i2c02_read_byte(&eeprom, 1) == 0x12);
	CHECK(i2c02_read_byte(&eeprom, 0) == 0x34);
	i2c02_stop(&eeprom);
}

static void i2c04_set_address(xavix_eeprom24c08 *eeprom,
	uint16_t address)
{
	i2c04_start(eeprom);
	CHECK(i2c04_write_byte(eeprom, i2c04_control(address, 0)));
	CHECK(i2c04_write_byte(eeprom, (uint8_t)address));
}

static void test_eeprom24c04(void)
{
	xavix_eeprom24c08 eeprom;
	uint8_t image[XAVIX_EEPROM24C08_SIZE];
	memset(image, 0x5a, sizeof(image));
	xavix_eeprom24c08_init(&eeprom, image, sizeof(image));

	/* A4 selects a different physical device and must not be acknowledged. */
	i2c04_start(&eeprom);
	CHECK(!i2c04_write_byte(&eeprom, 0xa4));
	i2c04_stop(&eeprom);
	CHECK(!eeprom.write_generation);

	i2c04_set_address(&eeprom, 0x134);
	CHECK(i2c04_write_byte(&eeprom, 0x77));
	i2c04_stop(&eeprom);
	CHECK(eeprom.data[0x134] == 0x77);
	CHECK(eeprom.data[0x334] == 0x5a);

	eeprom.data[0x1ff] = 0x12;
	eeprom.data[0x000] = 0x34;
	i2c04_set_address(&eeprom, 0x1ff);
	i2c04_start(&eeprom);
	CHECK(i2c04_write_byte(&eeprom, i2c04_control(0x1ff, 1)));
	CHECK(i2c04_read_byte(&eeprom, 1) == 0x12);
	CHECK(i2c04_read_byte(&eeprom, 0) == 0x34);
	i2c04_stop(&eeprom);
}

static void i2c_set_address(xavix_eeprom24c08 *eeprom, uint16_t address)
{
	i2c_start(eeprom);
	CHECK(i2c_write_byte(eeprom, i2c_control(address, 0)));
	CHECK(i2c_write_byte(eeprom, (uint8_t)address));
}

static void test_eeprom(void)
{
	xavix_eeprom24c08 eeprom;
	uint8_t image[XAVIX_EEPROM24C08_SIZE];
	uint8_t copied[XAVIX_EEPROM24C08_SIZE];
	const uint16_t address = 0x234;
	uint32_t generation;

	memset(image, 0x5a, sizeof(image));
	xavix_eeprom24c08_init(&eeprom, image, sizeof(image));
	CHECK(eeprom.data[address] == 0x5a);
	CHECK(!xavix_eeprom24c08_is_dirty(&eeprom));

	i2c_set_address(&eeprom, address);
	CHECK(i2c_write_byte(&eeprom, 0x39));
	CHECK(eeprom.data[address] == 0x5a);
	i2c_stop(&eeprom);
	CHECK(eeprom.data[address] == 0x39);
	CHECK(xavix_eeprom24c08_is_dirty(&eeprom));
	CHECK(eeprom.write_generation == 1);
	xavix_eeprom24c08_clear_dirty(&eeprom);
	CHECK(!xavix_eeprom24c08_is_dirty(&eeprom));

	i2c_set_address(&eeprom, address);
	i2c_start(&eeprom);
	CHECK(i2c_write_byte(&eeprom, i2c_control(address, 1)));
	CHECK(i2c_read_byte(&eeprom, 0) == 0x39);
	i2c_stop(&eeprom);

	i2c_set_address(&eeprom, 0x11e);
	CHECK(i2c_write_byte(&eeprom, 0xaa));
	CHECK(i2c_write_byte(&eeprom, 0xbb));
	CHECK(i2c_write_byte(&eeprom, 0xcc));
	CHECK(i2c_write_byte(&eeprom, 0xdd));
	i2c_stop(&eeprom);
	CHECK(eeprom.data[0x11e] == 0xaa);
	CHECK(eeprom.data[0x11f] == 0xbb);
	CHECK(eeprom.data[0x110] == 0xcc);
	CHECK(eeprom.data[0x111] == 0xdd);

	eeprom.data[0x3ff] = 0x12;
	eeprom.data[0x000] = 0x34;
	i2c_set_address(&eeprom, 0x3ff);
	i2c_start(&eeprom);
	CHECK(i2c_write_byte(&eeprom, i2c_control(0x3ff, 1)));
	CHECK(i2c_read_byte(&eeprom, 1) == 0x12);
	CHECK(i2c_read_byte(&eeprom, 0) == 0x34);
	i2c_stop(&eeprom);

	i2c_start(&eeprom);
	CHECK(!i2c_write_byte(&eeprom, 0xb0));
	i2c_stop(&eeprom);

	generation = eeprom.write_generation;
	xavix_eeprom24c08_set_write_protect(&eeprom, 1);
	i2c_set_address(&eeprom, 0x020);
	CHECK(!i2c_write_byte(&eeprom, 0x91));
	i2c_stop(&eeprom);
	CHECK(eeprom.data[0x020] == 0x5a);
	CHECK(eeprom.write_generation == generation);
	xavix_eeprom24c08_set_write_protect(&eeprom, 0);

	xavix_eeprom24c08_copy_image(&eeprom, copied);
	CHECK(!memcmp(copied, eeprom.data, sizeof(copied)));
	CHECK(!xavix_eeprom24c08_load_image(&eeprom, image, sizeof(image) - 1));
	CHECK(xavix_eeprom24c08_load_image(&eeprom, image, sizeof(image)));
	CHECK(!memcmp(eeprom.data, image, sizeof(image)));
}

static unsigned sensor_scan_count(xavix_cu5501a *sensor, uint8_t mode, uint8_t start_data)
{
	unsigned row;
	unsigned column;
	unsigned count = 0;

	xavix_cu5501a_set_input(sensor, sensor->host_x, sensor->host_y, mode);
	xavix_cu5501a_write_io1(sensor, start_data, 0x21);
	for (row = 0; row < XAVIX_CU5501A_HEIGHT; row++)
	{
		(void)xavix_cu5501a_read_adc(sensor);
		for (column = 0; column < XAVIX_CU5501A_WIDTH; column++)
		{
			if (xavix_cu5501a_read_adc(sensor) == 0x40)
				count++;
		}
	}
	return count;
}

static void test_sensor(void)
{
	xavix_cu5501a sensor;

	xavix_cu5501a_init(&sensor);
	CHECK(xavix_cu5501a_read_io1(&sensor, 0xff) == 0xfd);
	CHECK(xavix_cu5501a_read_io1(&sensor, 0xff) == 0xfb);

	xavix_cu5501a_set_input(&sensor, 0x80, 0x80, XAVIX_SENSOR_NARROW);
	CHECK(sensor_scan_count(&sensor, XAVIX_SENSOR_NARROW, 0x21) == 9);
	CHECK(sensor_scan_count(&sensor, XAVIX_SENSOR_BROADSIDE, 0x21) == 25);
	CHECK(sensor_scan_count(&sensor, XAVIX_SENSOR_STEP_FORWARD, 0x21) == 81);
	CHECK(sensor_scan_count(&sensor, XAVIX_SENSOR_BROADSIDE | XAVIX_SENSOR_STEP_FORWARD, 0x21) == 81);
	CHECK(sensor_scan_count(&sensor, XAVIX_SENSOR_BROADSIDE, 0x01) == 0);

	xavix_cu5501a_set_input(&sensor, 0xff, 0xff, XAVIX_SENSOR_BROADSIDE);
	CHECK(sensor_scan_count(&sensor, XAVIX_SENSOR_BROADSIDE, 0x21) == 25);
	xavix_cu5501a_set_input(&sensor, 0x00, 0x00, XAVIX_SENSOR_STEP_FORWARD);
	CHECK(sensor_scan_count(&sensor, XAVIX_SENSOR_STEP_FORWARD, 0x21) == 81);

	xavix_cu5501a_set_input(&sensor, 0xff, 0x80, XAVIX_SENSOR_NARROW);
	xavix_cu5501a_write_io1(&sensor, 0x21, 0x21);
	CHECK(xavix_cu5501a_pixel_at(&sensor, 0, 15) == 0x40);
	CHECK(xavix_cu5501a_pixel_at(&sensor, 31, 15) == 0);
	xavix_cu5501a_set_input(&sensor, 0x00, 0x80, XAVIX_SENSOR_NARROW);
	xavix_cu5501a_write_io1(&sensor, 0x21, 0x21);
	CHECK(xavix_cu5501a_pixel_at(&sensor, 31, 15) == 0x40);
	CHECK(xavix_cu5501a_pixel_at(&sensor, 0, 15) == 0);
}

static void test_timer(void)
{
	xavix_timer timer;

	xavix_timer_init(&timer, 0);
	CHECK(timer.master_clock_hz == XAVIX_MASTER_CLOCK_NTSC);
	xavix_timer_write(&timer, 1, 1);
	xavix_timer_write(&timer, 2, 0);
	xavix_timer_write(&timer, 0, 0x41);
	CHECK(xavix_timer_read(&timer, 3) == 1);
	CHECK(!xavix_timer_advance(&timer, 1));
	CHECK(xavix_timer_read(&timer, 3) == 1);
	CHECK(!xavix_timer_advance(&timer, 1));
	CHECK(xavix_timer_read(&timer, 3) == 0);
	CHECK(xavix_timer_advance(&timer, 2));
	CHECK(xavix_timer_read(&timer, 3) == 0xff);
	CHECK(xavix_timer_read(&timer, 0) & 0x80);
	CHECK(xavix_timer_irq_pending(&timer));
	xavix_timer_write(&timer, 0, 0x80);
	CHECK(!xavix_timer_irq_pending(&timer));
	CHECK(xavix_timer_read(&timer, 0) == 0x80);

	xavix_timer_write(&timer, 1, 2);
	xavix_timer_write(&timer, 2, 2);
	xavix_timer_write(&timer, 0, 1);
	CHECK(!xavix_timer_advance(&timer, 23));
	CHECK(timer.current_value == 0 && timer.prescale_cycles == 7);
	CHECK(!xavix_timer_advance(&timer, 1));
	CHECK(timer.current_value == 0xff && !timer.running);

	xavix_timer_write(&timer, 1, 4);
	xavix_timer_write(&timer, 2, 15);
	xavix_timer_write(&timer, 0, 1);
	CHECK(!xavix_timer_advance(&timer, 50000));
	xavix_timer_write(&timer, 2, 0);
	CHECK(timer.prescale_cycles == 0);
	CHECK(!xavix_timer_advance(&timer, 2));
	CHECK(timer.current_value == 3);
}

static void test_math(void)
{
	xavix_math math;

	xavix_math_reset(&math);
	xavix_math_write(&math, 2, 0x00);
	xavix_math_write(&math, 3, 5);
	xavix_math_write(&math, 4, 6);
	CHECK(xavix_math_read(&math, 5) == 30);
	CHECK(xavix_math_read(&math, 6) == 0);

	xavix_math_write(&math, 2, 0x03);
	xavix_math_write(&math, 3, 0xfe);
	xavix_math_write(&math, 4, 0xfd);
	CHECK(xavix_math_read(&math, 5) == 6);
	CHECK(xavix_math_read(&math, 6) == 0);

	xavix_math_write(&math, 5, 100);
	xavix_math_write(&math, 6, 0);
	xavix_math_write(&math, 2, 0x80);
	xavix_math_write(&math, 3, 3);
	xavix_math_write(&math, 4, 4);
	CHECK(xavix_math_read(&math, 5) == 112);

	xavix_math_write(&math, 1, 3);
	xavix_math_write(&math, 0, 2);
	CHECK(xavix_math_read(&math, 5) == 12);
	CHECK(xavix_math_read(&math, 0) == 2);
	CHECK(xavix_math_read(&math, 1) == 12);

	xavix_math_write(&math, 1, 0xff);
	xavix_math_write(&math, 0, 0x87);
	CHECK(xavix_math_read(&math, 5) == 0x80);
	CHECK(xavix_math_read(&math, 6) == 0xff);
}

typedef struct test_bus
{
	uint8_t source[256];
	uint8_t destination[256];
	unsigned reads;
	unsigned writes;
} test_bus;

static uint8_t dma_test_read(void *opaque, uint32_t address)
{
	test_bus *bus = (test_bus *)opaque;
	bus->reads++;
	return bus->source[address & 0xff];
}

static void dma_test_write(void *opaque, uint16_t address, uint8_t data)
{
	test_bus *bus = (test_bus *)opaque;
	bus->writes++;
	bus->destination[address & 0xff] = data;
}

static void test_dma(void)
{
	xavix_dma dma;
	test_bus memory;
	xavix_dma_bus bus;

	memset(&memory, 0, sizeof(memory));
	memory.source[0x10] = 0x81;
	memory.source[0x11] = 0x42;
	memory.source[0x12] = 0x24;
	memory.source[0x13] = 0x18;
	bus.read = dma_test_read;
	bus.write = dma_test_write;
	bus.opaque = &memory;
	xavix_dma_reset(&dma);
	xavix_dma_write(&dma, 1, 0x10, NULL);
	xavix_dma_write(&dma, 2, 0x00, NULL);
	xavix_dma_write(&dma, 3, 0x00, NULL);
	xavix_dma_write(&dma, 4, 0x20, NULL);
	xavix_dma_write(&dma, 5, 0x00, NULL);
	xavix_dma_write(&dma, 6, 0x04, NULL);
	xavix_dma_write(&dma, 7, 0x00, NULL);
	CHECK(xavix_dma_write(&dma, 0, 0x41, &bus) == 4);
	CHECK(memory.reads == 4 && memory.writes == 4);
	CHECK(!memcmp(&memory.destination[0x20], &memory.source[0x10], 4));
	CHECK(xavix_dma_read(&dma, 6) == 0 && xavix_dma_read(&dma, 7) == 0);
	CHECK(xavix_dma_irq_pending(&dma));
	xavix_dma_write(&dma, 0, 0x80, NULL);
	CHECK(!xavix_dma_irq_pending(&dma));

	xavix_dma_write(&dma, 6, 3, NULL);
	CHECK(xavix_dma_write(&dma, 0, 1, NULL) == 0);
	CHECK(xavix_dma_read(&dma, 6) == 3);
}

static void test_serialization(void)
{
	xavix_peripherals original;
	xavix_peripherals restored;
	xavix_peripherals untouched;
	uint8_t *first;
	uint8_t *second;
	size_t size;
	size_t written = 0;

	xavix_peripherals_init(&original, NULL, 0, XAVIX_MASTER_CLOCK_NTSC);
	for (size_t index = 0; index < XAVIX_EEPROM24C08_SIZE; index++)
		original.eeprom.data[index] = (uint8_t)(index ^ (index >> 3));
	i2c_set_address(&original.eeprom, 0x155);
	CHECK(i2c_write_byte(&original.eeprom, 0xa7));
	CHECK(original.eeprom.data[0x155] != 0xa7);
	xavix_cu5501a_set_input(&original.sensor, 0x31, 0xc2, XAVIX_SENSOR_BROADSIDE);
	xavix_cu5501a_write_io1(&original.sensor, 0x21, 0x21);
	(void)xavix_cu5501a_read_adc(&original.sensor);
	(void)xavix_cu5501a_read_adc(&original.sensor);
	xavix_timer_write(&original.timer, 1, 0x9a);
	xavix_timer_write(&original.timer, 2, 5);
	xavix_timer_write(&original.timer, 0, 0x41);
	(void)xavix_timer_advance(&original.timer, 17);
	xavix_math_write(&original.math, 2, 3);
	xavix_math_write(&original.math, 3, 0xec);
	xavix_math_write(&original.math, 4, 0xfd);
	xavix_dma_write(&original.dma, 1, 0x33, NULL);
	xavix_dma_write(&original.dma, 6, 0x57, NULL);

	size = xavix_peripherals_serialized_size();
	CHECK(size > XAVIX_EEPROM24C08_SIZE);
	first = (uint8_t *)malloc(size);
	second = (uint8_t *)malloc(size);
	CHECK(first != NULL && second != NULL);
	if (!first || !second)
	{
		free(first);
		free(second);
		return;
	}

	CHECK(xavix_peripherals_serialize(&original, first, size, &written));
	CHECK(written == size);
	memset(&restored, 0xa5, sizeof(restored));
	CHECK(xavix_peripherals_deserialize(&restored, first, size));
	CHECK(xavix_peripherals_serialize(&restored, second, size, &written));
	CHECK(!memcmp(first, second, size));

	i2c_stop(&original.eeprom);
	i2c_stop(&restored.eeprom);
	CHECK(original.eeprom.data[0x155] == 0xa7);
	CHECK(restored.eeprom.data[0x155] == 0xa7);
	CHECK(xavix_cu5501a_read_adc(&original.sensor) == xavix_cu5501a_read_adc(&restored.sensor));
	CHECK(xavix_timer_advance(&original.timer, 1000) == xavix_timer_advance(&restored.timer, 1000));
	CHECK(original.timer.current_value == restored.timer.current_value);
	CHECK(xavix_peripherals_serialize(&original, first, size, &written));
	CHECK(xavix_peripherals_serialize(&restored, second, size, &written));
	CHECK(!memcmp(first, second, size));

	memset(&untouched, 0x3c, sizeof(untouched));
	restored = untouched;
	first[0] ^= 1;
	CHECK(!xavix_peripherals_deserialize(&restored, first, size));
	CHECK(!memcmp(&restored, &untouched, sizeof(restored)));
	first[0] ^= 1;
	CHECK(!xavix_peripherals_deserialize(&restored, first, size - 1));

	free(first);
	free(second);
}

int main(void)
{
	test_eeprom();
	test_eeprom24c02();
	test_eeprom24c04();
	test_sensor();
	test_timer();
	test_math();
	test_dma();
	test_serialization();

	if (failures)
	{
		fprintf(stderr, "%u peripheral test(s) failed\n", failures);
		return EXIT_FAILURE;
	}
	puts("xavix_peripherals: all tests passed");
	return EXIT_SUCCESS;
}
