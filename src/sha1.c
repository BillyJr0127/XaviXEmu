// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "sha1.h"

#include <string.h>

typedef struct sha1_context
{
	uint32_t state[5];
	uint64_t total_bytes;
	uint8_t block[64];
	size_t used;
} sha1_context;

static uint32_t rotate_left(uint32_t value, unsigned count)
{
	return (value << count) | (value >> (32 - count));
}

static void process_block(sha1_context *context)
{
	uint32_t words[80];
	uint32_t a;
	uint32_t b;
	uint32_t c;
	uint32_t d;
	uint32_t e;
	unsigned index;

	for (index = 0; index < 16; ++index)
	{
		const uint8_t *source = context->block + index * 4;
		words[index] = ((uint32_t)source[0] << 24) | ((uint32_t)source[1] << 16) |
			((uint32_t)source[2] << 8) | source[3];
	}
	for (; index < 80; ++index)
		words[index] = rotate_left(words[index - 3] ^ words[index - 8] ^ words[index - 14] ^ words[index - 16], 1);

	a = context->state[0];
	b = context->state[1];
	c = context->state[2];
	d = context->state[3];
	e = context->state[4];
	for (index = 0; index < 80; ++index)
	{
		uint32_t function;
		uint32_t constant;
		uint32_t temporary;

		if (index < 20)
		{
			function = (b & c) | ((~b) & d);
			constant = UINT32_C(0x5a827999);
		}
		else if (index < 40)
		{
			function = b ^ c ^ d;
			constant = UINT32_C(0x6ed9eba1);
		}
		else if (index < 60)
		{
			function = (b & c) | (b & d) | (c & d);
			constant = UINT32_C(0x8f1bbcdc);
		}
		else
		{
			function = b ^ c ^ d;
			constant = UINT32_C(0xca62c1d6);
		}

		temporary = rotate_left(a, 5) + function + e + constant + words[index];
		e = d;
		d = c;
		c = rotate_left(b, 30);
		b = a;
		a = temporary;
	}

	context->state[0] += a;
	context->state[1] += b;
	context->state[2] += c;
	context->state[3] += d;
	context->state[4] += e;
}

static void initialize(sha1_context *context)
{
	memset(context, 0, sizeof(*context));
	context->state[0] = UINT32_C(0x67452301);
	context->state[1] = UINT32_C(0xefcdab89);
	context->state[2] = UINT32_C(0x98badcfe);
	context->state[3] = UINT32_C(0x10325476);
	context->state[4] = UINT32_C(0xc3d2e1f0);
}

static void update(sha1_context *context, const uint8_t *data, size_t length)
{
	context->total_bytes += length;
	while (length)
	{
		size_t available = sizeof(context->block) - context->used;
		size_t amount = length < available ? length : available;

		memcpy(context->block + context->used, data, amount);
		context->used += amount;
		data += amount;
		length -= amount;
		if (context->used == sizeof(context->block))
		{
			process_block(context);
			context->used = 0;
		}
	}
}

static void finish(sha1_context *context, uint8_t digest[20])
{
	uint64_t total_bits = context->total_bytes * 8;
	unsigned index;

	context->block[context->used++] = 0x80;
	if (context->used > 56)
	{
		memset(context->block + context->used, 0, sizeof(context->block) - context->used);
		process_block(context);
		context->used = 0;
	}
	memset(context->block + context->used, 0, 56 - context->used);
	for (index = 0; index < 8; ++index)
		context->block[56 + index] = (uint8_t)(total_bits >> (56 - index * 8));
	process_block(context);

	for (index = 0; index < 5; ++index)
	{
		digest[index * 4] = (uint8_t)(context->state[index] >> 24);
		digest[index * 4 + 1] = (uint8_t)(context->state[index] >> 16);
		digest[index * 4 + 2] = (uint8_t)(context->state[index] >> 8);
		digest[index * 4 + 3] = (uint8_t)context->state[index];
	}
}

void sha1_calculate(const uint8_t *data, size_t length, uint8_t digest[20])
{
	sha1_context context;

	initialize(&context);
	update(&context, data, length);
	finish(&context, digest);
}
