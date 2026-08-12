// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors
//
// Independently written tests. Expected CPU behavior is checked against the
// separately attributed MAME reference implementation documented in
// docs/provenance.md.

#include "../src/core/xavix_cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef XAVIX_CPU_ROM_ORACLE
#include "../src/rom_loader.h"

#include <wchar.h>

#ifndef XAVIX_CPU_ORACLE_LIMIT
#define XAVIX_CPU_ORACLE_LIMIT 1000U
#endif
#endif

#define ROM_SIZE UINT32_C(0x800000)

typedef struct fixture
{
	uint8_t low[0x8000];
	uint8_t *rom;
	uint32_t external_writes;
} fixture_t;

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
			return 0; \
		} \
	} while (0)

#define RUN_TEST(test) \
	do { \
		if (!(test)) { \
			fprintf(stderr, "%s:%d: test failed: %s\n", __FILE__, __LINE__, #test); \
			return EXIT_FAILURE; \
		} \
	} while (0)

static uint8_t fixture_read(void *opaque, xavix_cpu_bus_t bus, uint32_t address)
{
	fixture_t *const fixture = (fixture_t *)opaque;
	if (bus == XAVIX_CPU_BUS_LOW)
		return fixture->low[address & 0x7fff];
	return fixture->rom[address & (ROM_SIZE - 1)];
}

static void fixture_write(void *opaque, xavix_cpu_bus_t bus, uint32_t address, uint8_t data)
{
	fixture_t *const fixture = (fixture_t *)opaque;
	if (bus == XAVIX_CPU_BUS_LOW)
		fixture->low[address & 0x7fff] = data;
	else if (bus == XAVIX_CPU_BUS_EXTERNAL)
		fixture->external_writes++;
}

static int fixture_create(fixture_t *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->rom = (uint8_t *)calloc(ROM_SIZE, 1);
	return fixture->rom != NULL;
}

static void fixture_destroy(fixture_t *fixture)
{
	free(fixture->rom);
	fixture->rom = NULL;
}

static void set_vector(fixture_t *fixture, uint16_t vector, uint16_t target)
{
	fixture->rom[vector] = (uint8_t)target;
	fixture->rom[(uint16_t)(vector + 1)] = (uint8_t)(target >> 8);
}

static int step(xavix_cpu_t *cpu)
{
	const int cycles = xavix_cpu_execute(cpu, 1);
	CHECK(cycles > 0);
	CHECK(!cpu->stopped);
	return cycles;
}

static int test_reset_and_boot_prefix(void)
{
	static const uint8_t program[] = {
		0xa9, 0x04,       /* lda #$04: actual reset vector target */
		0x8d, 0x00, 0x79, /* sta $7900 */
		0xa9, 0x00,       /* lda #$00 */
		0x8d, 0x10, 0x78, /* sta $7810 */
		0xa9, 0x01,       /* lda #$01 */
		0x8d, 0x02, 0x79, /* sta $7902 */
		0xa2, 0xff,       /* ldx #$ff */
		0x9a              /* txs */
	};
	fixture_t fixture;
	xavix_cpu_t cpu;

	CHECK(fixture_create(&fixture));
	set_vector(&fixture, 0xfffc, 0xf2b1);
	memcpy(&fixture.rom[0xf2b1], program, sizeof(program));
	fixture.low[0x7900] = 0xcc;
	fixture.low[0x7810] = 0xcc;
	fixture.low[0x7902] = 0xcc;
	xavix_cpu_init(&cpu, fixture_read, fixture_write, &fixture);

	CHECK(cpu.pc == 0xf2b1);
	CHECK(cpu.code_bank == 0);
	CHECK(cpu.data_bank == 0);
	CHECK(cpu.a == 0x00 && cpu.x == 0x80 && cpu.y == 0x00);
	CHECK(cpu.s == 0xfd && cpu.p == 0x36);
	CHECK(step(&cpu) == 2);
	CHECK(cpu.pc == 0xf2b3 && cpu.a == 0x04 && cpu.p == 0x34);
	CHECK(xavix_cpu_execute(&cpu, 20) == 20);
	CHECK(!cpu.stopped);
	CHECK(cpu.pc == 0xf2c3);
	CHECK(cpu.a == 0x01 && cpu.x == 0xff && cpu.s == 0xff);
	CHECK(fixture.low[0x7900] == 0x04);
	CHECK(fixture.low[0x7810] == 0x00);
	CHECK(fixture.low[0x7902] == 0x01);
	CHECK(cpu.total_cycles == 22);

	fixture_destroy(&fixture);
	return 1;
}

static int test_far_call(void)
{
	static const uint8_t caller[] = {
		0xa9, 0x2a,             /* lda #$2a */
		0x83,                   /* staj */
		0x22, 0x01, 0x00, 0x90, /* callf $01:$9000 */
		0x8d, 0x00, 0x02        /* sta $0200 */
	};
	static const uint8_t callee[] = {
		0xa9, 0x05, /* lda #$05 */
		0x63,       /* adcj */
		0x80        /* retf */
	};
	fixture_t fixture;
	xavix_cpu_t cpu;
	int i;

	CHECK(fixture_create(&fixture));
	set_vector(&fixture, 0xfffc, 0x8000);
	memcpy(&fixture.rom[0x008000], caller, sizeof(caller));
	memcpy(&fixture.rom[0x019000], callee, sizeof(callee));
	xavix_cpu_init(&cpu, fixture_read, fixture_write, &fixture);

	for (i = 0; i < 3; i++)
		CHECK(step(&cpu));
	CHECK(cpu.code_bank == 0x01 && cpu.pc == 0x9000 && cpu.s == 0xfa);
	for (i = 0; i < 3; i++)
		CHECK(step(&cpu));
	CHECK(cpu.code_bank == 0x00 && cpu.pc == 0x8007 && cpu.s == 0xfd);
	CHECK(step(&cpu));
	CHECK(fixture.low[0x0200] == 0x2f);
	CHECK(cpu.total_cycles == 28);

	fixture_destroy(&fixture);
	return 1;
}

static int test_data_bank_and_zero_page(void)
{
	static const uint8_t program[] = {
		0xa9, 0x80,       /* lda #$80 */
		0x85, 0xff,       /* sta $ff: data bank */
		0xa9, 0x5a,       /* lda #$5a */
		0x85, 0x10,       /* sta $10: always lowbus */
		0xa9, 0x00,       /* lda #$00 */
		0xa5, 0x10,       /* lda $10: always lowbus */
		0xad, 0x10, 0x00, /* lda $0010: DB-qualified */
		0xad, 0x00, 0x90  /* lda $9000: DB-qualified */
	};
	fixture_t fixture;
	xavix_cpu_t cpu;
	int i;

	CHECK(fixture_create(&fixture));
	set_vector(&fixture, 0xfffc, 0x8000);
	memcpy(&fixture.rom[0x008000], program, sizeof(program));
	fixture.rom[0x000010] = 0xc3;
	fixture.rom[0x009000] = 0xa6;
	xavix_cpu_init(&cpu, fixture_read, fixture_write, &fixture);

	for (i = 0; i < 6; i++)
		CHECK(step(&cpu));
	CHECK(cpu.data_bank == 0x80);
	CHECK(fixture.low[0x10] == 0x5a && cpu.a == 0x5a);
	CHECK(step(&cpu));
	CHECK(cpu.a == 0xc3);
	CHECK(step(&cpu));
	CHECK(cpu.a == 0xa6);

	/* RMW zero-page modes also bypass DB. */
	cpu.code_bank = 0;
	cpu.pc = 0x0200;
	fixture.low[0x0200] = 0xe6; /* inc $10 */
	fixture.low[0x0201] = 0x10;
	fixture.low[0x10] = 0x7f;
	CHECK(step(&cpu));
	CHECK(fixture.low[0x10] == 0x80);

	fixture_destroy(&fixture);
	return 1;
}

static int test_pointer_register(void)
{
	static const uint8_t program[] = {
		0xa9, 0x00, 0x1b, /* PA low */
		0xa9, 0x90, 0x5b, /* PA middle */
		0xa9, 0x01, 0x9b, /* PA high */
		0xb3              /* lda [PA] */
	};
	fixture_t fixture;
	xavix_cpu_t cpu;
	int i;

	CHECK(fixture_create(&fixture));
	set_vector(&fixture, 0xfffc, 0x8000);
	memcpy(&fixture.rom[0x008000], program, sizeof(program));
	fixture.rom[0x019000] = 0x7c;
	xavix_cpu_init(&cpu, fixture_read, fixture_write, &fixture);
	for (i = 0; i < 7; i++)
		CHECK(step(&cpu));
	CHECK(cpu.pa == UINT32_C(0x019000));
	CHECK(cpu.a == 0x7c);

	fixture_destroy(&fixture);
	return 1;
}

static int test_nmi_and_rti(void)
{
	static const uint8_t handler[] = {
		0xa9, 0x77, /* lda #$77 */
		0x40        /* rti */
	};
	fixture_t fixture;
	xavix_cpu_t cpu;

	CHECK(fixture_create(&fixture));
	set_vector(&fixture, 0xfffc, 0x8000);
	set_vector(&fixture, 0xfffa, 0x9000);
	fixture.rom[0x018000] = 0xea;
	memcpy(&fixture.rom[0x009000], handler, sizeof(handler));
	xavix_cpu_init(&cpu, fixture_read, fixture_write, &fixture);
	cpu.code_bank = 1;
	cpu.a = 0x11;
	xavix_cpu_set_nmi(&cpu, 1);

	CHECK(step(&cpu) == 8);
	CHECK(cpu.code_bank == 0 && cpu.pc == 0x9000 && cpu.s == 0xf9);
	CHECK(step(&cpu) == 2);
	CHECK(cpu.a == 0x77);
	CHECK(step(&cpu) == 7);
	CHECK(cpu.code_bank == 1 && cpu.pc == 0x8000 && cpu.s == 0xfd);
	CHECK(!cpu.nmi_pending);

	fixture_destroy(&fixture);
	return 1;
}

static int test_decimal_adc(void)
{
	static const uint8_t program[] = {
		0xa9, 0x45, /* lda #$45 */
		0xf8,       /* sed */
		0x18,       /* clc */
		0x69, 0x55  /* adc #$55 */
	};
	fixture_t fixture;
	xavix_cpu_t cpu;
	int i;

	CHECK(fixture_create(&fixture));
	set_vector(&fixture, 0xfffc, 0x8000);
	memcpy(&fixture.rom[0x008000], program, sizeof(program));
	xavix_cpu_init(&cpu, fixture_read, fixture_write, &fixture);
	for (i = 0; i < 4; i++)
		CHECK(step(&cpu));
	CHECK(cpu.a == 0x00);
	CHECK(cpu.p & XAVIX_CPU_C);
	CHECK(cpu.p & XAVIX_CPU_N);
	CHECK(!(cpu.p & XAVIX_CPU_Z));

	fixture_destroy(&fixture);
	return 1;
}

static int test_all_opcodes_dispatch(void)
{
	fixture_t fixture;
	xavix_cpu_t cpu;
	unsigned opcode;

	CHECK(fixture_create(&fixture));
	set_vector(&fixture, 0xfffc, 0x0200);
	set_vector(&fixture, 0xfffa, 0x0200);
	set_vector(&fixture, 0xfffe, 0x0200);
	xavix_cpu_init(&cpu, fixture_read, fixture_write, &fixture);

	for (opcode = 0; opcode < 0x100; opcode++)
	{
		memset(fixture.low, 0, sizeof(fixture.low));
		xavix_cpu_reset(&cpu);
		cpu.pc = 0x0200;
		fixture.low[0x0200] = (uint8_t)opcode;
		fixture.low[0x0201] = 0;
		fixture.low[0x0202] = 0;
		fixture.low[0x0203] = 0;
		CHECK(step(&cpu));
		CHECK(!cpu.stopped);
	}

	fixture_destroy(&fixture);
	return 1;
}

#ifdef XAVIX_CPU_ROM_ORACLE
typedef struct oracle_checkpoint
{
	unsigned cb;
	unsigned db;
	unsigned a;
	unsigned x;
	unsigned y;
	unsigned sp;
	unsigned p;
	unsigned j;
	unsigned k;
	unsigned l;
	unsigned m;
	unsigned pa;
	unsigned pb;
	unsigned xpc;
} oracle_checkpoint_t;

static uint8_t oracle_read(void *opaque, xavix_cpu_bus_t bus, uint32_t address)
{
	if (bus == XAVIX_CPU_BUS_LOW && address >= 0x4000 && address < 0x4100)
	{
		const uint8_t offset = (uint8_t)address;
		return (uint8_t)((offset >> 4) | (offset << 4));
	}
	return fixture_read(opaque, bus, address);
}

static int parse_oracle_line(const char *line, oracle_checkpoint_t *checkpoint)
{
	return sscanf(line,
		"CB=%x DB=%x A=%x X=%x Y=%x SP=%x P=%x J=%x K=%x L=%x M=%x PA=%x PB=%x | %x:",
		&checkpoint->cb, &checkpoint->db, &checkpoint->a, &checkpoint->x,
		&checkpoint->y, &checkpoint->sp, &checkpoint->p, &checkpoint->j,
		&checkpoint->k, &checkpoint->l, &checkpoint->m, &checkpoint->pa,
		&checkpoint->pb, &checkpoint->xpc) == 14;
}

static int oracle_matches(const xavix_cpu_t *cpu, const oracle_checkpoint_t *checkpoint)
{
	/* PA in this trace is known debugger/tracelog formatting noise.  The
	 * direct debugger boundary probe reports its deterministic reset value 0. */
	return cpu->code_bank == checkpoint->cb &&
		cpu->data_bank == checkpoint->db &&
		cpu->a == checkpoint->a && cpu->x == checkpoint->x && cpu->y == checkpoint->y &&
		cpu->s == (uint8_t)checkpoint->sp && cpu->p == checkpoint->p &&
		cpu->j == checkpoint->j && cpu->k == checkpoint->k &&
		cpu->l == checkpoint->l && cpu->m == checkpoint->m &&
		cpu->pb == checkpoint->pb && xavix_cpu_linear_pc(cpu) == checkpoint->xpc;
}

static int run_rom_oracle(const char *zip_name, const char *trace_name)
{
	drgqst_rom_image image = { 0 };
	wchar_t zip_name_wide[1024];
	wchar_t error[512];
	fixture_t fixture;
	xavix_cpu_t cpu;
	FILE *trace;
	char line[512];
	unsigned line_number = 0;
	int result = 1;

	if (mbstowcs(zip_name_wide, zip_name, sizeof(zip_name_wide) / sizeof(zip_name_wide[0]) - 1) == (size_t)-1)
	{
		fprintf(stderr, "oracle: ROM ZIP path cannot be converted\n");
		return 1;
	}
	zip_name_wide[sizeof(zip_name_wide) / sizeof(zip_name_wide[0]) - 1] = L'\0';
	if (!drgqst_rom_load_zip(zip_name_wide, &image, error, sizeof(error) / sizeof(error[0])))
	{
		fwprintf(stderr, L"oracle: ROM load failed: %ls\n", error);
		return 1;
	}
	trace = fopen(trace_name, "rb");
	if (!trace)
	{
		fprintf(stderr, "oracle: cannot open trace %s\n", trace_name);
		drgqst_rom_release(&image);
		return 1;
	}

	memset(&fixture, 0, sizeof(fixture));
	/* xavix_state::machine_start initializes its 16 KiB CPU RAM to FF. */
	memset(fixture.low, 0xff, 0x4000);
	fixture.rom = image.data;
	xavix_cpu_init(&cpu, oracle_read, fixture_write, &fixture);
	if (cpu.pc != 0xf2b1 || cpu.a != 0 || cpu.x != 0x80 || cpu.y != 0 ||
		cpu.s != 0xfd || cpu.p != 0x36 || cpu.pa != 0)
	{
		fprintf(stderr, "oracle: reset boundary mismatch at %02X:%04X\n", cpu.code_bank, cpu.pc);
		goto done;
	}

	while (line_number < XAVIX_CPU_ORACLE_LIMIT && fgets(line, sizeof(line), trace))
	{
		oracle_checkpoint_t checkpoint;
		const int cycles = xavix_cpu_execute(&cpu, 1);
		line_number++;
		if (cycles <= 0 || cpu.stopped)
		{
			fprintf(stderr, "oracle: CPU stopped at trace line %u\n", line_number);
			goto done;
		}
		if (!parse_oracle_line(line, &checkpoint))
		{
			fprintf(stderr, "oracle: cannot parse trace line %u\n", line_number);
			goto done;
		}
		if (!oracle_matches(&cpu, &checkpoint))
		{
			fprintf(stderr,
				"oracle: first divergence after instruction %u\n"
				"  expected CB=%02X DB=%02X A=%02X X=%02X Y=%02X S=%02X P=%02X "
				"J=%02X K=%02X L=%02X M=%02X PB=%06X XPC=%06X\n"
				"  actual   CB=%02X DB=%02X A=%02X X=%02X Y=%02X S=%02X P=%02X "
				"J=%02X K=%02X L=%02X M=%02X PB=%06X XPC=%06X\n",
				line_number, checkpoint.cb, checkpoint.db, checkpoint.a, checkpoint.x,
				checkpoint.y, (uint8_t)checkpoint.sp, checkpoint.p, checkpoint.j,
				checkpoint.k, checkpoint.l, checkpoint.m, checkpoint.pb, checkpoint.xpc,
				cpu.code_bank, cpu.data_bank, cpu.a, cpu.x, cpu.y, cpu.s, cpu.p,
				cpu.j, cpu.k, cpu.l, cpu.m, cpu.pb, xavix_cpu_linear_pc(&cpu));
			goto done;
		}
		if (line_number == 7 || line_number == 20 || line_number == 100 ||
			line_number == 500 || line_number == 1000)
		{
			printf("oracle checkpoint %u: %02X:%04X matched\n",
				line_number, cpu.code_bank, cpu.pc);
		}
	}
	if (line_number != XAVIX_CPU_ORACLE_LIMIT)
	{
		fprintf(stderr, "oracle: trace ended at line %u\n", line_number);
		goto done;
	}
	printf("oracle: first %u instruction boundaries matched\n", line_number);
	result = 0;

done:
	fclose(trace);
	drgqst_rom_release(&image);
	return result;
}
#endif

int main(int argc, char **argv)
{
#ifdef XAVIX_CPU_ROM_ORACLE
	if (argc == 3)
		return run_rom_oracle(argv[1], argv[2]);
#else
	(void)argc;
	(void)argv;
#endif
	RUN_TEST(test_reset_and_boot_prefix());
	RUN_TEST(test_far_call());
	RUN_TEST(test_data_bank_and_zero_page());
	RUN_TEST(test_pointer_register());
	RUN_TEST(test_nmi_and_rti());
	RUN_TEST(test_decimal_adc());
	RUN_TEST(test_all_opcodes_dispatch());
	puts("xavix_cpu_test: all tests passed");
	return 0;
}
