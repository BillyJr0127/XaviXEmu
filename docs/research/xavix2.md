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
  to `$0c5c`, and waits.  The level-7 timer callback, DMA completion IRQ
  (level 12), PIO/24C08 traffic, and IRQ acknowledgement all continue working.
- Initial hypothesis: XaviX 2 has another timer/event source that invokes the
  firmware callback at about ROM `$1f325`, where code writes 1 to `$0c5c`.
- Experiment: raise each currently enabled IRQ level and separately apply a
  probe-only diagnostic pulse to RAM `$0c5c`.
- Result: the diagnostic `$0c5c = 1` pulse fast-forwards the loop and reaches
  the title, but a full timing run proved that the normal level-7 timer callback
  releases it naturally.  The first model incorrectly raised this timer only
  at 60 Hz vblank; isolated title audio later established its independent
  120 Hz cadence.  `$0c5c` is a synchronization flag, not the first functional
  blocker.
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
  the channel pitch as `source_rate * 65536 / engine_rate`. `ban_naru`
  stores its derived rate at low RAM `$0150`; this address is firmware-private
  and is not shared by the other titles.
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
filtering, and EEPROM persistence remain later milestones. Versioned,
ROM-isolated F5/F7 runtime states now capture the XaviX 2 CPU, GPU, audio,
timing, controller, and EEPROM hardware while rebinding process-local ROM and
bus callbacks on load. XaviX 2 support remains experimental.

## Four-image boot investigation (2026-08-12)

- Scope: the exact `ban_naru`, `ban_bldj`, `ban_db2j`, and `ban_dbz` images
  listed in `docs/rom-metadata.md`. ROM ZIPs were opened read-only outside the
  repository and were never copied into the source tree.
- Baseline: `ban_naru` and `ban_bldj` rendered their expected early graphics.
  Both Dragon Ball images remained black in XaviXEmu and in MAME revision
  `cf2d7a9be259552a3b493156e50e9149b6856448`.
- First DBZ blocker: timer IRQ 7 entered a low-RAM callback, started DMA, then
  executed EI/WAIT. Because interrupt status was also used as the CPU delivery
  line, the still-active IRQ 7 immediately nested and its prologue overwrote
  the callback. The later `$ff` fetch was corrupted RAM, not an original ROM
  opcode.
- Experiment: split interrupt status from pending delivery. Acknowledgement
  removes only the pending delivery, while firmware-visible status remains
  set until the documented clear write. A newly raised DMA IRQ remains able to
  wake WAIT.
- Result: DBZ completed the DMA sequence without the corrupted `$ff`, but its
  next RAM test exposed a second hardware property. The downloaded routine
  intentionally tests data ranges `$00000000-$000003ff`, palette/video ranges,
  and `$00000400-$0000ffff` while executing at nominal addresses
  `$00000100-$0000031a`. Unified low RAM therefore cannot represent the
  observed execution.
- Correction: DMA populates a distinct 64 KiB low-address instruction image as
  well as the data image. CPU instruction bytes use the former; normal loads
  and stores use the latter. This is a machine-level model, not a title check
  or a skipped self-test.
- Result: existing `ban_naru` and `ban_bldj` 600-frame image hashes are
  unchanged. `ban_db2j` and `ban_dbz` now render a BANDAI logo and reach the
  Japanese safety screen within 1,800 frames. DBZ also produces non-zero PCM.
  At 3,600 video frames all four exact images reach their own title or
  title/menu screen through the normal CPU, interrupt, DMA, and GPU paths.
  A complete 60-second PCM capture contains non-zero samples for `ban_naru`
  and `ban_dbz`; `ban_bldj` and `ban_db2j` remain entirely silent despite
  active voice status and need separate audio investigation.
  The four dynamically generated `$ff` executions in DB2J and twelve in DBZ
  seen by the 600-frame regression remain recorded as unknown CPU behaviour
  rather than hidden. Longer title runs encounter additional `$ff` executions
  in all four images, so its exact semantics remains unresolved.
