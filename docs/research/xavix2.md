# XaviX 2 / `ban_naru` investigation

## Scope

- ROM: `ban_naru.zip`, opened read-only from the user's `Games` directory.
- Expected image: 8 MiB, CRC32 `e3465ad2`, SHA-1
  `13e3d2de5d5a084635cab158f3639a1ea73265dc`.
- Architecture: XaviX 2 RISC CPU, reset PC `$40000000`; this is not the
  6502-derived XaviX 2000 core used by the six playable games.
- The ROM ZIP was not modified, renamed, copied, committed, or packaged.

## Boot probe implemented

- Observed symptom: the existing standalone rejected the image, and feeding it
  to the XaviX 2000 core would execute the wrong CPU architecture.
- PC/address: reset vector `$40000000` jumps to `$40000020`.
- Hypothesis: a framework-independent port of the current BSD-licensed MAME CPU
  plus the known memory map can identify the first real hardware dependency.
- Experiment: implement a standalone XaviX 2 CPU, RAM/ROM map, IRQ controller,
  DMA, PIO/24C08 and the experimental command-list GPU in an isolated probe.
- Result: the exact ROM verifies and boots without an unmapped read/write or an
  unimplemented opcode before the first blocking loop.  The GPU renders the
  XaviX logo and Japanese safety warning correctly.
- Code changed: `src/xavix2/xavix2_cpu.*`, `src/xavix2/xavix2_machine.*`, and
  `tests/xavix2_boot_probe.c`.  They began as an isolated probe and the proven
  frame/input path is now linked into the GUI as experimental support.

## Frame wait initially mistaken for a blocker

- Observed symptom: after displaying the safety warning, execution spends
  nearly all running time in a three-instruction polling loop.
- PC/address: `$4001f2c0` through `$4001f2c5`; RAM byte `$00000c5c`.
- Instruction sequence: load unsigned byte from `$0c5c` into R0, test R0 by
  OR-ing it with itself, then branch back while zero.
- Relevant state: the preceding code enables interrupts (`$f9`), writes zero
  to `$0c5c`, and waits.  The vertical blank IRQ (level 7), DMA completion IRQ
  (level 12), PIO/24C08 traffic, and IRQ acknowledgement all continue working.
- Initial hypothesis: XaviX 2 has another timer/event source that invokes the
  firmware callback at about ROM `$1f325`, where code writes 1 to `$0c5c`.
- Experiment: raise each currently enabled IRQ level and separately apply a
  probe-only diagnostic pulse to RAM `$0c5c`.
- Result: the diagnostic `$0c5c = 1` pulse fast-forwards the loop and reaches
  the title, but a full timing run proved that the normal level-7 vertical blank
  callback also releases it once per frame.  At the configured 98 MHz CPU clock
  and 60 Hz video rate, an unmodified 3,000,000,000-byte-cycle run reaches the
  full title naturally.  `$0c5c` is a frame synchronization flag, not the first
  functional blocker.
- Permanent code changed: none.  The pulse is an opt-in boot-probe experiment,
  not part of `XaviXEmu.exe`.

## Second problem exposed after the diagnostic

- Observed symptom: after the unknown event is synthesized, execution reaches
  an opcode MAME also leaves unimplemented.
- PC/address: first observed at `$40055cf7`.
- CPU opcode: `$ff`, observed 37 times in the one-billion-byte-cycle title run.
- Result: the CPU continues far enough to render the title, but the opcode must
  be understood before claiming correct gameplay.

## Input/event investigation

- Observed symptom: after the title is reached, changing the raw PIO input does
  not wake the title task and no further PIO read occurs.
- Firmware addresses: the input sampler at `$1f6ab` reads PIO `$ffffe208`, masks
  it with `$005fffa0`, stores current state at RAM `$0da4`, rising edges at
  `$0da8`, and falling edges at `$0dac`.  This is a 15-effective-line parallel
  motion/sensor value rather than 32 independent ordinary buttons.
- Evidence: title/menu code at `$423f8` reads the rising-edge word and explicitly
  tests bit 16 and bit 19.  MAME currently labels those PIO lines B/Execute and
  Down, but the original product used two reflective gloves, so the remaining
  lines may encode optical position or phase rather than standalone buttons.
