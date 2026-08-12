# `epo_es2j` investigation

The ROM used for this investigation was user-supplied and opened read-only.
It is not included in the source tree, build output, or this note. The MAME
comparison is fixed to revision
`cf2d7a9be259552a3b493156e50e9149b6856448`.

## Identity and board profile

- Exact image: 4,194,304 bytes, CRC32 `840aecb1`, SHA-1
  `ad52449ffc13af5f4c67b2c3cf438e7ecd80b9fb`.
- MAME machine configuration: `xavix2000_4mb`, generic `xavix` input ports,
  base XaviX state, and `init_xavix`.
- MAME status at the fixed revision: `MACHINE_NOT_WORKING` and
  `MACHINE_IMPERFECT_SOUND`.
- The 4 MiB image is mirrored over the 8 MiB external bus. MAME declares no
  serial EEPROM, parallel NVRAM, optical sensor, or game-specific state class.

## Hardware isolation

The standalone default profile includes the Dragon Quest 24C08 and synthetic
CU5501A sensor, so using that fallback would expose hardware this board does
not have. A separate plain XaviX 2000 profile was added instead.

P0 and P1 are ordinary digital inputs. MAME leaves the generic ANPORT
callbacks unbound, so reads resolve to `$ff` and guest writes are ignored.
The inherited active-high unused ADC ports resolve to `$00` after a conversion;
the reset latch remains `$ff` until the first conversion. The profile models
these values explicitly, without random input, I2C, synthetic sensor activity,
or durable EEPROM/NVRAM.

## Boot and input probes

- 600 natural frames: Japanese safety warning rendered, PC `$003d53`,
  framebuffer hash `d92eaf76ac21a101`, and the CPU remained active.
- 1,800 natural frames: JFA-branded title rendered, PC `$003d53`, framebuffer
  hash `465e41175235d19d`, and the CPU remained active.
- 3,600 natural frames: a black transition frame was captured while the CPU
  remained active; this was not a permanent stall.
- 4,200 natural frames: attract-mode football rendered, PC `$003d53`,
  framebuffer hash `56efdac8e558b1e7`, and the CPU remained active.
- Across 4,200 frames P0 and P1 were polled, ADC conversions settled to `$00`,
  and the disconnected optical sensor and EEPROM remained inactive.
- A probe-only `$01` P0 pulse at frame 1,650 entered the game-select menu.
  Single pulses of the other P0 bits at the same point produced the same final
  menu image. This establishes a digital select path but does not justify a
  complete host-control map.

## Audio and persistence

The ordinary frame audio path produced nonzero PCM without an audio-specific
change: 4,347,844 of 6,720,000 stereo samples were nonzero at 4,200 frames,
with peak magnitude 19,839. This proves activity, not hardware-accurate sound.

The board has no declared durable EEPROM or NVRAM. F5/F7 uses an independent
`epo_es2j-runtime-state.sav` identity so it cannot collide with another game.
ROM-independent tests cover path selection, payload validation, round-trip
loading, and reset behavior that does not preserve ordinary RAM as NVRAM.

## Current milestone and first remaining blocker

Exact ROM recognition and the missing plain XaviX 2000 board profile were the
standalone integration blockers. They are corrected without an opcode skip,
PC-specific bypass, or ROM patch. Correct safety, title, menu, and attract-mode
football graphics now render, and PCM activity is present.

No card-scanner error was observed in the natural 4,200-frame run. The first
remaining milestone is interactive menu/gameplay control and identification
of the card-scanner protocol when firmware requests it. Card input, control
mapping, audio accuracy, and a complete play-through remain unverified, so the
title is classified **Experimental**.
