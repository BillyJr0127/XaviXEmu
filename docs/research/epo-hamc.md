# `epo_hamc` investigation

The ROM used for this investigation was user-supplied and opened read-only.
It is not included in the source tree, build output, or this note. The MAME
comparison is fixed to revision
`cf2d7a9be259552a3b493156e50e9149b6856448`.

## Identity and board profile

- Exact image: 4,194,304 bytes, CRC32 `b1177813`, SHA-1
  `ed01096ebb63b72267ad7e0b2115224bbab64011`.
- MAME machine configuration: `xavix2000_4mb`, input ports `epo_hamc`, state
  class `xavix_epo_hamc_state`, and `init_xavix`.
- MAME status at the fixed revision: `MACHINE_NOT_WORKING` and
  `MACHINE_IMPERFECT_SOUND`.
- No serial EEPROM or parallel NVRAM is declared for this board.

## First blocking problem and correction

The first standalone blocker was exact-image recognition. After recognition,
the first required hardware path is the optical acquisition loop around ROM
addresses `$0517c0-$05186f`: firmware waits for ordered IN1 `$02`/`$04`
transitions, selects ADC channel 0 through `$7b81`, reads `$7b80`, and repeats
the scan with different illumination output. A fixed generic input would not
satisfy that protocol, while MAME's random placeholder is not a hardware
model.

The correction is an append-only dedicated no-EEPROM profile. It supplies the
deterministic synchronization phase order `$00,$02,$06,$04` needed for the
observed edge waits. ADC reads currently return a neutral zero baseline;
there is not yet enough evidence to model the original gloves' reflectance
geometry, illumination polarity, or electrical timing. ANPORT remains open
bus. No guest PC, opcode, RAM, timer, interrupt, or ROM check is skipped or
patched.

## Verification

- 600 natural frames reached the correct EPOCH logo with the CPU active.
- 1,800 natural frames reached the correct animated `Ham Ham Dai Circus`
  title and copyright screen with the CPU active.
- A separate 3,600-frame run with the same synchronization model remained in
  the animated title sequence without entering a stopped CPU state.
- IRQ source remained `$00`, no stopped state occurred, and durable-storage
  bytes remained unchanged in all probes.
- The 3,600-frame run produced 5,760,000 stereo samples, of which 4,402,279
  were nonzero, at peak magnitude 10,059. This proves PCM activity, not exact
  sound.
- ROM-independent tests cover exact metadata, synchronization order,
  no-EEPROM behavior, open ANPORT, neutral ADC reads, host-pointer isolation,
  save-state synchronization reset, and independent runtime-state identity.

## Current milestone and limitations

Exact recognition and the synchronization behavior needed to reach correct
title graphics are implemented without a game-code bypass. The physical glove
protocol, useful non-neutral ADC samples, illumination behavior, geometry,
and host controls are not yet implemented. Interactive gameplay, audio
accuracy, and a complete play-through remain unverified, so the title is
classified **Experimental**.
