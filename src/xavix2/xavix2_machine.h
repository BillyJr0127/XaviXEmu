// SPDX-License-Identifier: BSD-3-Clause
// MAME-derived portions copyright-holder: David Haywood
// XaviXEmu adaptation and modifications:
// Copyright (c) 2026 Billy Jr. and contributors

#ifndef XAVIXEMU_XAVIX2_MACHINE_H
#define XAVIXEMU_XAVIX2_MACHINE_H

#include "xavix2_cpu.h"
#include "xavix2_audio.h"
#include "xavix_peripherals.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	XAVIX2_LOW_RAM_SIZE = 0x10000,
	XAVIX2_PROGRAM_RAM_SIZE = 0x10000,
	XAVIX2_VIDEO_RAM_SIZE = 0x1f800,
	XAVIX2_MMIO_SIZE = 0x2000,
	XAVIX2_CPU_CLOCK = XAVIX2_AUDIO_MASTER_CLOCK,
	XAVIX2_CYCLES_PER_FRAME = XAVIX2_CPU_CLOCK / 60,
	XAVIX2_TIMER_CYCLES = XAVIX2_CPU_CLOCK / 120,
	XAVIX2_CAPTURE_REGISTER_FIRST = 0x240,
	XAVIX2_CAPTURE_REGISTER_COUNT = 12,
	XAVIX2_CAPTURE_TRACE_CAPACITY = 1024,
	XAVIX2_MOTION_SAMPLE_FIRST = 0x05,
	XAVIX2_MOTION_SAMPLE_SIZE = 19,
	XAVIX2_MOTION_PACKET_FIRST = 0x0d,
	XAVIX2_MOTION_PACKET_SIZE = 7,
	XAVIX2_IRQ_CONTEXT_FIRST = 0x18,
	XAVIX2_IRQ_CONTEXT_SIZE = 16,
	XAVIX2_SENSOR_BUFFER_FIRST = 0x1010,
	XAVIX2_SENSOR_BUFFER_SIZE = 16,
	XAVIX2_SENSOR_DECODED_FIRST = 0x1020,
	XAVIX2_SENSOR_DECODED_SIZE = 16,
	XAVIX2_MOTION_SOURCE_FIRST = 0x1200,
	XAVIX2_MOTION_SOURCE_SIZE = 0x100,
	XAVIX2_ACTION_STATE_FIRST = 0x13a0,
	XAVIX2_ACTION_STATE_SIZE = 0x20,
	XAVIX2_AUDIO_MMIO_FIRST = 0xa00,
	XAVIX2_AUDIO_MMIO_SIZE = 0x20,
	XAVIX2_AUDIO_TRACE_CAPACITY = 4096
};

typedef struct xavix2_capture_trace_entry
{
	uint64_t cycle;
	uint32_t pc;
	uint16_t offset;
	uint8_t data;
	uint8_t write;
} xavix2_capture_trace_entry;