- Experiment: add probe-only per-frame sampling, input-event callback dispatch,
  precise IRQ injection, RAM/read tracing, and snapshot sweeps.  Every effective
  single line, every two-line combination, and all 210 ordered single-line
  transitions were tested from the same title snapshot.
- Result: IRQ levels 2 and 5 can reach the menu input reader under the diagnostic
  event path, proving the edge data is structurally valid.  None of the tested
  static or two-step values changes the title framebuffer, callback, or
  meaningful game RAM compared with the matched control.  A valid glove packet
  or missing sensor/timer event sequence is still required; arbitrary mapping
  to mouse buttons would currently be a game-specific guess.
- Related correction: IRQ8's prologue compares undocumented registers
  `$ffffe244` and `$ffffe24a`.  Treating every unknown MMIO byte as retained RAM
  made these registers diverge and trapped the handler.  Returning the current
  MAME-map value of zero for only those four bytes lets IRQ8 return normally.
- Code changed: callback scheduling, broad input sweeps and RAM dumps remain
  opt-in facilities in `xavix2-boot-probe`.  Only the later verified PIO edge
  sampler and motion packet path are used by the experimental GUI support.

## Motion packet and cursor mapping

- Observed symptom: raw PIO changes did not move the title cursor, but manually
  raising IRQ 10 entered a separate seven-byte input handler.
- PC/address: IRQ 10 code at `$00000486` copies `$000d-$0013` to `$0006-$000b`
  and `$0005`.  The game-side copier at `$4001f74f-$4001f76d` expands the six
  axis bytes to words at `$0dbc-$0dc6`.
- I/O/register: bytes 0-2 and 3-5 are two independent X/Y/Z samples, matching
  the product's two reflective wrist bands; byte 6 is a status value.
- Hypothesis: valid samples are positions from two optical reflectors rather
  than button codes.
- Experiment: inject one packet on each IRQ 10, sweep each axis, trace the
  histories at `$121a-$1279`, and compare framebuffer cursor positions.
- Result: each coordinate must be nonzero and below `$38`.  The first X byte
  moves the built-in cursor horizontally and the first Y byte moves it
  vertically.  At the title, packet `20 18 20 00 00 00 00` places the first
  hand on the Game Start target.  Approximate visible coordinates are
  `x = 6 * sample_x + 132` and `y = 334 - 4.33 * sample_y`.
- Code changed: probe-only IRQ 10 packet/sequence injection and focused RAM
  access traces.  No guessed game-specific input was added to the public GUI.

## First blocking CPU error

- Observed symptom: even with the built-in cursor visibly on Game Start, the
  target hit mask at `$6468` remained zero, so PIO Execute bit 16 could never
  activate the menu.
- PC/address: the hit-test routines at `$0004244d` and `$0004248b` end with
  opcode `$f7` for an in-bounds point and `$f6` for an out-of-bounds point.
  All twelve callers at `$0004218d-$000422cd` immediately use opcode `$da`
  to skip OR-ing the corresponding hit bit.
- Hypothesis: MAME's preliminary XaviX 2 core assigned `$f6/$f7` to the wrong
  status flag.  The firmware uses them as an explicit false/true result for the
  following not-equal branch.
- Experiment: compare an inside point `(sample_x=$20, sample_y=$18)` with an
  outside point, trace the returned flags, then model `$f6` as clear-Z and
  `$f7` as set-Z while leaving `$da` as branch-if-not-zero.
- Result: outside points now skip the hit-bit update, while the centered point
  changes `$6468` from 0 to 1.  Applying Execute bit 16 then advances from the
  Naruto title to the `メニューの巻` screen.  Treating `$da` itself as a V-flag
  branch was rejected: it traps the boot-time memory-clear loop at
  `$400006cd-$400006d3`.
- Code changed: the standalone XaviX 2 interpreter now makes `$f6` clear Z and
  `$f7` set Z, with a CPU regression test for both instructions.  The temporary
  alternate `$da` experiment was removed.

## GUI and story-path validation

- Observed symptom: the first GUI integration exposed a 640x400 command-list
  coordinate area with the game image centered inside large black borders.
