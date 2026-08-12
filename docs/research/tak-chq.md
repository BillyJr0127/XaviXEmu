# `tak_chq` investigation

The ROM used for this investigation was user-supplied and opened read-only.
It is not included in the source tree, build output, or this note. The MAME
comparison is fixed to revision
`cf2d7a9be259552a3b493156e50e9149b6856448`.

## Identity and board profile

- Exact image: 4,194,304 bytes, CRC32 `ffd2eb95`, SHA-1
  `a30884da5554483ebfd0009cf5dd1768be8a99cb`.
- MAME machine configuration: `xavix2000_i2c_24c04_4mb`, input ports
  `xavix_i2c`, state class `xavix_i2c_state`, and `init_xavix`.
- MAME marks it not working with imperfect sound and notes that a CPU car
  crashes when it reaches a corner.
- XaviXEmu reuses its generic 4 MiB XaviX 2000 plus 24C04 profile and gives
  this title separate EEPROM and runtime-state identities.

## Natural boot milestone

- 600 frames: animated racing attract scene; PC `$009547`, frame hash
  `ff0c7cce9b8b8988`.
- 1,800 frames: complete title; PC `$009544`, frame hash
  `bc2910876de6f822`.
- 3,600 frames: race HUD and track map; PC `$02d442`, frame hash
  `7df1879659ab1ed8`. The CPU remained active with no IRQ lock.
- The 3,600-frame run produced 5,760,000 stereo PCM samples, 4,761,340 of
  them nonzero, with peak magnitude 11,698. This proves an active path, not
  accurate pitch, tempo, or mixing.
- It read P0 `$7a00` 7,015 times (last PC `$009560`), P1 `$7a01` 936,829
  times (last PC `$009565`), ANPORT2 `$7b10` and ANPORT3 `$7b11` 3,507 times
  each (last PCs `$00954f/$009554`), and ADC `$7b80` 1,754 times. No
  `$7a80/$7a81` I/O-event polling appeared.

## Input probes

- A complete active-high P0 sweep at the title found `$20` advances into a
  race and `$80` opens car select. Bits `$01/$02/$04/$08/$10/$40` did not
  advance the title.
- From car select, `$10` returns to the title and `$20` confirms. Other P0
  bits did not visibly change the selected car.
- Fixed `$40/$80/$c0` values on ANPORT2 visibly select different player-one
  cars. The same values on ANPORT3 did not alter player one's screen, which is
  consistent with a separate second-player wheel.
- P1 bits `$08/$10` were not swept because they are the live 24C04 SDA/SCL
  lines. No production host mapping is inferred from these probes.

## Current blocker and scope boundary

The first standalone usability blocker is input presentation: the Win32 front
end does not yet expose the two physical wheel channels and their accelerator,
brake, and command controls. Boot, video, EEPROM, CPU execution, and PCM all
pass this first milestone, but diagnostic input is not a playable GUI mapping.

The reported CPU-car corner failure has not yet been reproduced with a
controlled sustained-race trace. No CPU opcode, including ASR, was changed.
The next investigation must map the controls, hold acceleration through a
corner, and capture exact PC, operands, and I/O before considering a CPU fix.

The title remains **Experimental**: sustained gameplay, controller feel, the
corner failure, audio accuracy, and a complete play-through are unverified.