- Automated coverage: the ROM-independent CPU test verifies that instruction
  fetch may differ from data reads. The machine test drives a real low-address
  DMA transfer, verifies the separate post-DMA views, and confirms that an
  accepted source is not immediately redelivered while a different pending
  source is still accepted. All 14 CTest tests pass.

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

Follow-up user testing showed that the corrected loop endpoints did not fix the
overall pacing or all accumulated noise.  Command-level traces and a short
user-supplied real-hardware recording then provided three additional pieces of
evidence:

- the firmware loads the system-clock constant `98,437,488` and divides it by
  `(13 + 1) * (32 + 1)`, storing the resulting 213,068 Hz engine rate at low
  RAM `$0150`; its pitch routine explicitly computes
  `source_rate * 65536 / engine_rate`, establishing Q16 rather than Q15;
- a spectrum comparison of the same title cue gives a 1.00 candidate/reference
  frequency ratio with Q16, while the experimental Q15 interpretation is both
  too fast and spectrally mismatched.  The reference excerpt is analysis-only
  and is not part of the repository;
- `$c0|channel` commands copy the live `$ea18` pitch and `$ea1c/$ea1d` volume
  controls into an existing voice, including observed pitch slides such as
  4900, 4986, 5156, and 5455; ignoring them left stale voice parameters; and
- high-rate playback can advance by more than one source byte, so checking only the
  byte landed on can skip a `$80` terminator and enter the next ROM region.

The voice model now latches start parameters, applies the live `$c0` updates,
uses the firmware-derived Q16 step, and checks every source byte crossed by a
high-rate voice.  ROM-independent tests cover parameter updates and a
terminator skipped by a two-byte step.  Overall music/event pacing, stale
voices during menu transitions, exact envelopes, filtering, and opcode `$ff`
remain separate research milestones rather than being hidden behind another
global playback multiplier.

The initial CPU scheduler still used a rounded 98 MHz estimate even though the
same firmware exposes the exact 98,437,488 Hz system source used by the audio
divider. The scheduler now uses that exact source as well. This is a 0.446%
timing correction, not a title-specific music multiplier; exact envelopes and
filtering remain open.

Follow-up validation confirmed that Q16 gives normal voice pitch while the
game still feels slow and polyphonic menu passages retain audible noise.  A
30-second automated title-to-character-select capture reached the signed
16-bit output rails during ordinary music, proving that the provisional mixer
had no accumulator headroom.  The output conversion now preserves one guard
bit before saturation.  This changes neither source pitch nor firmware event
timing.  F10 exposes live FPS, guest byte-cycles, dropped frames, and WinMM
drop/underrun counters so host scheduling can be separated from guest timing
on the user's machine before any clock change is considered.

## Cross-title audio engine-rate correction (2026-08-12)

- Observed symptom: the three newly recognized images appeared silent in the
  Windows front end. `ban_bldj` and `ban_db2j` reported active voices but
  generated zero PCM, while `ban_dbz` generated nominally non-zero PCM that
  was effectively inaudible.
- Fault isolated: the audio renderer read its engine rate from low RAM `$0150`
  on every frame. That address came from the `ban_naru` firmware trace; it is
  title-private RAM rather than a sound-hardware register. At 1,200 frames,
  `$0150` held 213,068 in `ban_naru`, zero in `ban_bldj` and `ban_db2j`, and
  257 in `ban_dbz`. `ban_bldj` stored the same derived 213,068 value at
  `$0158`, directly demonstrating the layout difference.
- Hardware evidence: 30-frame MMIO traces show `ban_naru` and `ban_bldj`
  program `$ea00=$20`, `$ea05=$0d`, while `ban_db2j` and `ban_dbz` program
  `$ea00=$20`, `$ea05=$0f`. The traced firmware formula divides the 98,437,488
  Hz source clock by `(EA00 + 1) * (EA05 + 1)`, producing 213,068 Hz for the
  first pair and 186,434 Hz for the second. Existing pitch traces use the
  resulting engine rate to convert channel Q16 pitch to source-sample cadence.
