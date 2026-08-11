/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 Billy Jr. and contributors */
#ifndef DRGQST_PLAYER_STATE_H
#define DRGQST_PLAYER_STATE_H

#include "drgqst_core.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	DRGQST_STATE_FORMAT_VERSION = 1
};

/*
 * Canonical, fixed-version little-endian state image.  The image contains
 * CPU execution state, machine.state, audio state and frame scheduling.
 * It deliberately excludes the ROM, callback pointers, audio output buffer,
 * framebuffer, zbuffer and other derived video data.
 */
size_t drgqst_state_serialized_size(void);

/* On every call, written receives the required size when it is non-NULL. */
int drgqst_state_save(const drgqst_core *core, uint8_t *output,
	size_t output_size, size_t *written);

/*
 * Loading is atomic: malformed, truncated, oversized or incompatible input
 * leaves core unchanged.  A successful load preserves the destination
 * core's ROM and all host callbacks.  Derived video/audio output buffers are
 * cleared; drgqst_core_run_frame() renders the restored state normally.
 */
int drgqst_state_load(drgqst_core *core, const uint8_t *input,
	size_t input_size);

#ifdef __cplusplus
}
#endif

#endif
