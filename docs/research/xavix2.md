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
  tests/xavix2_boot_probe.c`.  They began as an isolated probe and the proven
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
- Initial hypothesis: because descriptor `+$0e/+$12` points one byte beyond the
  first `$80` end code, it was provisionally treated as an end boundary and a
  `$240|channel` voice restarted from the primary waveform.
- Superseding evidence: SSD's US 7,561,931 B1 explicitly specifies an attack
  array followed by a separately terminated sustain array.  Naruto ROM data
  matches it byte-for-byte: the descriptor's second address is the first byte
  of a complete second array, not unrelated data.
- Correction: looping voices play the first array once and repeat the second.
  F7 loading also refreshes the loop head from the guest descriptor so older
  states do not retain the provisional primary-loop address.  See
  [`xavix2-audio.md`](xavix2-audio.md) for the patent and ROM measurements.
- Code changed: `src/xavix2/xavix2_audio.c`, state restoration, audio test, and
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
timing.  F11 exposes live FPS, guest byte-cycles, dropped frames, and WinMM
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
- The earlier primary-restart conclusion was later disproved by the SSD sound
  patent and a wider ROM measurement.  Channel 8's `$7dc600-$7dc67f` second
  array is a short sustain cycle; other instruments have second arrays of
  thousands of bytes.  This exact attack/sustain layout now takes precedence
  over the earlier mixed-video comparison.  See
  [`xavix2-audio.md`](xavix2-audio.md).
- Command traces then separated the two forms of `$c0|channel`. Pitch slides
  write zero to `$ea1a/$ea1b`; note releases set `$ea1b` bit 0 at routine
  `$40055d42-$40055d89`.  Initial notes on channels 11-13 release six video
  frames after key-on, while later melodic notes commonly release after eleven
  frames.  The old mixer ignored this flag, leaving looped notes audible until
  channel reuse.  Immediate deallocation was also wrong because the firmware
  still observes the channel during the release stage.  The provisional model
  now keeps the active bit asserted during a 16-frame linear fade, then clears
  the voice; a new key-on cancels the fade.  An isolated title-state comparison
  keeps the sampled pitch ratio at approximately 1.00.  Firmware tracing now
  proves that the release routine reads `+$36`, scales it by low RAM `$1441`
  divided by 64, writes it back, and then sets `$ea1b` bit 0.  Its exact
  envelope meaning and the hardware filter remain to be decoded before audio
  can be considered exact.

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
  channel-ready bits and a traced projector fixture. Type-0 Filter bit 27 is
  now honored: zero performs the documented four-tap bilinear interpolation,
  while one retains nearest sampling. Map-0/Map-1 folding, transparent texels,
  and premultiplied palette alpha are covered by synthetic fixtures.

## Indexed-palette alpha on the Blue Dragon result screen (2026-08-15)

- A user F5 state resumed at frame 7349 and reached the same result panel after
  180 video frames. The final GPU1 list still contained all eleven tiles for
  each missing bar: the upper panel uses palette 96-103 and the lower panel
  uses 104-111. This rules out a missing command, bad crop, or depth rejection.
- The dominant nonzero texel is color 7. Its palette values are `$a8e0` for
  the cyan-blue bar and `$80ea` for the orange bar; the repeated color-0 entry
  is `$8421`. Treating every palette value with bit 15 set as fully transparent
  therefore discards both intended fills along with the real transparent
  texels.
- SSD's RPU patent describes palette entries as premultiplied RGB plus
  `(1-alpha)` and blends them as `destination * (1-alpha) + source`.
  <https://patents.google.com/patent/CN101116112A/zh>. A later EPOCH trace
  resolves the exact high-bit layout as `1:B4:N2:G4:N1:R4:N0`: bit 15 selects
  premultiplied RGB444 plus the three interleaved `Nalpha` bits. `$8421` is
  consequently zero source with `Nalpha=7`, the indexed-texture fully
  transparent endpoint. The endpoint is not an exact-color key: EPOCH also
  uses `$8c61`, `$8423`, `$8463`, and `$8c63` on transparent borders. Texel
  zero is not intrinsically transparent; Blue Dragon's sky uses it for normal
  opaque colors. Type-1 Gouraud `Nalpha=7` remains the separately documented
  87.5% destination contribution.
- Applying that model restores both colored panels and leaves the background
  engraving visible through them, matching the supplied hardware capture and
  the result sequence around 4:43 in
  <https://www.youtube.com/watch?v=DycWroEDXI8&t=283s>. The same rule is shared
  by sprite and textured-polygon palette lookups; no ROM, frame, or title
  special case is used.

## Type-1 premultiplied alpha on the Blue Dragon boss shadow (2026-08-17)

- A maintainer F7 state at frame 49,357 submits 18 Type-1 triangles whose
  bounding boxes form a tapered strip from screen `(149,100)-(171,119)` down
  to `(160,195)-(160,196)`. This is the same deforming translucent connector
  visible below the dragon in the hardware video around 14:38; no command or
  geometry is missing.
- The strip's flat RGB555 colors fall from `$3a45` through `$2de4`, `$2563`,
  `$1902`, `$0ca1`, `$0860`, and `$0420` while Nalpha rises from zero to four.
  The source color is therefore already premultiplied in step with alpha.
- Type-1 blending now follows the same SSD premultiplied convention as the
  indexed-palette path: `output = source + destination * Nalpha / 8`, with
  component saturation. The former extra `source * (8-Nalpha) / 8` term
  attenuated the connector twice and made its lower segments appear absent.
  A ROM-independent half-alpha fixture covers the corrected transfer without
  a title, frame, or descriptor special case.
- The connector also proves that the RPU streams must be merged by depth rather
  than rendered channel-by-channel. Its Type-1 triangles use depth `$2ff`;
  the dragon-body sprites use depths `$d0/$e0` (equivalent to `$d00/$e00`),
  while the foreground character uses depth `$10`. Drawing the complete sprite
  list after GPU0 hid the connector behind the dragon. The compositor now
  interleaves nonzero polygon depth groups with sprite depth bands, producing
  the required far dragon body -> translucent connector -> near character
  order. The established zero-depth Dragon Ball compatibility path is retained
  until those games expose final polygon depths.

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
  `$24,$1d,$28` completes calibration, highlights `決定`, and reaches a random
  demo battle without a PC, RAM, or ROM bypass.
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

### DBZ store-demo identity

- The exact `ban_dbz` image with CRC32 `$7e535ea2` and SHA-1
  `6c746af763273bd9e47929c3ba857c7af563bf79` visibly labels its title screen
  `体験版`. Selecting `決定` enters a random battle directly rather than the
  retail story flow. The image is therefore documented as a store-demo build;
  the generic product name used by the preliminary MAME entry is not evidence
  that this particular dump is the retail program.
- Firmware contains only one absolute PIO read, at `$400192ed`; it masks only
  receiver-present bit 23. Neighbouring PIO bits are outputs, and no second
  DIP/mode input is sampled. Independent cold boots with the complete serial
  EEPROM filled with `$00`, `$55`, `$aa`, and `$ff` are frame-, CPU-, and RAM-
  identical through the title. No hardware or EEPROM switch that unlocks a
  retail route has been found.
- The demo still contains multiple battle environments and character assets,
  which explains why much retail-looking data is present. That does not
  establish that the omitted story state machine is reachable from this ROM;
  XaviXEmu does not patch a retail mode into the guest.

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
- SSD's geometry patent resolves the formerly ambiguous normal-lighting chain.
  `$0f` is the composite `TD` command: it transforms each packed Vector10
  normal to internal Q0.15, computes the light/view dot flags, and writes one
  eight-bit `Dot` record per normal. It does not write three XYZ bytes. The
  one-byte output keeps DBZ/DB2J's following material tables intact instead of
  corrupting them.
- Command `$0b` (`Ccalc`) consumes those indexed Dot records for Type-1
  polygons. It applies the documented face/sign test, adds the five-bit
  Ambient level, and rewrites the three premultiplied RGB555 vertex colors.
  Command `$0e` (`TDBP`) performs the same transform/dot path for sequential
  Type-0 face normals and writes only `d2[14:10]` Light. The rasterizer applies
  that field as the documented `1/32..32/32` texture-RGB multiplier while
  preserving palette alpha.
- The signed light vector still comes from command `$07`, which transforms
  signed XYZ16 values through the Q7.8 matrix before firmware copies the
  result to `$e850/$e852/$e854`. ROM-independent tests cover opposite normals,
  front/back sign selection, Ambient, Type-1 vertex colors, Type-0 Light, and
  the one-byte TD output boundary.
- Vector16 screen X/Y are signed Q15.1. Composite `$0c` APV must therefore
  retain the same factor of two as stand-alone `Pproj` followed by `View`.
  Removing the old APV-only half-scale closes DB2J F7's left ground opening by
  projecting the ROM's terrain at its hardware size; it also reduces the list
  naturally through the documented viewport clipping rather than a
  title-specific hole filler. Blue Dragon, Naruto and DBZ F7 checkpoints keep
  their existing HUD, character and foreground layers.
- The two GPU channels still need a fully scanline-accurate YSU/RPU merge. The
  current compositor preserves ordinary equal-depth sprite list order, which
  keeps DB2J's later attack/hit effects in front of the tiled enemy, and uses
  the established premultiplied palette/Type-1 alpha paths. Longer F7 replays
  show the energy sphere and hit flashes over the animated enemy at their
  firmware-submitted depth; remaining edge-order cases should be solved by the
  patented per-line merger, not by game-specific object reordering.
### DB2J large enemy projection

- The DB2J battle also uses geometry command `$0d` for a separate large-object
  path. Command `$0c` supplies indexed integer XYZ anchors; `$0d` writes their
  doubled projected coordinates back to the anchor records and fills the
  polygon depth used by firmware to choose the tiled-sprite scale.
- A preceding command `$01` is not the same transform as `$0c`: it returns
  Q16.16 points for the firmware's distance calculation. The traced `(3,0,4)`
  translation therefore has length `$50000`, not `5` or `$500`; the latter
  interpretations clamp both six-bit sprite scales to `$3f` and enlarge the
  enemy until only parts of its body remain visible.
- The doubled coordinate contains one retained wrap page above each packed GPU
  axis (11-bit X and 10-bit Y). Firmware averages the unwrapped coordinates for
  culling, then masks them when it builds the sprite list. Dropping those page
  bits made hit detection continue while the enemy's 8-by-5 tile object was
  rejected off-screen.
- At the maintainer battle checkpoint the corrected hardware path grows GPU1
  from 23 to roughly 50 records. Captures through 1,200 video frames show the
  normally scaled enemy changing distance and pose, attacking, and receiving
  aligned hit flashes. This is guest animation rather than a host-injected
  sprite. Textured polygon materials remain limited by the unrelated `$0e`
  work above.

### Equal-depth sprite geometry and DBZ title recovery

- DBZ's startup frame submits a full-screen backing sprite after its smaller
  XaviX mark at the same depth, so plain list order hides the mark. A blanket
  reverse order is also wrong: DB2J submits its two 160x240 Shenron backing
  halves before a later 8x3 grid of 32x32 dialogue-panel tiles, and reversal
  hides the panel. The current scanline-sort baseline therefore paints larger
  covered areas before smaller same-depth overlays, retaining list order for
  equal-sized objects. The maintainer's F7 dialogue checkpoint and DBZ frame
  150 verify both orderings directly.
- Startup issues two `E408(count=0) -> E414(count=2)` pairs in one video frame.
  Each empty GPU0 submission opens a new depth-FF sprite pass. Battle frames,
  by contrast, prepaint a nonempty sprite background before GPU0 polygons and
  must not repaint that sky over the polygon result at E414.
- An earlier provisional command `$07` transform was removed after it broke
  DBZ's title target. A later trace established the signed XYZ16/Q8.8 form
  documented above; that implementation retains the known green decision
  target while supplying the battle light-vector registers.

## Naruto second-stage distance unit (2026-08-13)

### Title hit-test regression

- A fresh boot could display and move the host cursor over the title's central
  sensor target, yet a verified centered packet plus the Execute pulse no
  longer advanced to `メニューの巻`.
- The regression was isolated from motion routing, the 120 Hz game timer,
  geometry command `$11`, signed immediate decoding, and the later-stage
  multiply correction. The same input sequence advanced as soon as `$f6/$f7`
  again cleared/set Z as required by hit-test helpers `$4244d/$4248b` and their
  immediate `$da` branches.
- The fixed 1,800-frame replay reaches `メニューの巻` with frame hash
  `F2C8540EDE87A19B`. Replaying the user's second-stage F7 checkpoint still
  shows the enemy in distinct near, far, running, and attack poses, so the
  menu repair does not revert the later trajectory fixes.

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
  scaled projectile.  Frames without the enemy contain no matching sprite
  command, so this is distinct from the renderer dropping a submitted layer.
  The later trajectory investigation below found and corrected a CPU flag
  error; earlier long-replay conclusions about the intended absence duration
  are therefore superseded.

### Second-stage hardware-video comparison and audio baseline

- A newer user F7 checkpoint at hardware frame 5,877 resumes the same forest
  encounter.  Before the CPU correction, a 600-frame command trace submitted
  descriptor 33 (the 78x102 enemy) on only 73 hardware frames and the visible
  positions were fragmented.  The object still existed in firmware, but its
  motion vector repeatedly sent it beyond the 320x240 crop.
- The movement path at `$34223-$3424b` normalizes an angle, looks up sine, and
  performs a fixed-point multiply.  The normalizer reads the signed division
  remainder from hardware result register HR3 with `C9 43`, then immediately
  branches on its sign.  The interpreter moved HR3 without updating N/Z, so
  the branch inherited the negative flag from an older comparison and added a
  false `$400` half-turn.  The resulting lookup was negative and moved the
  enemy offscreen in the opposite direction.  Updating N/Z on C8/C9 result
  moves restores the expected `$0ab` angle and positive sine/movement value.
- That flag correction made the first approach continuous, but a closer
  hardware-video comparison at 2:38, 2:43, and 2:48 showed that it was not the
  complete fix.  The real enemy remains present through changing poses and
  moves between foreground and background distances, whereas the emulator's
  object stayed in state 1/action 0 at depth 1280 and eventually ran offscreen:
  <https://www.youtube.com/watch?v=kTcEUWf5t7U&t=147s>.
- The remaining failure was in the B4/B5 64-bit multiply.  The fixed-point
  helper at `$54560` multiplies a positive radius by a signed ROM sine-table
  value, then checks that HR1 is the sign extension of HR0 before extracting
  the Q16.16 result.  The preliminary unsigned interpretation turned every
  negative half-cycle into an overflow.  The enemy therefore never reached the
  ground condition that advances its behavior and replacement schedule.
- Treating B4/B5 as signed restores the firmware-owned sequence without any
  sprite, culling, timer, RAM, or ROM override.  At 100-frame samples of the
  same 600-frame F7 replay, one live enemy progresses through actions 1, 2, and
  3, behavior states 1, 2, and 7, and depths 1920, 1760, and 2240.  Captured
  frames show different running, attack, smoke, and distant poses at frames
  100, 200, 300, 400, 500, and 600, matching the user's observation that the
  second-stage enemy should normally remain active while its pose and distance
  change.
- The boot probe now offers `XAVIX2_AUDIO_CHANNEL_METRICS=1`.  Unlike the old
  end-of-frame log, it records each sound command at the instant it executes,
  so a stop and restart in the same frame are not misreported as one sustained
  voice.  A deterministic ten-second replay of this F7 point uses effect
  channels 0 and 1 plus music channels 3-13.  Channels 3-13 each receive 20-25
  key-ons, 17-21 release commands, and firmware pitches corresponding to about
  9.4-42.2 kHz source rates under the common 213,068 Hz engine clock.  Only
  channel 9 performs seven zero-flag pitch slides; the other live updates are
  key-off releases.  This is the per-channel regression baseline for decoding
  the remaining instrument-specific envelope/filter behavior without inventing
  independent channel clocks.
- All eleven looping instruments reached by the same replay have a secondary
  descriptor address exactly one byte after the primary waveform's first `$80`
  terminator.  The separation holds across primary lengths from 3,455 to 31,231
  bytes, while one-shot descriptors may retain unrelated secondary values.  The
  secondary field is therefore an end/boundary pointer here, not an alternate
  sustain waveform; loop restart at the primary address is the evidence-backed
  behavior.

## 640x480 startup display mode (2026-08-15)

- DB2J programs `$e650=$0008` with visible origin `$02c0,$0106` during the
  common XaviX 2 startup sequence. Its GPU list contains two adjacent
  320x480 sprites which form one 640x480 logo frame.
- Returning a fixed 320x240 viewport therefore exposed exactly the upper-left
  quarter. Mode `$08` now reports the full 640x480 surface and validates the
  origin against that size; later title and gameplay modes continue to report
  their programmed 320x240 viewport.
- The full 16-bit value and programmed origin both matter. EPOCH Take-copter's
  XaviX startup logo uses `$e650=$1608` with origin Y `$0120` and is a native
  640x480 surface. Later scenes keep `$1608` but switch origin Y to `$0110`;
  their lists fill 640 pixels across and only 240 backing rows. Presenting the
  latter as native 640x480 compressed the scene into the upper half, while
  doubling the startup form stretched and clipped its logo. Only the `$0110`
  form is now line-doubled. A 30-frame cold-boot capture of the `$0120` form is
  pixel-identical to the retained pre-doubling baseline.

## EPOCH XaviX 2 recognition batch (2026-08-15)

XaviX 2 is not a PlayStation 2 software profile. Its firmware directly
programs the XaviX 2 geometry unit, perspective projector, polygon lists, and
sprite lists. A title may nevertheless choose mostly scaled two-dimensional
art: Naruto and Blue Dragon rely heavily on the sprite channel, while the two
Dragon Ball battle paths exercise the polygon channel substantially more.

Five additional exact 8 MiB images were verified read-only and added without
filename-based guessing. `epo_dtcj` reaches a complete Doraemon tutorial
screen and `epo_pabj` reaches the Pooh name-entry UI. `epo_dab2j` reaches the
XaviX/EPOCH logos, safety warning, and a later book-like screen, but not yet a
verified title. All three keep the CPU running and produce nonzero PCM.

`epo_ssk2` and `epo_sskj` are deliberately listed as not working. The former
continues into an unresolved low-ROM CPU/MMIO loop without a visible frame;
the latter completes early initialization and then holds a blank white frame.
PIO16/P16 SDA and PIO17/P17 SCL traffic is now routed to the 24C04-compatible
EEPROM model documented for this EPOCH board family, but that board-level fix
does not justify inventing controller packets or bypassing either firmware
wait. Each title has a separate runtime-state identity so later fixes cannot
cross-load state from another XaviX 2 image.

The three boards with documented 24C04 devices (`epo_dtcj`, `epo_ssk2`, and
`epo_sskj`) now also use separate 512-byte durable files. Runtime-state restore
first saves and snapshots the live EEPROM, restores guest hardware, then
overlays that image and its write generation. F7 therefore remains useful for
diagnosis without rolling back settings or controller calibration. At 600
natural frames, `epo_ssk2` and `epo_sskj` have issued 27 and 24 serial writes;
`epo_dtcj` configures the same PIO16/P17 bus but has not written it at that
checkpoint. `epo_dab2j` and `epo_pabj` do not configure the bus in the same
probe and retain runtime-state-only storage.

The first `epo_ssk2` failure is now bounded more tightly. At cycle 26,282,447
the firmware enters `$000f6164`, reads an element count of zero from low RAM
`$005d`, then executes its normal decrement-and-branch loop. The branch and
zero-flag behaviour match the reference CPU implementation; changing them or
skipping this PC would be a title-specific bypass. The missing prerequisite is
the producer that should repopulate `$005d` after the IRQ task clears low RAM,
so the title remains not working pending that hardware/data-flow result.

`epo_pabj` confirms that its UI is not waiting on ordinary PIO buttons. Its
firmware converts a hardware-produced record at `$1008-$100f` through
`$00044400`, writes the resulting three coordinates to `$1010-$1012`, and
consumes them at `$0003fe5f`; 600 natural frames contain 231 updates while PIO
input reads remain zero. This is a useful controller integration seam, but the
record's physical source and calibration are not yet proven, so no synthetic
mouse mapping is enabled. `epo_sskj` likewise has no PIO input reads at the
blank-screen checkpoint, despite active CPU/audio work and 24 serial EEPROM
writes, pointing to a separate unmodelled controller/acquisition condition.

## EPOCH Take-copter infrared receiver (2026-08-26)

`epo_dtcj` does not read an ordinary button register or the wrist-reflector
motion buffer. Its firmware arms the head-mounted receiver with IRQ2, samples
PIO7 with IRQ10, shifts the serial word into low RAM `$02e4`, and decodes it
through the ROM tables at `$006dcc00-$006dcc4f`. Successful packets write the
mapped direction to `$02e0` and set the new-sample flag at `$02e1`.

The receiver has a persistent one-bit phase distinction. A newly synchronized
packet leaves counter `$02e2=$1a` and needs 27 IRQ10 clocks for bits 26 through
0. Once armed, the next IRQ2 handler consumes the first count itself, leaves
`$02e2=$19`, and needs only 26 IRQ10 clocks for bits 25 through 0. Sending 27
clocks unconditionally shifts every later packet and produced only an
occasional valid sample. The emulator now reads the firmware's live counter
after IRQ2 and supplies exactly the remaining clocks, while the original IRQ
handlers and ROM lookup remain responsible for decoding.

The verified 4-by-4 code grid is:

| Horizontal | Neutral | Down | Up | Both vertical |
|---|---:|---:|---:|---:|
| Neutral | `0199e667` | `0199e001` | `019987ff` | `01999999` |
| Right | `01986679` | `0198601f` | `019807e1` | `01981987` |
| Left | `0181e787` | `0181e1e1` | `0181861f` | `01819879` |
| Both horizontal | `01866799` | `018661ff` | `01860601` | `01861867` |

The mapped bits are `$01/$04` for the two horizontal directions and
`$10/$40` for the two vertical directions. The routine at `$000352cb`
filters the latest five samples and requires a magnitude of at least three
before changing direction. Host keyboard directions take priority. A held
right-button mouse drag accumulates relative head tilt and returns to centre on
release, preventing a menu direction from remaining asserted indefinitely.
The selected PS analogue stick uses its current normalized axes rather than
the retained reflector position used by optical games, so its physical spring
return also sends neutral. Wii position selects the same grid. Because the
physical controller is worn on the head rather than pointed at the television,
the game has no persistent point cursor. The GUI therefore suppresses both the
former EPOCH orange tilt marker and the XaviX2-wide blue diagnostic target;
this presentation choice does not change the tilt reports. Horizontal,
vertical, diagonal, and the quick backward-to-forward acceleration sequence
are all represented through the same two axes. Rapid backward-to-forward
crossing on a PS analogue stick or right-button mouse drag is promoted to the
filtered sequence rather than being lost between video reports.

At the saved opening checkpoint, holding the Left code starts a `$78`-tick
firmware countdown. Completion changes `$0c8c` from state 1 to state 3. A
70-video-frame Left hold followed by neutral reaches the later `$1608`
640x240 line-doubled scene, changes the GPU lists from
`$7380/4,$32d0/3` to the active
`$5ce0/6,$32d0/37-38` pair, continues audio, and produces no unmapped reads or
writes over a 500-frame replay. Keeping Left held far beyond the completed
prompt can steer subsequent screens and is not a valid neutral-transition
test.

## EPOCH Take-copter eye-plane geometry (2026-08-26)

> Correction after locating SSD's geometry-engine patent: `$ffffe844` is
> `ZNear` and `$ffffe846` is `ZFar`; `$e846=$7fff` is not a disabled-near-plane
> sentinel. The experimental fan-clipping result described below was based on
> that incorrect interpretation and is not a valid hardware model. See
> [XaviX 2 geometry and rendering hardware](xavix2-3d.md) for the patent-backed
> GE/YSU/RPU model and replacement plan.

The later-game F7 fixture resumes at cycle `53851842112` in display mode
`$1610`, crop `$0360/$0188`. Before this fix, 97.5 percent of its 320x240
frame was white and only the bottom character-status text survived. The
firmware was still submitting three command-`$4d` geometry batches, including
272 vertices in the terrain batch, so this was not a stalled game or a missing
presentation crop.

The projector's `$e846` register is `$7fff` here. This is a disabled explicit
near-plane sentinel, not a signed Q16.16 near distance. Treating it as a
distance rejected every projected triangle. The remaining issue was faces
crossing the camera eye: rejecting a complete face when one vertex had
non-positive depth kept only a thin horizon strip. The command-`$4d` path now
copies the source list before compaction, clips Gouraud terrain against a
one-unit eye plane and the selected 320x240 viewport, interpolates RGB555
edge colors, and fan-triangulates the clipped result. Indexed material faces
are composed after the Gouraud sky/terrain base for this sentinel mode.

The saved state now produces 146 valid polygons on its first resumed frame.
Sixty-frame neutral, left, right, and up replays finish with different frame
hashes and 127-149 polygons, matching visible horizon/terrain movement rather
than a static replacement picture. A command-`$4d` regression test checks
that an eye-crossing face produces only in-viewport packed coordinates and
that a fully behind-camera face still produces no output. A 30-frame cold
boot capture remains byte-identical to the retained pre-fix XaviX-logo
baseline. Exact later-scene character and material composition still needs
comparison with original hardware.

## EPOCH Take-copter presentation, controls, and audio cross-check (2026-08-30)

Cold-boot captures separate three firmware-selected display forms. Mode
`$1608` with origin `$0120` is a native 640x480 logo frame; mode `$1608` with
origin `$0110` is a logical 640x240 scene that the presenter line-doubles to
640x480; the later `$1610`, crop `$0360/$0188` path is a full 320x240 frame.
The current 60-second controlled replay fills the complete output with the
Doraemon flight artwork, sky, and water, and the saved later scene also fills
all 320x240 pixels. Its sparse horizon is therefore missing geometry/material
content, not a host-side half-height resolution or viewport error.

The host now exposes six title-specific actions: forward/start, backward,
left, right, upright, and boost. A tap is retained for six video reports
because the firmware consumes a five-sample filtered history. The boost action
sends six backward reports, six forward reports, and six neutral reports.
Keyboard, right-button relative mouse movement, the selected PS/gamepad
analogue stick, and Wii position all converge on the same
verified 4-by-4 infrared code grid instead of bypassing the game's decoder.

The title programs sound dividers `$ea00=$20` and `$ea05=$17`, which derive a
124,289 Hz engine rate from the 98,437,488 Hz source. This PCM source clock and
its Q16 pitch remain independent of the firmware-event timer. The earlier
wide-window spectrum comparison was sufficient to reject a PCM pitch change,
but not to establish visual/event cadence.

### Take-copter horizon blending, late-flight fade, and cadence (2026-08-30)

The public hardware recording at 11:20 shows a cyan haze between the sky and
water rather than an additional fog sprite. The resumed `epo_dtcj` frame uses
Type-0 descriptors zero through three for the complete water surface: 2 bpp,
`Filter=0`, full light, and high-bit palette entries `$8c63,$9ce6,$ad4b,$bdce`.
Their interleaved `Nalpha` values are exactly `7,6,5,4`. The passing cloud uses
`$8c61,$9ce6,$bdce,$e319`, another deliberate `7,6,4,1` ramp. This resolves the
high-bit format as `1:B4:N2:G4:N1:R4:N0`, with premultiplied RGB444 and
three-bit inverse alpha. Treating every entry as fixed one-half transparency
had darkened the cloud's rectangular border three times and suppressed the
water's cyan/white distance haze. Treating `Nalpha=7` as only 7/8 transparent
then left lighter but still plainly visible rectangles around clouds,
characters, and title art. The indexed-texture path now discards that endpoint;
values zero through six use the patent's
`background * Nalpha/8 + premultiplied source` blend. Palette scans from EPOCH,
Naruto, Blue Dragon, DBZ and DB2J find no confirmed visible `Nalpha=7` texel,
so this remains a shared hardware rule rather than a title-specific exception.

Near the end of the flight, records 215 and 216 in the `$4e00` GPU0 polygon
stream form the two halves of the complete visible viewport at depth zero and
color `$7fff`. The sky and terrain records use larger depths `$a00-$e00`.
The contemporaneous GPU1 list still contains 16 cloud/character sprites at
depths `$2e-$7c`. The former submission-order renderer completed GPU0 first,
including the transition, then E414 repainted every GPU1 sprite at full
contrast. This exactly explains the emulator's white sky with unaffected
characters.

The EPOCH compositor now draws the nonzero Type-1 sky/terrain backdrop, lets
Nalpha-graded Type-0 water soften that backdrop, draws the E414 sprite
stream, and finally composites GPU0's deferred depth-zero Type-1 transition.
The fade records themselves prove that the missing gradual transition was not
an absent alpha ramp: at resumed frames 400, 410, and 420, the two full-screen
triangles progress through `Nalpha=5/2/0` while their already-premultiplied
RGB555 values rise from `$2d6b` through `$5ef7` to `$7fff`. Replays now show
clouds, characters, and the sky losing contrast together before the final
opaque white frame. A cross-channel regression test locks the E408-before-E414
case so sprites cannot be repainted after the fade.

The frame also contains the horizon material; it is not an uncovered geometry
hole. Type-1 records provide the cyan/white sea-base gradient and Type-0
records provide the bit-15 RGB444/Nalpha water texture. Class ordering removes
the recent opaque-sky-over-water white strip; decoding the texture's `7..4`
alpha ramp then restores the broad cyan/white haze that hides the sea/sky seam
without blurring clouds or characters. Dense F7 replays also retain the
already-correct gradual full-scene fade.

Feature matching
puts resumed emulator frame zero at hardware-video second 17.7. With the
former universal 120 Hz IRQ-7 sequencer, emulator frame 480 reached the title
after eight host seconds, while hardware reaches it near second 33.9: a 2.03x
event-speed error. Take-copter therefore uses one IRQ-7 tick per 60 Hz video
frame in every display mode. An initial follow-up incorrectly inferred that the
`$1608` logo/carousel surface selected 120 Hz from its higher note-command count.
User validation showed the carousel and audio falling far out of real time, and
a same-state benchmark isolated the cause: 300 menu frames take 5.958 seconds at
120 Hz but 2.946 seconds at 60 Hz. Display width is not a timer selector, and
note-command counts are not independent cadence evidence. Bandai's independently
verified boards retain 120 Hz throughout.

A left/neutral/right carousel replay at 60 Hz confirms that the firmware accepts
the recovered EPOCH infrared direction words. Door, story, high-score, and
free-mode cards remain separate in every full 640x480 frame. The raw lower 240
rows are blank, the presenter replaces all 480 output rows on every call, and
the GPU clears its complete internal target at vblank. The two or three dark
blue rounded strips below each card are also present in that card's ROM source
art and on hardware; they are intentional shadows, not a stale second menu row.
A genuinely duplicated full card would need a new F5 captured on the bad frame.
### Take-copter sprite filtering and Gouraud dither (2026-08-30)

The first resumed flight frame submits 16 sprites. Every command has the
unclipped-sprite `Filter` bit 29 clear, which Fig. 16 and Fig. 21 of
CN 101116112 A define as four-tap bilinear sampling. The five flying-character
parts at indices 11-15 are all reduced from their source sizes with Q2.4 scales
`$0e`, `$0c`, `$0b`, `$08`, and `$09`. Nearest-neighbor sampling was therefore
the direct cause of their coarse outlines. Sprite rendering now reverse-maps
destination pixel centres, samples the four surrounding row-padded texels, and
interpolates RGB plus the patented premultiplied `(1-alpha)` value. `Filter=1`
remains nearest-neighbor. A synthetic 2x sprite regression locks down both
modes, and resumed Blue Dragon, Naruto, DBZ, and DB2J states remain stable.

The same F7 state contains `$e620=$c6`; all four Bandai states contain
`$e620=$d8`. Each byte is a different permutation of the four two-bit values
0, 1, 2, and 3. This matches Fig. 20 exactly: the register packs the four noise
values selected by the X/Y coordinate LSBs and added to fractional Gouraud RGB
before RGB555 quantization. Applying that programmed pattern changes only
quantization choice and makes the cyan-to-white sky gradient less banded.

There is no missing full-screen white sprite in the resumed GPU1 list: the
largest source is 120 by 48 and the remaining records are clouds and flying
characters. The hardware recording's additional broad softness is consistent
with the patented final path through an NTSC/PAL composite video encoder and
analog video DAC. The patent does not publish that encoder's filter
coefficients, so an exact composite low-pass model remains separate work; a
title-specific white overlay or arbitrary whole-frame blur is not justified by
the submitted scene data.

### Take-copter translucent mesh coverage and cloud placement (2026-08-30)

The later rainbow fixture submits GPU0 Type-1 records 57 through 88 at depth
`$a05`, all with `Nalpha=3`. The former all-inclusive barycentric test let both
triangles own pixels exactly on a shared edge. Twenty-one pixels in the 32-face
rainbow mesh were consequently blended twice, exposing its diagonal
triangulation; the dense water mesh had the same general defect. The RPU patent
gives a half-open split between the upper and lower portions of a triangle, but
does not specify a complete modern top-left pixel-coverage convention. A trace-
and image-compatible top-left edge rule changes 224 pixels in the resumed
frame, removes all shared-edge double coverage, and is protected by an adjacent
translucent-triangle fixture. The established premultiplied
`source + destination*Nalpha/8` transfer remains unchanged.

The rainbow's broad level lower end is not a missing alpha layer: all five
bottom vertices are deliberately at screen Y 105 with color `$4d82`. The softer
hardware capture is consistent with the final composite-video path; changing
Type-1 alpha or adding a title-specific blur would regress the Blue Dragon
connector and EPOCH full-scene fade.

The same F7 frame's three cloud objects use Type-0 descriptors 0 through 2 and
the shared `$19003d1d` material. Their palette entries `$8421`, `$9ce6`,
`$ad4b`, and `$bdce` provide the expected transparent-to-half-opacity white
ramp. At the
saved yaw, the only positive-depth cloud spans visible X 342 through 478 and
is just outside the 320-pixel right edge; the other two are behind the camera.
Rightward motion brings them through Y 71 through 114, above the Y 140--155
horizon. This state therefore does not support a global cloud Y offset or a
display line-doubling change; a new F5 is required if an underwater-cloud
frame can still be reproduced.

### Take-copter cross-class depth merge and composite output (2026-08-30)

The RPU patent sorts the Type-0 and Type-1 streams independently, then feeds
both through one merger sorter. For semitransparent content, larger raw depth is
drawn first so the destination already contains the surface behind the current
primitive. The former EPOCH shortcut instead rendered every nonzero Type-1
primitive before every Type-0 primitive, regardless of their interleaved
12-bit depths.

The resumed fixture contains 356 GPU0 records: 227 Type-0 and 129 Type-1.
Replaying the same RAM, palette, and ROM with the patented cross-class depth
order changes exactly 3,793 of 76,800 visible pixels. Of these, 2,785 lie in
the sea/sky transition at Y 139 through 157. The live renderer now walks each
distinct nonzero raw depth from far to near and renders both types in stable
record order. Depth-zero Type-0 remains in the first GPU0 pass, while the
existing depth-zero Type-1 full-scene transition is still deferred until the
E414 sprite pass. A mixed Type-0/Type-1 overlap fixture prevents class order
from overriding raw depth. The complete 19-test suite passes, and an F7 A/B
reproduces the predicted 3,793/2,785 pixel counts exactly.

This correction fixes a real layer-joining error, but it does not reshape the
rainbow. That object is a 32-face Type-1 Gouraud mesh at depth `$a05`, with one
polygon-wide `Nalpha=3`; its five bottom vertices are deliberately level at
screen Y 105. Type-1 does not pass through the texture bilinear unit, has no
per-vertex alpha, and the patents describe no multisample or coverage
anti-aliasing block. Its dedicated 2-by-2 dither only perturbs fractional RGB
before quantization to reduce Mach bands.

The final hardware boundary is also now better constrained. CN 101116112 A
sends line-buffer RGB through an NTSC/PAL composite video encoder and video
DAC. SSD's earlier dedicated encoder patent, JPH 10-301552 A / JP 3554137 B2,
uses subcarrier modulation and documents optional Y band-stop and C band-pass
analogue filters to reduce luminance/chrominance interference. It gives NTSC
and PAL encoder clocks of 21.47727 and 21.28137 MHz, respectively, but does not
publish a receiver/capture filter response. Therefore a future Composite
presentation mode should model horizontal luma/chroma bandwidth separately;
an arbitrary full-frame Gaussian blur or title-specific rainbow blur is not a
hardware-backed core fix.

Primary sources:

- <https://patents.google.com/patent/CN101116112A/zh>
- <https://patents.google.com/patent/US20080273030A1/en>
- <https://patents.google.com/patent/US20090278845A1/en>
- <https://patents.google.com/patent/JP3554137B2/ja>

### Mixed-layer seams in enhanced presentation (2026-08-30)

The remaining join around Take-copter objects was not a native triangle crack.
The resumed rainbow contains 40 shared Type-1 edges with zero duplicate
coverage, zero uncovered pixels, and matching colors at every shared vertex.
Instead, the optional 2x presentation path chose its resampler from only the
nearest native pixel: polygon pixels used bilinear interpolation while an
adjacent sprite pixel used nearest-neighbour.  The same final RGB edge was
therefore filtered on one side and hard-cut on the other.

This distinction does not exist at the documented hardware boundary.  The RPU
color blender first writes the already-composited polygon/sprite RGB into its
line buffer, and only then does the video encoder consume that RGB stream.
The enhanced presenter now keeps wholly non-polygon footprints sharp, but any
2-by-2 footprint touching polygon content is filtered symmetrically on both
sides of the join.  In the resumed F7 frame this changes 3,192 of 307,200 2x
output pixels.  A red-polygon/blue-sprite regression verifies symmetric
75/25 and 25/75 samples while the solid regions remain unchanged.

This removes an emulator-created object seam but deliberately does not alter
the rainbow's level native bottom edge.  A future composite-video mode may
model luma and chroma bandwidth after final composition; it must remain
separate from geometry, coverage, and alpha correctness.

### Translucent sprites over 3D (2026-08-30)

The `ban_dbz` F7 battle trace identifies the expanding red energy ball as a
96-by-96 indexed GPU1 sprite.  Four consecutive ROM frames at `17b680`,
`17c880`, `17da80`, and `17ec80` are 0x1200 bytes apart.  Direct 4-bpp decoding
disproves the earlier transparent-cutout hypothesis: palette index zero is
confined to the area outside the ball, while its complete centre uses index
six (`41bf`), an ordinary opaque RGB555 pink/white entry.

The actual failure was foreground-list reordering.  Each `$e414` list submits
23 small depth-zero tiles that assemble Frieza, followed by the depth-`71`
energy sphere.  The compatibility renderer globally sorted that list by
Depth, drawing the sphere first and then reconstructing Frieza over its opaque
centre.  Four stage captures isolated this sequence.  The patented RPU uses a
scanline prefetch/recycle merger rather than a frame-wide object sort.  Until
that path is literal, the compatibility renderer recognizes the consecutive
depth-zero small-tile run and moves only its immediately following large
effect behind that run in draw time (therefore in front on screen).  The same
rule covers the weak sphere: it stays at depth zero but grows beyond the
generic presentation-backing area threshold, which previously made it flip
behind the fighter partway through its animation.  All other
foreground, background, and pre-polygon objects retain their established
depth sort.  This narrower rule also prevents the trial clear screen's
depth-`04` logo tiles from being replayed over its higher-depth panel bands.
The result hides Frieza behind the opaque core as in the public hardware
recording: <https://www.youtube.com/watch?v=J1pfNd5NEPg>.

An independent block-like edge did come from optional enhanced presentation.
Its ownership mask previously cleared the underlying polygon bit for every
visible sprite texel, including translucent texels that retain destination
RGB.  The palette and bilinear samplers now propagate the effective
destination weight to presentation; only a fully opaque sprite replaces
polygon ownership.  Native 1x output remains bit-identical, while the 2x
presenter filters the already-composited aura/character boundary consistently.
A regression covers a translucent sprite crossing the last polygon pixel;
another covers the different-depth tiled-enemy/energy-sphere foreground order.

### RPU frame latch and alternating Bandai command buffers (2026-08-31)

The `ban_db2j` reflection checkpoint exposed a brown cross for one host frame
and a dark rectangle for the next.  Eight consecutive final-frame captures
first suggested a transparent-palette failure, but captures taken immediately
after every `$e414` trigger separated the two submissions inside each 60 Hz
video frame.  The first complete `$e408/$e414` pair was clean on frames four,
five and six.  Only the second pair, using the alternate `$7fc0/$8420` command
buffer, introduced the rectangles.

The ROM evidence explains why forcing palette index zero transparent would be
wrong.  The 36-by-36 reflection texture at `$415300` has index zero around its
complete border, but the second submission changes its palette epoch from a
transparent `$8421` entry to `$8021` and later to opaque `$530e`.  The following
73-by-81 effect at `$28cd00` is likewise dominated by border index zero while
its second-submit palette uses `$8021`.  Drawing both complete lists into one
persistent software framebuffer therefore combines object data from one epoch
with the alternate buffer's palette and leaves exactly the observed blocks.
Other XaviX2 art uses an opaque index zero, so a global color-key remains
disproved.

SSD's RPU prefetchers consume a sorted polygon/sprite submission for the
displayed frame and receive an explicit frame/field-switch notification.  The
Bandai firmware runs its event interrupt at 120 Hz while video remains 60 Hz,
so it can trigger the alternate complete buffer before the next vblank.  The
emulator now retains the first completed merged surface until that vblank;
the later paired `$e408/$e414` trigger remains guest-visible as ready but does
not repaint the current line-buffer surface.  Sprite-only startup submissions
are unaffected because they do not set the completed merged-pair latch.

The same F7 state now produces eight clean consecutive frames with the
reflection and character animation intact.  One-frame resumed captures from
Blue Dragon, DBZ, Naruto and Take-copter retain their expected terrain,
characters, HUD and clouds.  A synthetic regression changes the palette before
a second complete pair and verifies that neither the completed pixel nor the
GPU pixel-write counter changes before vblank.  Avoiding the redundant full
raster also reduces the DB2J high-resolution replay average from about
14.15 ms to 7.21 ms per frame; in a new 1,800-frame run only one frame exceeded
16.67 ms and the maximum was 22.56 ms.

Primary source:

- <https://patents.google.com/patent/US20090278845A1/en>