- Correction: the provisional XaviX 2 audio model now derives its rate from
  those sound MMIO divider registers. It no longer interprets any title's low
  RAM as an audio register. This is shared hardware behavior, not a ROM-name
  check or a per-game playback multiplier.
- Regression: ROM-independent audio tests verify both observed divider pairs.
  The machine test uses the two MMIO configurations while placing zero and
  257 at private low RAM `$0150`, then verifies the exact source position and
  generated PCM after a normal video frame.
- Long-run validation: all four exact images naturally issue the already
  modelled one-shot, looping, update, and stop commands. `ban_bldj` starts its
  later multichannel title audio around 20.6 seconds; `ban_db2j` reaches it
  around 47.7 seconds and `ban_dbz` around 46.1 seconds. At 60 seconds all
  four images report active voices and non-zero PCM. No additional audio IRQ,
  title delay bypass, or ROM-specific start command was introduced.

### Naruto title-state channel isolation (2026-08-13)

- A user-supplied F5 state at the title restores nine active voices on channels
  5 through 13. Their firmware-derived source rates form intentional families
  near 12, 18, 24, and 35 kHz; forcing every channel to one rate would alter
  the music's pitch and is not a valid speed correction.
- Channels 5, 6, 7, 10, 11, 12, and 13 are looping in this checkpoint, while
  channels 8 and 9 are one-shot voices. The checkpoint alone cannot establish
  which loop should already have stopped or faded, so no title-specific channel
  was disabled.
- The GUI now exposes all 64 voices with live rate, volume, and loop labels.
  Host muting deliberately leaves source position and guest-visible active bits
  unchanged. This permits channel-by-channel isolation without changing the
  timing that produced the fault; the resulting channel number can then be
  traced back to its `$80|channel` stop or `$c0|channel` update lifecycle.
- The earlier full-mix comparison was invalid: accompaniment peaks hid the
  timing error in an isolated melodic voice.  A new capture with every voice
  except channel 8 host-muted was aligned to the same flute phrase at 10 seconds
  in the reference video.  Its moving harmonic sequence has a clear optimum at
  0.5025 emulator/reference speed (neighbouring 0.5000 and 0.5050 are the next
  candidates), establishing that firmware music events were running at about
  half speed.
- The channel-8 descriptor still derives a plausible approximately 16 kHz PCM
  source rate from the `$ea00/$ea05` sound dividers.  Doubling CPU byte-cycle
  throughput while preserving the PCM clock produced a WAV byte-for-byte
  identical to the slow baseline, proving that the sequencer is interrupt-
  paced rather than CPU-bound.  The correction keeps 60 Hz video and the
  98,437,488 Hz CPU/PCM sources, but separates IRQ 7 from vblank and schedules
  it at 120 Hz.  This doubles note start/stop and game-event cadence without
  shifting sample pitch.  Version-1 F5 files migrate their timer phase at load.
- Repeating the same-state channel-8 capture with the 120 Hz timer moves the
  sequence-alignment optimum from 0.5025 to 1.0225; its narrow-band pitch ratio
  remains 1.0000.  Reference and emulator onset families now agree around
  0.82/0.83, 1.23/1.24, 1.64/1.65, and 3.27/3.29 seconds.  The residual roughly
  2% alignment offset is below the precision justified by a mixed-video source,
  so the hardware-like 120:60 integer cadence is retained.
- The remaining overlapping loop noise exposed a separate waveform-address
  error.  Channel 8, for example, starts at ROM `$7dac80`, has its second
  address at `$7dc600`, and contains terminators at `$7dc5ff` and `$7dc67f`.
  The second address is therefore the sustain-loop target selected after a
  terminator, not an exclusive end that wraps playback to the primary start.
  Channels 3, 10, and 13 have the same terminator-immediately-before-target
  layout.  The audio core now plays each attack once and loops only the sustain
  section.  Exact envelope and filter behavior remains a separate milestone.

