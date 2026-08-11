// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#ifndef DRGQST_PLAYER_SHA1_H
#define DRGQST_PLAYER_SHA1_H

#include <stddef.h>
#include <stdint.h>

void sha1_calculate(const uint8_t *data, size_t length, uint8_t digest[20]);

#endif