- Experiment: compare non-background bounds across stable title/menu frames.
- Result: the actual output raster is the central 320x240 area.  Cropping that
  area produces the native 4:3 image without scaling away real pixels.
- Input experiment: drive the same video-frame API used by the GUI with a
  centered wrist packet and short Execute edges, then sweep the cursor target
  grid on the subsequent menus.
- Result: the scripted path entered `メニューの巻`, selected Story Battle,
  selected `はじめから`, confirmed Naruto, and reached the story/tutorial.
  The otherwise visually sparse start/continue page exposed distinct hit masks
  `$00000002` and `$00000004`, plus `$00010000` for Back, confirming that the
  built-in cursor and hit-test path continue to work beyond the title.
- Control evidence: the tutorial explicitly instructs the player to join both
  hands and swing them horizontally to guard.  The GUI therefore lets right
  mouse or Space add the second reflector at the first reflector's position;
  moving while held produces the required paired horizontal history.  Whether
  later techniques need separated independent hand positions remains open.
- Code changed: XaviX 2 is accepted by the GUI, displayed at 320x240, and fed
  one or two mouse-controlled wrist samples once per motion IRQ.  State and
  EEPROM menus remain disabled and no sound is claimed.

## Current milestone

The exact first gameplay blocker is identified and corrected: XaviX 2 opcodes
`$f6/$f7` operate on the zero flag, not the overflow flag as in the current
MAME implementation.  With periodic IRQ 10 motion samples, the built-in cursor
works and Execute enters the next menu.  The public GUI now has an experimental
path for `ban_naru`: the mouse supplies the first X/Y/Z reflector, holding the
right button supplies a second reflector at the same position, and the left
button drives the verified Execute PIO line.  A 2,000-frame end-to-end run
reached `メニューの巻` through this same frame/input API.  The GPU's active
image is the central 320x240 area of the larger command-list coordinate space,
so the GUI crops that area before applying its optional 4:3 presentation.

## Menu click sampling and first sound implementation

- Observed symptom: a short GUI click could move the built-in cursor correctly
  but fail to advance a menu, making the verified Naruto story path appear
  inaccessible.
- I/O involved: PIO Execute bit 16 is sampled at the 60 Hz video/input cadence.
- Hypothesis: a normal Windows mouse-down/up pair can both occur between two
  firmware input samples.
- Experiment: delay Execute by two video frames after positioning the cursor,
  then retain the edge for four frames even if the physical button was already
  released.
- Result: the mouse position is visible before Execute is asserted and a click
  can no longer collapse to a zero-frame pulse.  The existing scripted path
  still reaches the Naruto character screen and story tutorial.
- Code changed: the XaviX 2 GUI input path now contains only this short Execute
  latch; the motion coordinates and game hit testing remain firmware-driven.

- Observed symptom: XaviX 2 ran silently.  MAME's driver only documents the
  sound hardware as unknown.
- PC/address involved: firmware routines `$00054d36-$00056034`; MMIO
  `$ffffea00-$ffffea1e`; 64 channel records at `$c0010000`, each `$40` bytes.
- Evidence: the ROM contains a hidden `SOUND TEST` screen at `$00248612` and
  its handler at `$00048bb6`.  The sound driver writes a sample pointer split
  across channel offsets `+$02/+$06`, pitch at `+$16`, stereo levels at
  `+$32/+$33`, then submits `$40|channel` (start), `$80|channel` (stop),
  `$c0|channel` (update), or `$240|channel` (looping start) through `$ea0a`.
  `$ea10-$ea17` are read as the 64 active-channel bits.  The pointed ROM data
  is signed 8-bit PCM with `$80` used as the end marker.  The firmware computes
  the channel pitch as `source_rate * 65536 / engine_rate` and stores the
  engine rate at low RAM `$0150`.
- Experiment: implement only those evidenced commands, active bits, PCM
  termination/looping, pitch conversion, stereo levels, linear interpolation,
  and 48 kHz output.  Do not guess the still-unknown envelope table semantics.
- Result: a 1,200-frame title/menu run reports active channels
  `F83F000000000000`, produces 1,600 non-zero samples in the final stereo
  frame with peak 19,454, and remains on the correct game menu.  The dedicated
  PCM command/loop/status test passes, as do all 13 project tests.
