# `tom_dpgm` investigation

The ROM used for this investigation was user-supplied and opened read-only.
It is not included in the source tree, build output, or this note. The MAME
comparison is fixed to revision
`cf2d7a9be259552a3b493156e50e9149b6856448`.

## Identity and board profile

- Exact image: 4,194,304 bytes, CRC32 `1dc181b3`, SHA-1
  `fa30069d17705f27e4ff45e7f6ccf06986e138f3`.
- MAME machine configuration: `xavix2000_i2c_24c08_4mb`, input ports
  `ttv_lotr`, state class `xavix_i2c_lotr_state`, and `init_xavix`.
- MAME status at the fixed revision: `MACHINE_NOT_WORKING` and
  `MACHINE_IMPERFECT_SOUND`.

## Standalone model

The title has an append-only profile combining an exact 24C08 EEPROM model,
the observed `$00,$02,$06,$04` optical-acquisition synchronization phases,
and the generic sensor write strobe. It does not install another game's
sprite-address cursor watch, patch guest code, force a PC, skip a timer, or
introduce a title-specific state transition.

## Natural-frame verification

- 600 frames reached the Disney Interactive logo.
- 1,800 frames reached the star-field heart and wand sensor tutorial.
- 3,600 frames reached a four-heart sensor exercise.
- Continuous circular host motion advances to the in-game `スティックを
  ハートにあわせてね` alignment tutorial through the unmodified optical
  acquisition path.
- The CPU remained active and the 3,600-frame run produced 5,760,000 stereo
  samples, with 219,016 nonzero samples and peak magnitude 5,721. This proves
  PCM activity, not audio accuracy.
- ROM-independent tests cover exact identity, 24C08 selection, synchronization
  order, generic sensor strobe, disabled internal-cursor watch, state restore,
  and independent EEPROM/runtime-state filenames.

## Current limitations

The original reflector geometry and optical timing, later gameplay, audio
accuracy, and a complete play-through remain unverified. The title is therefore
classified **Experimental**.