typedef struct xavix2_machine
{
	xavix2_cpu_t cpu;
	const uint8_t *rom;
	size_t rom_size;
	uint8_t low_ram[XAVIX2_LOW_RAM_SIZE];
	uint8_t program_ram[XAVIX2_PROGRAM_RAM_SIZE];
	uint8_t video_ram[XAVIX2_VIDEO_RAM_SIZE];
	uint8_t palette_ram[0x800];
	uint8_t mmio[XAVIX2_MMIO_SIZE];
	xavix2_audio audio;
	uint16_t motion_packet_address;
	uint64_t mmio_read_counts[XAVIX2_MMIO_SIZE];
	uint64_t mmio_write_counts[XAVIX2_MMIO_SIZE];
	uint32_t mmio_last_read_pc[XAVIX2_MMIO_SIZE];
	uint32_t mmio_last_write_pc[XAVIX2_MMIO_SIZE];
	xavix2_capture_trace_entry audio_mmio_trace[XAVIX2_AUDIO_TRACE_CAPACITY];
	uint64_t audio_mmio_trace_total;
	uint32_t audio_mmio_trace_next;
	uint32_t screen_data[0x400 * 0x800];
	xavix_eeprom24c08 eeprom;
	uint32_t pio_fixed_input;
	uint32_t pio_input;
	int experimental_direct_pio_sample;
	int experimental_dispatch_input;
	int experimental_callback_pending;
	uint32_t experimental_callback_address;
	int experimental_capture_readback;
	uint16_t experimental_capture_a;
	uint16_t experimental_capture_b;
	uint32_t pio_output_mask;
	uint32_t experimental_sampled_pio;
	uint64_t pio_read_count;
	uint64_t pio_input_read_count;
	uint32_t pio_observed_input_or;
	uint64_t input_state_read_count;
	uint32_t last_input_state_read_pc;
	uint16_t last_input_state_read_address;
	uint32_t last_input_state_regs[8];
	uint32_t last_pio_read_pc;
	uint32_t last_pio_read_value;
	uint64_t capture_read_count[XAVIX2_CAPTURE_REGISTER_COUNT];
	uint64_t capture_write_count[XAVIX2_CAPTURE_REGISTER_COUNT];
	xavix2_capture_trace_entry capture_trace[XAVIX2_CAPTURE_TRACE_CAPACITY];
	uint32_t capture_trace_count;
	uint64_t capture_trace_dropped;
	uint64_t sensor_buffer_read_count;
	uint64_t sensor_buffer_write_count;
	xavix2_capture_trace_entry sensor_buffer_trace[XAVIX2_CAPTURE_TRACE_CAPACITY];
	uint32_t sensor_buffer_trace_count;
	uint64_t sensor_buffer_trace_dropped;
	uint64_t sensor_decoded_read_count;
	uint64_t sensor_decoded_write_count;
	xavix2_capture_trace_entry sensor_decoded_trace[XAVIX2_CAPTURE_TRACE_CAPACITY];
	uint32_t sensor_decoded_trace_count;
	uint64_t sensor_decoded_trace_dropped;
	uint64_t diagnostic_trace_start_cycle;
	uint64_t motion_sample_read_count;
	uint64_t motion_sample_write_count;
	xavix2_capture_trace_entry motion_sample_trace[XAVIX2_CAPTURE_TRACE_CAPACITY];
	uint32_t motion_sample_trace_count;
	uint64_t motion_sample_trace_dropped;
	uint64_t irq_context_read_count;
	uint64_t irq_context_write_count;
	xavix2_capture_trace_entry irq_context_trace[XAVIX2_CAPTURE_TRACE_CAPACITY];
	uint32_t irq_context_trace_count;
	uint64_t irq_context_trace_dropped;
	uint64_t motion_source_read_count;
	uint64_t motion_source_write_count;
	xavix2_capture_trace_entry motion_source_trace[XAVIX2_CAPTURE_TRACE_CAPACITY];
	uint32_t motion_source_trace_count;
	uint64_t motion_source_trace_dropped;
	uint64_t action_state_read_count;
	uint64_t action_state_write_count;
	xavix2_capture_trace_entry action_state_trace[XAVIX2_CAPTURE_TRACE_CAPACITY];
	uint32_t action_state_trace_count;
	uint64_t action_state_trace_dropped;
	uint16_t diagnostic_ram_first;
	uint16_t diagnostic_ram_last;
	uint64_t diagnostic_ram_read_count;
	uint64_t diagnostic_ram_write_count;
	xavix2_capture_trace_entry diagnostic_ram_trace[XAVIX2_CAPTURE_TRACE_CAPACITY];
	uint32_t diagnostic_ram_trace_count;
	uint64_t diagnostic_ram_trace_dropped;
	uint32_t interrupt_active;
	uint32_t interrupt_pending;
	uint32_t interrupt_enabled;
	uint32_t interrupt_nmi;
	uint8_t interrupt_latched_level;
	uint8_t interrupt_latched_valid;
	uint64_t irq_level_read_count;
	uint64_t irq_clear_write_count;
	uint32_t last_irq_clear_pc;
	uint16_t last_irq_clear_mask;
	uint64_t next_vblank_cycle;
	uint64_t dma_completion_cycle;
	uint64_t gpu_trigger_count;
	uint64_t gpu_pixel_write_count;
	uint8_t gpu_sprite_background_prepared;
	uint8_t timer_rate_hz;
	uint64_t dma_transfer_count;
	uint64_t frame_count;
	uint64_t unmapped_read_count;
	uint64_t unmapped_write_count;
	uint32_t first_unmapped_read;
	uint32_t first_unmapped_write;
	uint32_t last_gpu_pc;
	uint32_t first_gpu_pc;
	uint32_t last_gpu_register;
	uint16_t last_gpu_count;
	uint16_t maximum_gpu_count;
	uint8_t debug_text[256];
	unsigned debug_length;
	uint64_t next_timer_cycle;
} xavix2_machine_t;

int xavix2_machine_init(xavix2_machine_t *machine, const uint8_t *rom,
	size_t rom_size);
void xavix2_machine_set_motion_packet_address(xavix2_machine_t *machine,
	uint16_t address);
void xavix2_machine_set_fixed_pio_input(xavix2_machine_t *machine,
	uint32_t input);
void xavix2_machine_set_timer_rate(xavix2_machine_t *machine,
	unsigned rate_hz);
void xavix2_machine_update_takecopter_timer_rate(xavix2_machine_t *machine);
void xavix2_machine_set_high_resolution_3d(xavix2_machine_t *machine,
	int enabled);
void xavix2_machine_set_skip_render(xavix2_machine_t *machine, int enabled);
unsigned xavix2_machine_frame_scale(const xavix2_machine_t *machine);
void xavix2_machine_reset(xavix2_machine_t *machine);
void xavix2_machine_raise_irq(xavix2_machine_t *machine, unsigned level);
void xavix2_machine_clear_irq(xavix2_machine_t *machine, unsigned level);
int xavix2_machine_transmit_epoch_ir(xavix2_machine_t *machine,
	uint32_t serial_word);
void xavix2_machine_set_capture(xavix2_machine_t *machine,
	uint16_t capture_a, uint16_t capture_b);
uint64_t xavix2_machine_execute(xavix2_machine_t *machine,
	uint64_t cycle_budget);
uint64_t xavix2_machine_run_video_frame(xavix2_machine_t *machine,
	const uint8_t motion_packet[XAVIX2_MOTION_PACKET_SIZE],
	uint32_t pio_input);
const uint32_t *xavix2_machine_visible_frame(const xavix2_machine_t *machine,
	unsigned *width, unsigned *height, unsigned *stride);
const int16_t *xavix2_machine_frame_audio(const xavix2_machine_t *machine);
size_t xavix2_machine_state_size(void);
int xavix2_machine_state_save(const xavix2_machine_t *machine,
	void *output, size_t output_capacity, size_t *output_size);
int xavix2_machine_state_load(xavix2_machine_t *machine,
	const void *input, size_t input_size);

#ifdef __cplusplus
}
#endif

#endif /* XAVIXEMU_XAVIX2_MACHINE_H */