## Shared controller/power status register (2026-08-12)

- Observed symptom: `ban_bldj` displayed a blinking crossed-battery warning in
  the upper-right corner even though the emulated controller has no battery
  failure state.
- GPU evidence: command zero at low RAM `$6d30` draws a 32x16 2-bpp object from
  ROM `$786b80`.  The command is added only when the firmware's debounced status
  is zero; RAM `$0c84` bit 4 controls only its blink phase.
- Register evidence: `ban_bldj`, `ban_db2j`, `ban_dbz`, and `ban_naru` contain
  the same routine for `$ffffec48`.  It treats bits 1:0 as a firmware-written
  saturating debounce counter and bit 2 as an externally supplied status line.
  The corresponding routine entry points are `$40058c01`, `$40076401`,
  `$4005392a`, and `$40054cee`.
- Correction: reads preserve the two-bit counter and report the normal
  controller/power status line high; writes update only the counter and cannot
  clear bit 2.  This lets the original firmware converge to its ready state
  without hiding a sprite or checking a ROM name.
- Remaining uncertainty: the exact physical source of the status line has not
  been confirmed from a schematic or PCB trace, so the model deliberately does
  not claim a specific battery-monitor circuit.

## Fractional GPU scaling and Blue Dragon gameplay (2026-08-12)

- Observed symptom: the selected mode portrait in `ban_bldj` was split by
  one-pixel seams, and its battle scene drew the background and HUD but omitted
  the enemy character.
- Command evidence: GPU bits 36-41 and 42-47 form six-bit unsigned Q2.4 X/Y
  scale factors. The selected menu image uses 16x16 source tiles, field `$11`,
  and a 17-pixel placement grid. Treating only the upper two bits as an integer
  draws 16-pixel tiles and necessarily leaves the observed seams.
- Battle evidence: 27 character commands use field `$0a`; their 32x32 source
  tiles are placed on a 20-pixel grid, exactly matching `32 * 10 / 16 = 20`.
  The old integer-only decode reduces `$0a` to zero and drops that complete
  layer. The corrected path renders it at 0.625x.
- Cross-title evidence: `ban_dbz` uses `$3f` to make an 82x61 source cover
  approximately 323x240 pixels. A later 169x35 object steps through `$30`,
  `$2c`, `$28`, `$24`, `$20`, `$1c`, `$18`, and `$14` while its position moves
  to keep the object center fixed, directly demonstrating continuous zoom.
- Controlled comparison: the old and corrected Blue Dragon menu frames differ
  in 464 pixels, all within the selected tiled image. At the same battle
  checkpoint, CPU execution, low RAM, and video RAM are identical while the
  old renderer omits the enemy and the corrected renderer shows it.
- Code changed: the command renderer now applies the complete Q2.4 fields to
  pixel boundaries. A ROM-independent GPU test covers `$11` magnification and
  Blue Dragon's `$0a` reduction. `XAVIX2_GPU_SCALE_TRACE=1` enables a bounded probe-only
  command/descriptor report for future zoom effects.
- Remaining uncertainty: output size and placement are firmware-proven, but
  the exact subpixel sampling phase has not been compared against a hardware
  pixel capture.

## Per-title motion packet buffers (2026-08-12)

- Observed symptom: the host wrote every IRQ-10 packet to low RAM `$000d`, the
  address used by Naruto and Blue Dragon, so `ban_db2j` and `ban_dbz` could not
  receive the same controller data through their own handlers.
- Firmware evidence: DB2J consumes its producer packet at `$014d-$0153` and
  copies it to `$0146-$014b` with status at `$0145`; DBZ uses `$0149-$014f`,
  `$0142-$0147`, and `$0141` respectively.
