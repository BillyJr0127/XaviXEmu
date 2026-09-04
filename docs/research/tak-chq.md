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

## Patent-guided display-position interrupt

The early SSD graphics patents describe a raster-position-driven circular
pixel buffer rather than a conventional final-state framebuffer. The machine
already exposed $6ffa/$6ffb, enable bit $6ff8.4, and interrupt source
$40, but the core did not generate that programmed display-position
interrupt.

An exact A/B probe at frame 3,600 changed only that interrupt. With it
disabled, the frame ended at PC $02d442 with hash 7df1879659ab1ed8 and no
visible road. With it enabled, the frame ended at PC $02d450 with hash
f0ffe6705fca3a36; the grey road, white lane markings, and red/white shoulder
returned in the upper part of the racing view. A focused synthetic test also
verifies one acknowledged position interrupt per frame.

A later read/write trace identified the remaining straight-road failure. The
handler reads current raster Y from `$6ffb`, indexes horizontal and vertical
scroll tables at `$28f1,Y` and `$29f1,Y`, writes `$6fcc/$6fcd`, increments Y,
and stores it back to `$6ffb` to schedule the following scanline. The emulator
implemented writes to `$6ffa/$6ffb` but returned `$ff` on reads, so every
handler indexed entry `$ff` and wrapped the next position to zero.

The position registers now read back their programmed values. At frame 3,600,
Y advances continuously from `$61` through `$af`, while horizontal scroll
changes through values including `$b3`, `$c1`, `$cf`, and `$d9`. Scroll-only
changes are rendered against the absolute hardware raster line, matching
MAME's `drawline = line + scrolly` formula. This activates the road's intended
per-scanline perspective and bends instead of leaving it as one straight
tilemap.

The renderer preserves already emitted scanlines at a position interrupt but
does not invent a local vertical origin. For the first road line, native raster
`$62` plus scroll `$9f` wraps to source line `$01`; subtracting a segment
origin instead selects `$a0`, removes the road, and repeats HUD tiles below it.
The absolute-raster result at frame 3,600 has hash `a5343dafd208448d` and keeps
the curved road, red/white shoulder, player car, track marker, and dashboard in
their correct regions. The saved result is
`diagnostics/tak-chq-hardware-scroll-3600.png`.

The readback path is covered by a machine-register test and a synthetic core
test that reschedules a second position interrupt on the next scanline in the
same frame. A fresh 7,200-frame demo also completes without a CPU stop. See
[the patent map](xavix-hardware-patents.md) for the primary sources and
remaining experiment order.

## CPU-car corner freeze

The natural demo reproduced the reported corner failure. From frame 3,600
through frame 7,200, PC remained at `$02d450`, frame hash remained
`f5376c8f5d4dab9a`, and tracked RAM writes stopped. The loop performs a binary
midpoint using J/K; applying accumulator BCD correction while D is set makes
the midpoint equal the upper bound, so the search never narrows.

Keeping J/K/L/M ADC/SBC binary crosses that loop and exposes a second failure
at `$00a97f`. Tracing the command-ring producer and consumer shows both using
ordinary accumulator ADC/SBC while D remains set. BCD correction moves the
intended `$0fxx` ring endpoints into invalid `$13xx/$14xx` addresses and the
consumer eventually stalls on byte `$a8` at `$0996`.

The SSD 2000 result is broader than an extended-register exception: D is an
architecturally preserved flag, including across interrupts, but ADC/SBC do
not apply BCD correction. This is configured only for XaviX 2000 profiles;
earlier XaviX profiles retain verified 6502-style decimal arithmetic. Unit
tests cover both CPU families, reset persistence, and interrupt/RTI flag
preservation. With binary SSD 2000 arithmetic, a fresh 7,200-frame demo run
finishes with `stopped=0`, continuous RAM activity, and changing frame hashes
instead of either stable loop.

## GUI control mapping and remaining scope

ANPORT2 and ANPORT3 now receive player-one and player-two wrapping wheel
counters rather than ordinary absolute ADC positions.
P0 `$20`, `$10`, and `$80` are mapped as accelerator/confirm, brake/cancel,
and command/item respectively, matching both the controlled probes and the
three buttons described for the original grip. Keyboard steering now ramps
slowly across a centred virtual range and returns to centre on release; that
range becomes approximately `$bf/$3f` around the hardware's `$ff` wrap point.
Mouse steering is relative and half as sensitive while accelerator or brake is
held, so reaching a window edge cannot leave the wheel pinned. PS/gamepad axes
use a squared centre-precision curve over that same virtual range: a small
stick tilt makes a small correction, while full tilt still reaches full useful
lock. A connected DualSense was verified through the same WinMM axis path used
by the application.

The shared steering bias exposed the counter encoding. MAME's unimplemented
base ANPORT callback returns `$ff`, while the GUI had treated `$80` as the
physical centre. A controlled player-race probe kept `$ff` until frame 3,000:
remaining at `$ff` left the car straight, then changing to `$3f` or `$bf`
visibly turned it in opposite directions. The host therefore retains an
ordinary centred virtual wheel for usability but translates it around the
hardware's `$ff` wrap point. This removes the initial `$ff -> $80` half-turn
that biased keyboard, mouse, and gamepad control alike.

The title remains **Experimental**: the long automated corner regression and
host input paths are verified, but sustained manual play, original-controller
feel, audio accuracy, and a complete play-through remain unverified.
