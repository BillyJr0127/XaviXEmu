# `epo_ebox` investigation

The ROM used for this investigation was user-supplied and opened read-only.
It is not included in the source tree, build output, or this note. The MAME
comparison is fixed to revision
`cf2d7a9be259552a3b493156e50e9149b6856448`.

## Identity and board profile

- Exact image: 4,194,304 bytes, CRC32 `e25ae4f5`, SHA-1
  `7f7b613f0ab8f43f5cad0d13de538921e77cae9c`.
- MAME machine configuration: `xavix2000_4mb_nv`, input ports `epo_ebox`,
  base XaviX state, and `init_xavix`.
- MAME status at the fixed revision: `MACHINE_NOT_WORKING` and
  `MACHINE_IMPERFECT_SOUND`.
- The 4 MiB image is mirrored over the 8 MiB external bus. Internal RAM
  `$3000-$3fff` is 4 KiB of battery-backed parallel NVRAM initialized to
  `$ff`; there is no serial EEPROM.
- IN0 is active high: `$01` select, `$02` back, `$10/$20` up/down, and
  `$40/$80` left/right. Bits `$04/$08` are not assigned to host controls.

## Hardware isolation

The existing Super Dash Ball profile also has parallel NVRAM, but its four
ANPORT channels are controller inputs with game-specific conversion. Excite
Boxing does not use that controller. A generic parallel-NVRAM profile was
therefore added instead of extending the SDB special case.

MAME leaves Excite Boxing's ANPORT callbacks unbound, so they resolve to
`$ff` and ANPORT writes call a no-op callback. Its ADC callbacks are connected
to the inherited unused analog input ports, whose active-high default is
`$00`. XaviXEmu models both cases explicitly so this profile cannot fall
through to writable machine latches or the Dragon Quest synthetic optical
sensor.

## Boot and input probes

- 600 natural frames: EPOCH logo, PC `$0038fc`, framebuffer hash
  `00ad88ca195202cd`, IRQ source `$00`, and the CPU remained active.
- 1,800 natural frames: animated Excite Boxing title/menu, PC `$0038fc`, hash
  `21ebef7b616fb2cd`, IRQ source `$00`, and the CPU remained active.
- 3,600 natural frames: the title/menu continued animating, PC `$0038fa`, hash
  `21ebef7b616fb2cd`, IRQ source `$00`, and the CPU remained active.
- Across 3,600 frames ADC reads returned `$00`, the optical sensor stayed
  inactive, and firmware initialization changed 2,179 bytes of the parallel
  NVRAM image.
- At the title, `$01` entered character selection and `$02` retained/returned
  to the title. After a diagnostic select prelude, each documented direction
  bit reached the level-selection screen; `$02` returned to the title. These
  pulses were probe-only experiments and did not add a firmware bypass.

## Audio and persistence

The ordinary frame audio path produced nonzero PCM without an audio-specific
change: 1,740,296 of 2,880,000 stereo samples were nonzero at 1,800 frames,
with peak magnitude 30,089. This proves activity, not hardware-accurate sound.

Excite Boxing has separate NVRAM and runtime-state persistence identities.
Tests cover the exact 4,096-byte NVRAM payload, rejection of the wrong size,
reset preservation, changed-byte generation tracking, distinct filenames,
and the same durable-storage routing used when F7 restores a runtime state.

## Current milestone and first remaining blocker

No CPU, timer, memory-bank, video, input-polling, ADC, or ANPORT blocker was
observed before the title, character selection, or level selection. Exact ROM
recognition and the missing generic parallel-NVRAM board profile were the
standalone integration blockers; both are corrected without a PC-specific
skip or game-data patch.

The title is classified **Experimental**. The first remaining milestone is a
verified transition into normal boxing gameplay and validation of control
feel. Audio accuracy and a complete play-through also remain unverified.
