// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Billy Jr. and contributors

#include "core/xavix_machine.h"

#include <stdio.h>
#include <string.h>

static int check(int condition, const char *message)
{
	if (!condition)
		fprintf(stderr, "FAIL: %s\n", message);
	return condition;
}

int main(void)
{
	uint8_t rom[0x10000];
	xavix_machine machine;
	int ok = 1;
	unsigned i;
	for (i = 0; i < sizeof(rom); ++i)
		rom[i] = (uint8_t)i;
	xavix_machine_init(&machine, rom, sizeof(rom));
	ok &= check(machine.state.main_ram[0] == 0xff, "power-on RAM fill");
	xavix_machine_write_low(&machine, 0x1234, 0x5a);
	ok &= check(xavix_machine_read_low(&machine, 0x1234) == 0x5a, "main RAM round trip");
	ok &= check(xavix_machine_read_low(&machine, 0x4012) == 0x21, "text-array nibble swap");
	xavix_machine_write_low(&machine, 0x6003, 0x81);
	ok &= check(machine.state.fragment_ram[3] == 0x81, "sprite x high mirror");
	ok &= check(machine.state.fragment_ram[0x403] == 1, "sprite x high plane");
	ok &= check(xavix_machine_read_external(&machine, 0x12abcd) == 0xcd, "ROM mirroring");
	machine.state.vector_enable = 1;
	machine.state.irq_vector[0] = 0x34;
	machine.state.irq_vector[1] = 0x12;
	ok &= check(xavix_machine_read_vector(&machine, 0xfffe) == 0x34, "programmable IRQ low");
	ok &= check(xavix_machine_read_vector(&machine, 0xffff) == 0x12, "programmable IRQ high");
	xavix_machine_write_low(&machine, 0x7c01, 1);
	xavix_machine_write_low(&machine, 0x7c02, 0);
	xavix_machine_write_low(&machine, 0x7c00, 0x41);
	xavix_machine_advance(&machine, 4);
	ok &= check(machine.state.peripherals.timer.current_value == 0xff, "timer underflow");
	ok &= check((machine.state.irq_source & 0x10) != 0, "timer IRQ");
	xavix_machine_trigger_ioevent(&machine, 0x01);
	ok &= check(!(machine.state.ioevent_active & 0x01), "disabled IO event ignored");
	xavix_machine_write_low(&machine, 0x7a80, 0x01);
	xavix_machine_trigger_ioevent(&machine, 0x01);
	ok &= check((machine.state.ioevent_active & 0x01) != 0, "IO event latched");
	ok &= check((machine.state.irq_source & 0x08) != 0, "IO event IRQ");
	xavix_machine_write_low(&machine, 0x7a81, 0x01);
	ok &= check(!(machine.state.ioevent_active & 0x01), "IO event acknowledged");
	ok &= check(!(machine.state.irq_source & 0x08), "IO event IRQ cleared");
	return ok ? 0 : 1;
}