- Code changed: board setup selects the producer address explicitly from the
  verified ROM kind, and reset preserves that selection. The frame API remains
  format-agnostic and still receives two XYZ samples plus status.
- Blue Dragon input evidence: two exactly overlapping reflector samples are
  merged by firmware into one blob. Its central confirm gesture succeeds when
  the second sample is separated vertically by two sensor units. This offset
  is limited to `ban_bldj`; Naruto retains coincident samples for its documented
  joined-hands guard gesture, and DB2J/DBZ gesture meanings remain under study.

## GPU0 perspective ground path (2026-08-12)

- Observed symptom: Blue Dragon's battle characters appeared after fractional
  sprite scaling was fixed, but the lower 99 screen rows remained black.
- Submission evidence: firmware tests bits 8 and 9 of `$ffffe60a` separately.
  Bit 8 submits `$e600/$e602` through the GPU0 trigger at `$e408`; bit 9 submits
  the ordinary sprite list through GPU1 at `$e414`. The preliminary `$0240`
  status advertised only GPU1, so the first channel was never submitted.
- Record evidence: GPU0 uses 16-byte triangle records, not the GPU1 eight-byte
  object format. Its 96-record Blue Dragon list contains the missing terrain
  mesh and texture state. Treating it as 96 eight-byte objects stops exactly
  before the first useful terrain record.
- Projection evidence: command 2 at `$e858` consumes signed Q16.16
  depth/vertical/horizontal triples and writes doubled projected Y/X values.
  Blue Dragon uses focal length `$0080`; the firmware halves the results and
  adds screen center. With this unit absent, 80 terrain triangles collapse to
  `(1024,512)` before reaching GPU0.
- Texture evidence: the record fields and addressing match SSD's polygon and
  divided-texture layout in patent families CN101116112A and US20090278845.
  The implementation uses the documented Bw/Cw perspective weights,
  Tsegment table, descriptor geometry, 4x4 Map-1 blocks, and palette lookup.
  This restores the perspective ground instead of stretching a sprite band or
  inserting a title-specific background.
- Validation: the same 11,000-frame battle replay now retains the existing
  character/HUD layers and fills the formerly black lower half with the ROM's
  green/stone terrain and projected shadow. ROM-independent tests cover the
  channel-ready bits and a traced projector fixture. Texture filtering remains
  nearest-neighbour; the observed ground requests filtering, so bilinear
  sampling and the other polygon modes remain follow-up accuracy work.

## Dragon Ball cursor and receiver path (2026-08-12)

- DB2J and DBZ place their IRQ-10 producer packets at `$014d` and `$0149`
  respectively. Each packet is two X/Y/reflector-area triples plus status;
  the third byte is area, not depth.
- Both titles exposed a CPU decode error in opcode `$06/$07`. The load-
  immediate form has a signed 19-bit value: `$0647ee80` is `-$1180` and
  `$0647f380` is `-$0c80`. Treating it as signed 22-bit pinned both optical
  cursors outside their menu bounds even though packet delivery and gesture
  classification were active.
- With the corrected decode, DB2J's title PIO bits 16/19 select its red and
  blue routes, and its later menus use live optical coordinates plus an
  approximately 56-frame dwell. A horizontal pair at X `$23/$27`, Y `$18`
  selects the central Dragon Mission option and reaches the Shenron story.
  Story arrows require a genuine leave/re-enter collision edge rather than a
  digital button. Repeating that edge advances through Kame House, then a dwell
  on the mission screen's right-side `決定` reaches the first battle without a
  PC or state bypass.
- DBZ samples PIO bit 23 as a receiver-present input during boot; neighbouring
  bits are firmware outputs, so the level is configured only for DBZ rather
  than injected as a generic button. A stable pair `$20,$19,$28` and
  `$24,$1d,$28` completes calibration, highlights `決定`, and reaches the real
  battle state without a PC, RAM, or ROM bypass.
