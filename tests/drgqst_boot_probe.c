// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "core/drgqst_core.h"
#include "rom_loader.h"

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>

typedef struct checkpoint
{
	uint32_t instructions;
	uint32_t linear_pc;
	uint8_t a;
	uint8_t x;
	uint8_t y;
	uint8_t s;
	uint8_t p;
	uint8_t data_bank;
} checkpoint;

static int compare_checkpoint(const drgqst_core *core, const checkpoint *expected)
{
	const xavix_cpu_t *cpu = &core->cpu;
	int match = xavix_cpu_linear_pc(cpu) == expected->linear_pc &&
		cpu->a == expected->a && cpu->x == expected->x && cpu->y == expected->y &&
		cpu->s == expected->s && cpu->p == expected->p && cpu->data_bank == expected->data_bank;
	printf("%5lu  PC=%06lX A=%02X X=%02X Y=%02X S=%02X P=%02X CB=%02X DB=%02X %s\n",
		(unsigned long)expected->instructions,
		(unsigned long)xavix_cpu_linear_pc(cpu), cpu->a, cpu->x, cpu->y, cpu->s,
		cpu->p, cpu->code_bank, cpu->data_bank, match ? "OK" : "MISMATCH");
	return match;
}

int main(int argc, char **argv)
{
	static const checkpoint checkpoints[] = {
		{ 0,    0x00f2b1, 0x00, 0x80, 0x00, 0xfd, 0x36, 0x00 },
		{ 1,    0x00f2b3, 0x04, 0x80, 0x00, 0xfd, 0x34, 0x00 },
		{ 8,    0x00f2c3, 0x01, 0xff, 0x00, 0xff, 0xb4, 0x00 },
		{ 21,   0x00f2d5, 0x80, 0x02, 0x00, 0xff, 0x34, 0x00 },
		{ 101,  0x00f2d9, 0x80, 0x1d, 0x00, 0xff, 0x34, 0x00 },
		{ 501,  0x00f2d5, 0x80, 0xa2, 0x00, 0xff, 0xb4, 0x00 },
		{ 1001, 0x20ffed, 0xc1, 0x00, 0x2f, 0xfc, 0xb5, 0x00 }
	};
	drgqst_rom_image image = { 0 };
	drgqst_core *core = NULL;
	wchar_t path[32768];
	wchar_t error[384];
	uint32_t completed = 0;
	unsigned index;
	int ok = 1;

	if (argc != 2 || !MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path,
		(int)(sizeof(path) / sizeof(path[0]))))
	{
		fprintf(stderr, "usage: drgqst-boot-probe <drgqst.zip>\n");
		return 2;
	}
	if (!drgqst_rom_load_zip(path, &image, error, sizeof(error) / sizeof(error[0])))
	{
		fwprintf(stderr, L"ROM error: %ls\n", error);
		return 2;
	}
	core = (drgqst_core *)calloc(1, sizeof(*core));
	if (!core || !drgqst_core_init(core, image.data, image.size))
	{
		fprintf(stderr, "core allocation/initialization failed\n");
		free(core);
		drgqst_rom_release(&image);
		return 2;
	}
	for (index = 0; index < sizeof(checkpoints) / sizeof(checkpoints[0]); ++index)
	{
		uint32_t wanted = checkpoints[index].instructions;
		if (wanted > completed)
		{
			drgqst_core_run_instructions(core, wanted - completed);
			completed = wanted;
		}
		ok &= compare_checkpoint(core, &checkpoints[index]);
	}
	ok &= core->machine.state.extbus_control[0] == 0x04;
	ok &= core->machine.state.extbus_control[2] == 0x01;
	printf("cycles=%llu extbus=%02X/%02X/%02X result=%s\n",
		(unsigned long long)core->cpu.total_cycles,
		core->machine.state.extbus_control[0], core->machine.state.extbus_control[1],
		core->machine.state.extbus_control[2], ok ? "PASS" : "FAIL");
	free(core);
	drgqst_rom_release(&image);
	return ok ? 0 : 1;
}
