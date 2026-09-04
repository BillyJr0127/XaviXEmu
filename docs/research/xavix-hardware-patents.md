# XaviX and XaviX 2000 patent map

This note records primary patent evidence for the early SSD/XaviX hardware and
connects it to reproducible emulator experiments. User-supplied ROM images
were opened read-only and are not included in this repository. A patent can
establish an architecture, but does not by itself prove that every described
option exists in every XaviX revision or reveal every memory-mapped bit.

## Generation boundary

The patents below describe the architecture associated with the original
XaviX and XaviX 2000 era. They must not be applied blindly to the later
SuperXaviX/XaviX 2 geometry engine.

- JP2000200178A explicitly names SSD's preferred high-speed processor,
  scanning image generator, and colour-video encoder. This is the strongest
  primary-source bridge between the individual circuit patents and SSD's
  integrated home-TV information-processing platform.
- JP2008505386A is a later compatible-processor patent with bitmap, pixel
  plotter, and more explicit graphics bus requestors. It is useful supporting
  evidence for later compatibility, not proof that early XaviX 2000 contains
  every later block.

## Primary sources and confidence

| Patent family | What the primary source establishes | Confidence for early XaviX |
| --- | --- | --- |
| [JP3557067B2 / JPH10222151A](https://patents.google.com/patent/JP3557067B2/ja), [US6043811A](https://patents.google.com/patent/US6043811A/en) | Raster position generator; text and sprite object generators; a circular pixel buffer smaller than a frame and potentially smaller than a scanline; per-pixel code and depth; palette transparency; variable character formats; optional rotation, enlargement, and reduction. | High. JP2000200178A names this scan-image-generator family directly. |
| [JP2000200178A](https://patents.google.com/patent/JP2000200178A/ja) | The SSD home-TV platform and its drivers. The sprite driver controls coordinates, character number, priority, colour, transfer, variable size, enlargement/reduction, and rotation. It identifies the preferred processor, image generator, and video encoder patent families. | High as the integration link; medium for exact register meanings. |
| [US6070205A / TW448363B](https://patents.google.com/patent/TW448363B/en) | A single-chip processor with multiple independent buses, multiple bus masters, and independent arbitration. | High for the system architecture; low for exact contention timing until measured. |
| [US7561931B1](https://patents.google.com/patent/US7561931B1/en) | A PCM sound bus master, 16 time-multiplexed channels, four DAC groups, waveform and envelope fetches, attack/loop playback, pitch accumulation, four interrupt timing sources, and programmable DAC output timing. | Very high. Its 16 channels and 384-byte, 24-byte-per-channel local state align unusually closely with the observed sound block. |
| [JP2008505386A](https://patents.google.com/patent/JP2008505386A/en) | Later compatible processor arbitration names background arrays 1/2, sprite DMA, character headers/data, bitmap data, waveform/envelope data, DMA, and a pixel plotter as bus request sources. | Supporting evidence only for early hardware; high relevance to later XaviX-compatible generations. |

## Graphics model implied by the patents

The early graphics block is not a conventional full-frame framebuffer. It
generates text/tile and sprite objects around the current raster position,
converts them to pixels, compares their depth, writes colour codes into a
small circular pixel buffer, then converts the winning codes through the
palette as the raster advances. Buffer positions are cleared after output.

Consequences for emulation:

1. A single render using only end-of-frame register values is not always
   equivalent to the hardware. A game can change scroll, palette, sprite DMA,
   or object parameters at a programmed raster position.
2. Missing competitors or scenery can result from timing or object-window
   selection even when the sprite data itself is valid.
3. Equal-depth tie order, transparent palette entries, and the circular
   left/right edge must be reproduced precisely. A normal painter's
   algorithm can be subtly wrong.
4. Enlargement, reduction, and rotation are native object features described
   by the family. Their existence does not identify all XaviX register bits;
   those still require game traces and controlled tests.

The current core already has corresponding storage and paths for fragment
RAM ($6000-$67ff), palette RAM ($6800-$69ff), segment registers
($6a00-$6a1f), two tile register blocks ($6fc8-$6fd7), sprite control
($6fd8), sprite DMA ($6fe0-$6fe6), and per-pixel priority. The important
remaining mismatch is no longer a single final snapshot for programmed
position interrupts: those frames are now composed in vertical ranges. Exact
horizontal-position splitting, writes not synchronized by that interrupt, and
the patent's finite circular-buffer edge behavior remain to be implemented.

## Confirmed position-interrupt correction

The machine already stored the programmed display position at $6ffa/$6ffb,
recognized enable bit $6ff8.4, and implemented acknowledgement source
$40, but it never generated the interrupt. The core now converts the
native 256-by-256 raster position into the current frame cycle budget and
latches source $40 when execution crosses it. A synthetic test verifies
one acknowledged interrupt per frame and repetition on the next frame.

An exact A/B probe of tak_chq at frame 3,600 changed only this interrupt:

| Probe | Final PC | Position registers | Frame hash | Visible result |
| --- | --- | --- | --- | --- |
| Position IRQ disabled | $02d442 | $68/$61 | 7df1879659ab1ed8 | Distant scenery and a dark/blank racing surface; no road. |
| Position IRQ enabled | $02d450 | $68/$00 | f0ffe6705fca3a36 | Grey road, lane markings, and red/white shoulder return in the upper part of the racing view. |

The current MAME machine implementation independently confirms the direct
native x/y position interpretation and updates the screen before asserting
the position interrupt. A focused write trace then established why a naive
screen-space split still failed: Choro-Q replaces tile generator 0 at y=$61,
but leaves tile generator 1 and sprite DMA unchanged. The replacement road
band must begin with its own local source row rather than continue at absolute
screen y.

The range renderer now preserves emitted pixels, restarts only a changed tile
generator at the interrupt boundary, and keeps unchanged generators in
absolute coordinates. The frame-3,600 result is `f5376c8f5d4dab9a`: sky and
city occupy the upper band, the perspective road continues from the horizon
to the bottom, and the car, track marker, and HUD remain visible. An initial
ttv_mx boot/title probe remains byte-identical, which is expected because it
does not reach a comparable split gameplay scene.

This is a controlled graphics correction, not yet a complete motorcycle
renderer. The idle Choro-Q frames are stable but do not prove road motion;
sustained wheel/throttle input and opponent-car visibility remain to be
verified.

## Sound model implied by US7561931B1

The patent is a close structural match to $7400-$757f: its 16 voices times
24 bytes of local working state total $180 bytes, exactly the size of that
memory-mapped region. The emulator separately reads 16-byte-per-voice
parameter descriptors from paged main RAM and keeps expanded host-side voice
state. It describes:

- four groups of four time-multiplexed channels;
- waveform address, loop address, bank/cache, pitch accumulator and fraction;
- left/right envelope addresses and current state;
- an initial attack array followed by a second array that can loop after its
  marker;
- four independently timed interrupt sources;
- channel volume, envelope, waveform midpoint subtraction, and cascaded DAC
  multiplication;
- programmable mute/gap and lead/lag between physical DAC time slots to
  reduce crosstalk.

The emulator already implements 16 voices, waveform/loop addressing, pitch,
envelopes, the four tempo/IRQ groups, $75fe acknowledgement, mixing, and
host-rate conversion. The patent therefore gives us a much stronger test
oracle than guessing from isolated songs, but it does not justify injecting
analogue noise or deleting digital samples. The DAC gaps primarily describe
physical output settling and crosstalk.

The next safe sound experiments are:

1. Trace each channel from attack to second-array marker and loop, including
   the four waveform types, without changing marker semantics first.
2. Measure all four tempo interrupt sources and $75fe acknowledgement
   against CPU-cycle timestamps.
3. Compare pitch-accumulator and envelope transitions against known ROM
   waveform bytes one channel at a time.
4. Model DAC lead/lag only if a hardware recording or register trace shows an
   audible digital consequence; otherwise keep the clean digital mix.

## Prioritized graphics experiments

1. Map wheel and throttle controls, then capture a sustained moving Choro-Q
   race with frame-to-frame road and opponent-object evidence.
2. At the moment an NPC disappears, capture sprite DMA source/destination,
   the relevant object entries, character headers, depth, and palette index.
   This separates missing data from a render-window or priority failure.
3. Extend the verified vertical position-interrupt split to horizontal x
   boundaries and timestamped palette/sprite changes. Choro-Q's recovered
   full-height road remains the first regression gate.
4. Test equal-depth ties, transparent palette writes, wrapped left/right
   objects, and scaled/rotated objects as isolated video-core fixtures.
5. Apply later compatible-processor bus priorities only after a trace proves
   contention affects a title. Do not copy the later JP2008505386A priority
   table into early XaviX 2000 by assumption.

## What is established and what is not

Established by primary documents and controlled probes:

- the early SSD graphics design is raster/object based with a circular
  code/depth pixel buffer;
- text/tile and sprite pixels share depth comparison and palette transparency;
- the integrated platform has object scaling/rotation capabilities;
- the sound processor is a 16-channel PCM bus master with attack/loop,
  pitch/envelope, four timing groups, and multiplexed DAC output;
- generating the missing programmed position IRQ changes real Choro-Q
  gameplay and recovers road graphics.

Not yet established:

- the exact latch time of graphics writes outside the programmed position
  interrupt;
- which scaling/rotation bits and formats are implemented in each silicon
  revision;
- exact bus-contention penalties;
- the cause of every missing motorcycle NPC;
- whether physical DAC settling controls should alter a clean digital export.