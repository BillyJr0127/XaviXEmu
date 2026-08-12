# `epo_mini` investigation

The ROM used for this investigation was user-supplied and opened read-only.
It is not included in the source tree, build output, or this note. The MAME
comparison is fixed to revision
`cf2d7a9be259552a3b493156e50e9149b6856448`.

## Identity and board profile

- Exact image: 4,194,304 bytes, CRC32 `2adb01ee`, SHA-1
  `987218b6799195ba15adf39885c1d177c381ec26`.
- MAME machine configuration: `xavix2000_i2c_24c08_4mb`, input ports
  `ttv_lotr`, state class `xavix_i2c_lotr_state`, and `init_xavix`.
- MAME status at the fixed revision: `MACHINE_NOT_WORKING` and
  `MACHINE_IMPERFECT_SOUND`.
- The MAME source notes that a timer workaround was needed there. XaviXEmu
  reaches the milestones below through its normal CPU and machine execution;
  no timer, PC, opcode, RAM, interrupt, or ROM bypass was added for this title.

## Standalone model

The title reuses the isolated 24C08 sensor board profile first verified for
`tom_dpgm`: exact 24C08 I2C behavior, deterministic optical-acquisition
synchronization, and the generic sensor write strobe. It has separate
`epo_mini-eeprom.sav` and `epo_mini-runtime-state.sav` identities. The Windows
front end deliberately supplies no speculative host input mapping or crosshair.

## Natural-frame verification

- 600 frames reached the Japanese safety/controller warning with the CPU
  active.
- 1,800 frames reached the correct colorful animated title screen with the CPU
  active.
- A 3,600-frame run remained in the correct animated title sequence without a
  stopped CPU state.
- PCM output was nonzero, proving audio activity but not timing, pitch, mixing,
  or hardware accuracy.
- The blank 24C08 image was initialized by the guest software. ROM-independent
  tests verify the exact ROM identity and separate EEPROM/runtime-state paths.

## Current limitations

Host input mapping, the original sensor behavior, gameplay, audio accuracy,
and a complete play-through remain unverified. The title is therefore
classified **Experimental**.