- In that battle state, keeping the first reflector visible, hiding the second
  for eight frames, then restoring it sets the firmware's `$155c` event and
  reaches the attack/projectile-spawn consumer at `$400264bc/$400265ea`.
  Hiding both reflectors reaches the two-hand consumer, while a four-frame
  horizontal sweep reaches the deflect path. The GUI maps these independently
  to left mouse, right mouse, and Space.
- DBZ battle setup changes the visible origin from `$0360,$0188` to
  `$0310,$0188`; the front end now follows the guest-programmed crop. The
  observed battle list contains a first-person canyon mesh rather than a
  player-character model, so the absent on-screen Goku body is not by itself
  evidence of a missing layer. Enemy and later-effect states remain to be
  exercised.

## Dragon Ball geometry command chain (2026-08-12)

- An exact frame-2351 battle trace establishes the full `$e858` chain as
  repeated command `$10` matrix compositions followed by `$0c`, `$0f`, `$0b`,
  `$0e`, and `$4d`. The first mesh uses `$9440->$a440` for `$0c`,
  `$9440->$b540` for `$0f`, `$b540` plus polygon destination `$55f0` for `$0b`,
  `$9664/$b540->$5df0` for `$0e`, and `$a440/$b540->$55f0` for `$4d`.
- Command `$10` composes the traced mixed fixed-point 3x4 matrix state.
  Command `$0c` expands packed signed XYZ10 vertices through the retained
  32-bit coefficient registers and Q16.16 translations. These stages now
  produce a non-empty projected list, but are not claimed cycle-exact.
- Command `$4d` now projects conventional Q16.16 XYZ vertices, removes the
  traced back-face winding, compacts the 16-byte records in place, and reports
  the active count through `$e85c`. At the DBZ battle checkpoint this produces
  non-empty GPU0 records; the DB2J first battle likewise submits a polygon
  scene instead of a zero-count list. Material output is not yet correct.
- The two GPU channels need a depth-aware merge rather than submission-order
  overwrite. The observed DBZ scene divides GPU1 into depth-`$ff` sky, then
  GPU0 terrain, then depth-`$00-$fe` HUD/foreground sprites. The compatibility
  renderer follows that evidenced split while retaining sprite-only title
  behavior; a complete scanline/depth merger remains future work.
- Command `$0f` now expands the traced packed normal data. Commands `$0b` and
  `$0e` remain unimplemented; firmware routes polygon lighting/material state
  through them before `$4d`. Without a hardware output fixture, their exact
  fixed-point normalization and light calculations are not guessed. Current
  geometry is therefore a rendering milestone, not a claim of correct battle
  terrain, model lighting, or complete later effects.

## Naruto second-stage distance unit (2026-08-13)

- An F7 checkpoint at hardware frame 13,584 resumes the forest battle with the
  terrain, HUD, enemy sprite, and rotating projectile supplied by independent
  GPU lists.  The enemy is a 78x102 source sprite using the normal six-bit
  Q2.4 scale path; it is not a missing polygon layer.
- Firmware wrapper `$40054392-$400543a6` writes a 32-bit value to `$e800`,
  starts geometry command `$11`, and reads a 16-bit result from `$e804`.
  Its callers form `x*x + y*y` before the call and use the result for live
  cursor/target distance decisions, identifying the operation as unsigned
  floor square root.
- With command `$11` ignored, `$e804` retained an unrelated prior value.  A
  controlled replay changed the rotating projectile's orientation and command
  count even though CPU state, ROM, and input sequence were otherwise equal.
  Implementing `floor(sqrt(E800.l)) -> E804.w` restores the firmware-owned
  distance result; ROM-independent tests cover zero, non-squares, squares, and
  the full 32-bit input range.
- A 104-frame replay after F7 shows the complete enemy sprite and correctly
  scaled projectile.  Longer replays confirm that the enemy is intentionally
  absent between its attack windows; this is distinct from a renderer dropping
  a submitted character command.  Later levels and a complete play-through
  remain outside this checkpoint validation.