- Code changed: a small XaviX 2 PCM mixer is connected to the existing Windows
  audio output.  Envelope behavior and exact hardware filtering remain
  provisional and should be calibrated against the hidden sound test.

## Enlarged-window timing

- Observed symptom: `ban_naru` played visibly slower in a maximized window than
  in the reference hardware recording, despite the host completing a headless
  600-frame run substantially faster than real time.
- Timing involved: the guest is paced at 60 video frames per second and about
  98 million byte-cycles per second.  The former paint path scaled every frame
  into a client-sized compatible bitmap and then copied that entire enlarged
  bitmap to the window.
- Hypothesis: the duplicate full-window GDI work was starving the emulator's
  timer rather than the emulated CPU clock being too low.
- Experiment: enable the opt-in `XAVIXEMU_TIMING=1` window-title counters and
  compare minimized and maximized runs.  Before the display change, minimized
  operation held 60.0 FPS and 98.0 Mbyte-cycles/s, while maximized operation
  fell to 42.0 FPS, 68.6 Mbyte-cycles/s, and dropped 18 frames per measurement
  interval.  Draw the nearest-neighbor framebuffer directly to the window DC,
  clearing only letterbox bars; allocate the old-style compatible surface only
  when F8 needs a screenshot.
- Result: the maximized run now holds approximately 60 FPS, 98 Mbyte-cycles/s,
  and zero dropped frames; an Alt+Enter full-screen run measured 60.0 FPS,
  98.0 Mbyte-cycles/s, and zero drops.  F8 still captures the current scaled
  viewport, and the normal paint path no longer performs a second full-size
  copy.
- Code changed: `src/main.c` display painting, on-demand screenshot capture,
  and opt-in timing diagnostics.  No guest clock, opcode, interrupt, or
  game-specific timing hack was changed.

Opcode `$ff`, gameplay gesture classification, exact audio envelopes and
filtering, EEPROM persistence, and save states remain later milestones; XaviX 2
support is therefore experimental rather than claimed complete.

## Equal-priority GPU layers and PCM loop endpoints

- Observed symptom: after entering Story Battle, the prompt
  `はじめから？つづきから？` and its hit targets existed, but both visible
  choice panels were absent.
- GPU data involved: the recorded command list at low RAM `$7480` contains the
  two 92x36 choice objects at command indices 17 and 18.  They share priority
  `$1f400000` with the background objects at indices 0 and 1.
- Hypothesis: sorting equal-priority commands by descending list index draws
  the choices before the opaque background, which then covers them.
- Experiment: replay the recorded command list using descending priority but
  stable ascending submission order for ties.  The two choice panels appear at
  the same positions as the real-hardware recording around 00:45.
- Result: equal-priority list order is now preserved.  No crop, palette, input,
  or game-specific drawing hack is involved.
- Code changed: `src/xavix2/xavix2_machine.c` GPU ordering only.

- Observed symptom: title/menu music eventually retained harsh unrelated
  noise, and the noise accumulated while navigating.
- Audio data involved: looping voices use samples such as `$79b280-$7a197f`,
  `$7dac80-$7dc5ff`, and `$7bca00-$7bfeff`.  In every inspected case the
  descriptor address at `+$0e/+$12` points one byte beyond the `$80` PCM end
  marker, not back into the sample.
- Hypothesis: treating that descriptor value as the loop destination enters
  unrelated ROM data immediately after the sample ends.
- Experiment: preserve the descriptor value as an optional end boundary and
  restart a `$240|channel` looping voice from its primary start address.
- Result: looping voices remain inside their evidenced PCM ranges.  A
  ROM-independent regression uses a descriptor end pointer after the marker
  and verifies that playback returns to the primary start.
- Code changed: `src/xavix2/xavix2_audio.c`, its voice state, audio test, and
  opt-in WAV/voice diagnostics in the boot probe.

The pitch conversion itself remains unchanged: recorded firmware values still
resolve to nominal rates near 12, 16, 24, and 32 kHz through
`pitch * engine_rate / 65536`.  Exact envelopes, filtering, opcode `$ff`, and
the remaining user-reported timing difference still require separate evidence;
no arbitrary global speed or pitch multiplier was added.
