# `epo_bowl` investigation

The ROM used for this investigation was user-supplied and opened read-only.
It is not included in the source tree, build output, or this note. The MAME
comparison is fixed to revision
`cf2d7a9be259552a3b493156e50e9149b6856448`.

## Identity and board profile

- Exact image: 2,097,152 bytes, CRC32 `d34f8d9e`, SHA-1
  `ebe3792172dc43904b9226beb27f1da89d2388cc`.
- MAME machine configuration: `xavix2000_i2c_24c04_2mb`, input ports
  `epo_bowl`, state class `xavix_i2c_state`, and `init_xavix`.
- MAME status at the fixed revision: `MACHINE_NOT_WORKING` and
  `MACHINE_IMPERFECT_SOUND`.
- The 2 MiB image is mirrored through the XaviX 2000 8 MiB external address
  window. XaviXEmu already implements this mapping as modulo-ROM addressing;
  the core-size whitelist was extended only for the exact 2 MiB board size.
- The serial device is a 24C04 on P1.3/P1.4. `epo_bowl` receives separate
  EEPROM and runtime-state identities so its files cannot collide with other
  24C04 games.

## First blocking problem

- Observed symptom: a plain XaviX 2000 plus 24C04 profile rendered the safety
  warning and title, then stopped making meaningful progress while the CPU
  itself remained running.
- PC/address: the downloaded acquisition routine repeatedly executed around
  `$001915-$00192b`; the first permanent wait settled at `$00191a-$00191d`.
- I/O/register: the loop waits for ordered transitions of IN1 bits `$02` and
  `$04`, then selects ADC channel 0 through `$7b81` and reads `$7b80` in a
  32-by-32 scan. A later pass changes the illumination output and repeats the
  acquisition.
- Control result: with a plain 24C04 profile, an 1,800-frame probe recorded
  34,298,096 reads of `$7a01`, only 565 earlier ADC reads, PC `$00191a`, and
  IRQ source `$00`. The stable frame hash and inactive IRQ rule out an IRQ
  storm as the first standalone blocker.
- Hypothesis: the bowling accessory supplies the same two-bit quadrature-like
  camera synchronization sequence used by the existing synthetic optical
  primitive, followed by reflected-light ADC data.

## Non-PC A/B experiment

- Experiment A: retain the plain 24C04 wiring. The firmware remains in the
  `$00191a` wait described above.
- Experiment B: without changing guest PC, RAM, opcodes, timers, or checks,
  attach the existing phase sequence `$00,$02,$06,$04`, the generic 32-by-32
  sensor ADC, and 24C04 line handling.
- Result: the same 1,800-frame probe naturally leaves `$00191a`. It remains
  active near the ordinary `$0023fd-$00240e` scheduler loop, renders an
  animated title, performs 1,331,829 ADC reads (including reflected samples),
  and keeps IRQ source `$00`.
- Conclusion: missing IN1 synchronization and sensor acquisition is the exact
  first blocker. The permanent correction is a dedicated
  `DRGQST_CORE_EPO_BOWL_SENSOR_24C04` profile. The generic 24C04 profile stays
  unchanged for boards with plain digital input.

## Permanent-profile verification

- 600 frames: Japanese safety warning, CPU active, PC `$0023fd`, IRQ `$00`.
- 1,800 frames: animated `Excite Bowling` title, CPU active near
  `$0023fd-$00240e`, IRQ `$00`, and 1,331,829 ADC reads.
- 3,600 frames: sensor-driven mode-selection screen, PC `$0023ff`, IRQ `$00`,
  8,383,185 IN1 reads, and 2,026,953 ADC reads, with no stopped state or IRQ
  lock. The probe's documented calibration trajectory supplied the synthetic
  reflector movement; this is not a claim that GUI controls are complete.
- IN0 pulse sweep at the mode menu: bit `$01` confirms and enters character
  selection; bits `$08` and `$10` visibly move through the menu. The opposite
  direction bits had no visible effect from the initial upper-left item, and
  bit `$02` had no visible effect at this checkpoint. These observations match
  the published active-high direction/button wiring but are not sufficient to
  assign final host controls.
- Automated tests cover exact 2 MiB acceptance, rejection of unsupported
  1 MiB images, external-ROM mirroring, the four-state sync sequence, 24C04
  device selection, independent persistence filenames, and live phase reset
  after F7 state restoration.

## Current milestone and limitations

The first blocking hardware behavior is corrected without a PC-specific or
game-specific check bypass. Safety, title, and mode-menu rendering are
reproducible, so the game is classified **Experimental**. GUI control mapping,
the physical bowling sensor geometry and timing, gameplay, audio accuracy, and
a complete play-through remain unverified.
