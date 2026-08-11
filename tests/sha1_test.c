// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "sha1.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int check(const uint8_t *data, size_t length, const uint8_t expected[20])
{
	uint8_t digest[20];

	sha1_calculate(data, length, digest);
	return memcmp(digest, expected, sizeof(digest)) == 0;
}

int main(void)
{
	static const uint8_t EMPTY_SHA1[20] = {
		0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
		0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09
	};
	static const uint8_t ABC_SHA1[20] = {
		0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
		0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d
	};
	static const uint8_t ABC[] = { 'a', 'b', 'c' };
	static const uint8_t MILLION_A_SHA1[20] = {
		0x34, 0xaa, 0x97, 0x3c, 0xd4, 0xc4, 0xda, 0xa4, 0xf6, 0x1e,
		0xeb, 0x2b, 0xdb, 0xad, 0x27, 0x31, 0x65, 0x34, 0x01, 0x6f
	};
	uint8_t *million_a;

	if (!check(NULL, 0, EMPTY_SHA1))
		return 1;
	if (!check(ABC, sizeof(ABC), ABC_SHA1))
		return 2;
	million_a = malloc(1000000);
	if (!million_a)
		return 3;
	memset(million_a, 'a', 1000000);
	if (!check(million_a, 1000000, MILLION_A_SHA1))
	{
		free(million_a);
		return 4;
	}
	free(million_a);
	return 0;
}
