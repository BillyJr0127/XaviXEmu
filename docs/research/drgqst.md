# `drgqst` emulation investigation

This note tracks the investigation of the first blocking problem in
`drgqst` (Kenshin Dragon Quest).  The local test ROM is deliberately excluded
from version control and is not modified by this work.

## Baseline

- Branch: `drgqst-re`
- Upstream commit: `cf2d7a9be259552a3b493156e50e9149b6856448`
- MAME version: 0.289 development tree
- Driver: `src/mame/tvgames/xavix_2000.cpp`
- Machine/input/state: `xavix2000_i2c_24c08`, `ttv_lotr`,
  `xavix_i2c_lotr_state`
- Functional source changes before baseline build: none

## Static investigation map

The following are candidates for dynamic tracing, not conclusions about the
first blocker.

| Area | Guest address / interface | Current implementation | Reason to trace |
| --- | --- | --- | --- |
| IN1 / I2C | lowbus `0x7a01`, direction `0x7a03` | Bits `0x02` and `0x04` use independent PRNG reads; SDA is bit `0x08` | Separates placeholder sensor-like inputs from EEPROM traffic |
| Analog ports | lowbus `0x7b00`, `0x7b01`, `0x7b10`, `0x7b11` | Default callbacks return `0xff` | No `drgqst`-specific sensor callbacks exist |
| ADC | lowbus `0x7b80`-`0x7b81` | Conversion is immediate and status is a placeholder random value | Could be polled by sensor or calibration code |
| Lightgun / pen | lowbus `0x6ffc`-`0x6fff` | Default reads return `0xff` | Potential optical-input path |
| Universal timer | lowbus `0x7c00`-`0x7c03` | Semantics are explicitly marked uncertain | `drgqst` is named as an active user; related games need timer workarounds |
| ROM DMA / IRQ | lowbus `0x7980`-`0x7987`, IRQ source `0x7ffc` | DMA completion and IRQ are synchronous | Source notes identify LOTR and `drgqst` as users of the DMA IRQ |
| XaviX2000 CPU | opcode stream and J/K/L/M/PA/PB registers | Several opcode/register semantics are uncertain | Must rule out a CPU-core failure before modelling the sensor |

## Experiments

### 2026-08-07: Obtain and identify the baseline tree

- Observed symptom: The initial workspace contained only
  `roms/drgqst.zip`; no source tree or Git repository was present.
- PC/address involved: Not applicable.
- I/O or register involved: Not applicable.
- Hypothesis: The source must be obtained before the requested baseline can be
  reproduced.
- Experiment: Fetched the current `master` tip from the official
  `mamedev/mame` repository and created `drgqst-re` from that commit.
- Result: Source is present at commit
  `cf2d7a9be259552a3b493156e50e9149b6856448`.  The ROM remains unchanged and
  ignored by Git.
- Code changed: None.

### 2026-08-07: Unmodified focused build

- Observed symptom: The first build attempt reached normal core compilation,
  but three concurrent `cc1plus` processes exhausted available memory.
- PC/address involved: Not yet measured.
- I/O or register involved: Not yet measured.
- Hypothesis: A build containing `xavix_2000.cpp` will reproduce the current
  `MACHINE_NOT_WORKING` behavior without functional source changes.
- Experiment: Build a focused MAME subtarget with
  `SOURCES=src/mame/tvgames/xavix_2000.cpp`.
- Result: The 12-job attempt stopped with host out-of-memory errors while
  compiling generic MAME core/OSD files.  There was no source compile error.
  The incremental two-job retry succeeded and produced `drgqst.exe` (MAME
  0.289, SHA-256
  `7012F310CB5FD744A010D2E2F2F40BACE0225D94A1535BAC681808B4E88B8AD3`).
  `-verifyroms drgqst` reports the local ROM set as good.
- Code changed: None (this investigation note does not affect emulation).

### 2026-08-07: Baseline runtime symptom

- Observed symptom: A 60-second unthrottled baseline reaches the sword
  calibration screen at video frame 2119 (35.3167 seconds).  Frames 2119
  through 3601 are byte-identical while the prompt asks for a large vertical
  cut through the center ball.
- PC/address involved: The main loop remains interruptible and continues to
  call the sensor wrapper at ROM `0x008cc8`; it is not a stopped CPU.
- I/O or register involved: No sensor-related ADC access occurs before the
  calibration sequence.  Video continues to render normally.
- Hypothesis: The visible wait is a software input/calibration state rather
  than a CPU, video, or boot failure.
- Experiment: Recorded the unmodified build for 60 seconds and compared every
  decoded frame around the final transition.
- Result: Boot logos, EEPROM-erased message, and the calibration UI all render.
  The repeatable first user-visible failure is the calibration wait.
- Code changed: None.

### 2026-08-07: Interrupt, timer, I/O, and ADC trace

- Observed symptom: At 100 ms the apparent `0x003bc5`/`0x003bd4` polling loop
  is serviced by the NMI handler and dispatches queued work; it is not a hang.
- PC/address involved: Initial ROM DMA loads `0x41ee78` to low RAM `0x2ac1`.
  DMA IRQs are handled at `0x003b59`.  Sensor acquisition later executes from
  low RAM `0x2ac1`-`0x2b2d`, called by ROM `0x008ce3`.
- I/O or register involved: DMA `0x7980`-`0x7987`; IRQ source `0x7ffc`; timer
  `0x7c00`-`0x7c03`; IN1 `0x7a01`; ADC `0x7b80`-`0x7b81`.
- Hypothesis: A malformed DMA IRQ, timer IRQ, EEPROM transaction, or XaviX2000
  opcode may be the first blocker.
- Experiment: Used bounded lowbus watchpoints for DMA, IRQ, timer, IN0/IN1,
  direction registers, I/O events, AN0-AN7, ADC, and lightgun registers during
  a 60-second run.
- Result: DMA copies valid executable code and its three observed completion
  IRQs are acknowledged.  EEPROM/I2C traffic completes before calibration.
  The calibration sensor wrapper starts and stops the timer normally; its
  counter changes from `0xff` to `0xfe` and no timer IRQ is pending.  There are
  no lightgun, direct analog-port, or I/O-event reads.  The first relevant
  analog activity is command `0x40` written to ADC control, which selects the
  AN0 callback (`0x40 & 0x13 == 0`), followed by reads at low-RAM PCs `0x2b03`
  and `0x2b06`.
- Code changed: None.

### 2026-08-07: First blocking branch identified

- Observed symptom: The first complete sensor acquisition stores only zeroes
  in the matrix at low RAM `0x0300`-`0x06df`.
- PC/address involved: At emulated time 35.308205809 seconds, ROM PC
  `0x008d1b` is reached from checks at `0x008d0b`-`0x008d19`.
- I/O or register involved: IN1 bits `0x02`/`0x04` supply transition timing;
  IN1 output bit `0x20` selects the second acquisition phase; ADC command
  `0x40` samples AN0.  The resulting count `$005b:$005c`, horizontal metric
  `$0202`, and vertical metric `$0206` are all zero.  IRQ source is zero and
  the timer is at `0xfe`.
- Hypothesis: The CU5501A optical acquisition interface is the first blocker:
  random placeholder sync inputs let polling eventually proceed, but the
  unused AN0 input supplies a deterministic blank image.
- Experiment: Broke on both outcomes of the firmware's first sensor-validity
  test (`0x008d1b` fail, `0x008d23` pass), captured the matrix and metrics, and
  disassembled the DMA source at ROM `0x41ee78`.
- Result: The fail breakpoint is always taken first.  The firmware acquires two
  31-row by 32-column images, subtracts the first from the second, counts pixels
  at least `0x08`, derives X/Y extents, and rejects the sample when those values
  are zero.  This rules out CPU opcode emulation, memory banking, DMA/IRQ,
  timer IRQ, EEPROM/I2C, video, sound, and the generic lightgun registers as the
  first blocking problem.  It does not imply those subsystems are perfect or
  that later blockers do not exist.
- Code changed: None.

## First-blocker milestone

The exact first blocking problem is an unmodelled CU5501A acquisition path.
`drgqst` currently inherits LOTR's independent PRNG placeholders for the two
IN1 synchronization bits and has no AN0 sensor source.  The game reaches its
intended calibration code, obtains an all-zero 32x31 difference image, and
takes the explicit no-sensor branch at `0x008d1b`.

The smallest testable correction will model the externally observable
behavior established by the firmware: deterministic synchronization edges on
IN1 bits `0x02`/`0x04`, two acquisition phases selected by output bit `0x20`,
and a small bright region in the AN0 image derived from a virtual sword X/Y
input.  This is a device-interface model, not a PC-specific bypass.  Exact
CU5501A timing, noise, optics, and exposure response remain future work.

### 2026-08-07: Minimal sensor-interface correction

- Observed symptom: With the original driver, the first validity test always
  reaches fail PC `0x008d1b` with a blank image.
- PC/address involved: Sensor acquisition remains the unmodified guest code at
  low RAM `0x2ac1`; the pass target is ROM PC `0x008d23`.
- I/O or register involved: IN1 bits `0x02`, `0x04`, and output bit `0x20`;
  ADC control/result `0x7b81`/`0x7b80`; virtual inputs `MOUSE0X/Y`.
- Hypothesis: Deterministic synchronization edges plus an illuminated-pass
  AN0 image should let the firmware itself derive valid coordinates.
- Experiment: Added a `drgqst`-specific state that preserves the inherited I2C
  behavior, supplies opposite-phase synchronization edges, resets a 32x31
  pixel stream for each acquisition pass, and returns a 3x3, value-`0x40`
  bright region at the virtual sword position only during the bit-`0x20`
  illuminated pass.  Rebuilt the focused target and broke on both validity
  outcomes.
- Result: The build succeeds.  At the first active acquisition, the fail
  breakpoint is not taken; the pass breakpoint at `0x008d23` is reached at
  cycle `0x2d32aef5`.  The guest computes threshold count `0x000b`, X metric
  `0x06`, and Y metric `0x3a`, with IRQ source `0x00` and timer current value
  `0xfe`.  This proves the correction supplies data through the real firmware
  protocol and crosses the exact first blocker without a PC check or skipped
  branch.
- Code changed: `src/mame/tvgames/xavix_2000.h` and
  `src/mame/tvgames/xavix_2000.cpp`; `drgqst` now has a dedicated state,
  machine configuration, and lightgun-style Sword X/Y inputs.  Shared LOTR,
  Star Wars, XaviX ADC, and timer code are unchanged.

### 2026-08-07: Static virtual-sword follow-up

- Observed symptom: Although every frame now contains a valid sensor point,
  the calibration prompt remains visible with an unmoving default input.
- PC/address involved: The validity path reaches `0x008d23`; no new fatal PC
  or stopped CPU is observed.
- I/O or register involved: Virtual Sword X/Y remain at a static host-input
  position.
- Hypothesis: Passing the sensor-presence check is necessary but the calibration
  state also requires the requested large vertical motion.
- Experiment: Recorded 90 seconds with the corrected driver and no host input.
- Result: Frame 2119 at 35.3167 seconds enters the calibration screen.  Frames
  2119 through 5401 are byte-identical for 54.7167 seconds.  This is expected
  for a static coordinate and identifies motion recognition as the next test,
  not a regression of the first-blocker correction.
- Code changed: None.

### 2026-08-07: Correct ADC stream framing

- Observed symptom: A scripted vertical input changes both firmware coordinate
  metrics instead of holding X fixed.  A direct matrix capture for host input
  `0x80,0x00` contains bright runs at row 0 columns 29-31, row 1 columns 0-1,
  and row 2 columns 27-31 rather than a 3x2 clipped blob near columns 15-17.
- PC/address involved: The first illuminated-pass ADC callback in a row occurs
  at low-RAM PC `0x2b47`; subsequent callbacks occur at `0x2b58`.
- I/O or register involved: ADC command/result `0x7b81`/`0x7b80`; matrix
  `0x0300`-`0x06df`.
- Hypothesis: Treating callbacks as pairs per stored pixel advances the virtual
  sensor at half speed and wraps its image into unrelated rows.
- Experiment: Logged callback PC, virtual pixel index, and phase for the first
  pixels of each illuminated row, then printed all 31 matrix rows at the first
  pass breakpoint.
- Result: The firmware performs one discarded priming conversion at the start
  of each row, followed by 32 conversions that are each read and stored.  The
  inner loop branches back to `0x2b4a`, after the priming conversion at
  `0x2b47`; it does not perform two conversions per stored pixel.  Change the
  stream sequencer to a 33-callback row: hold pixel 0 for the priming callback,
  then advance after each of the 32 stored callbacks.
- Code changed: The `drgqst` sensor callback framing in
  `src/mame/tvgames/xavix_2000.cpp`; no shared device code.

### 2026-08-07: Corrected matrix and axis correlation

- Observed symptom: The corrected stream framing must still be shown to place
  the virtual light at the intended matrix coordinates and keep the orthogonal
  axis stable during motion.
- PC/address involved: Matrix analysis is performed by the unchanged guest
  path before the pass branch at ROM `0x008d23`.
- I/O or register involved: AN0 ADC data becomes the difference matrix at
  low RAM `0x0300`-`0x06df`; guest coordinate metrics are `$0202` and `$0206`.
- Hypothesis: With one priming callback and 32 stored callbacks per row, a
  host input of `0x80,0x00` should produce a clipped 3-by-2 blob at the top
  center, not a wrapped image.
- Experiment: Captured all 31 matrix rows at the first pass branch, then drove
  Sword Y through its range while holding Sword X at `0x80` and logged the
  guest-derived metrics.
- Result: Only rows 0 and 1 contain non-zero samples, at columns 15-17 with
  value `0x40`; the threshold count is `0x0003`, X metric is `0x20`, and Y
  metric is `0x01`.  During vertical movement X remains `0x20` while Y moves
  from `0x01` through `0x3b`.  This supersedes the earlier pre-framing result
  (`count 0x000b`, metrics `0x06/0x3a`), which was produced by the incorrect
  paired-callback experiment.
- Code changed: None after the ADC stream-framing correction.

### 2026-08-07: Guest gesture classifier and vertical calibration

- Observed symptom: A valid but static sensor point, or a slow scripted slash,
  remains on the first vertical calibration prompt.
- PC/address involved: Motion tracking begins at ROM `0x008ff2`; displacement
  is calculated at `0x0092b8`, averaged at `0x00931f`, classified at
  `0x00933e`, and an accepted direction event is committed at `0x009098`.
- I/O or register involved: Average X/Y displacement is stored at
  `$022a/$022b`; sample count is `$005d`; LARGE stage is `$02f1`; accepted
  direction is `$0269`.
- Hypothesis: The remaining calibration wait is intentional gesture-speed
  filtering rather than another hardware failure.
- Experiment: Compared a one-second half-sweep with a 0.25-second half-sweep,
  while tracing all SMALL, MEDIUM, LARGE, and accepted-event branches.
- Result: The slow sweep produces 201 SMALL classifications, no MEDIUM/LARGE,
  and no event; its maximum observed per-sample displacement is `0x10`.
  The 0.25-second sweep produces 234 LARGE classifications at displacement
  `0x20` and 78 accepted events, split evenly between vertical direction codes
  `0x02` and `0x06`.  The firmware's squared-magnitude thresholds are SMALL
  below `0x0300`, MEDIUM from `0x0300` through `0x037f`, and LARGE at or above
  `0x0380`.  The vertical prompt starts at frame 2119; the valid fast slash
  begins changing the screen at frame 2136, and the horizontal calibration
  prompt is stable by frame 2198.  The transition is performed by unmodified
  guest gesture code.
- Code changed: None.

### 2026-08-07: Complete calibration and enter the game sequence

- Observed symptom: After the vertical step, the game requests a horizontal
  cut, then a timed ten-second multi-direction test, then asks the player to
  stop and confirm whether the sword cut at the intended location.
- PC/address involved: Accepted gestures continue through event commit PC
  `0x009098`; no PC-specific patch or forced game-state write is used.
- I/O or register involved: The same virtual Sword X/Y image and IN1/AN0
  acquisition path supplies every answer.  A focused confirmation-screen
  trace records no reads of IN0 `0x7a00`; reads of IN1 `0x7a01` remain inside
  the sensor acquisition routine, confirming that the on-screen arrow symbols
  request gesture directions rather than ordinary button bits.
- Hypothesis: A fast horizontal slash should begin the timed test; stopping
  after its countdown and answering "yes" with the indicated downward slash
  should finish calibration.
- Experiment: Sent the vertical calibration gesture, then fast horizontal
  movement from 37 to 48 seconds, held the sword still after the countdown,
  and finally sent a downward vertical gesture at 52 seconds.  Captured native
  screen images without modifying guest memory or EEPROM.
- Result: The horizontal gesture visibly cuts the ball and starts the timed
  test.  Holding still reaches the confirmation screen at frame 3066
  (approximately 51.1 seconds).  The downward gesture selects "yes"; by 52.5
  seconds the game reports that the sword has been adjusted.  Captures at 58,
  65, and 75 seconds show the voiced story/prologue advancing.  This is an
  end-to-end confirmation that the smallest sensor-interface correction clears
  the first blocker and reaches the normal game sequence.
- Code changed: None after the dedicated `drgqst` sensor-interface correction.

## Current model limits

- The IN1 synchronization timing is deliberately behavioral, not
  cycle-accurate.  The generic XaviX I/O write path calls `read_io1()` while
  composing output values, so such a write can consume one synthesized edge;
  the observed acquisition writes immediately reset the phase and are safe,
  but untested later I/O patterns may expose this limitation.
- Stream restart is currently level-triggered when output bits 0 and 5 are
  enabled and bit 0 is high.  This matches the observed pass-control writes,
  but the physical edge/level meaning of those lines is not yet known.
- The AN0 image is a small idealized bright blob.  CU5501A timing, optical
  response, noise, exposure, and the electrical identity of the two IN1 lines
  remain to be established from hardware evidence.
- `MACHINE_NOT_WORKING` is intentionally retained.  The correction clears the
  first proven blocker and reaches the prologue; it is not yet evidence that
  every later game path, save operation, video effect, or sound path is correct.

## Final verification for this milestone

- Focused `drgqst` build: succeeds with GCC 13.2.0.
- Driver validation: succeeds with no reported error.
- ROM audit: `romset drgqst is good` (one set checked, one OK).
- Corrected executable SHA-256:
  `6B0B0552A5DA987F5F07A71F2670E75B932EFDE1B788079A038B693A3243A31F`.
- `git diff --check`: no whitespace error (Git reports only the repository's
  configured LF-to-CRLF worktree warning).
- The local ROM remains ignored as `roms/drgqst.zip` and was not modified,
  renamed, deleted, redistributed, or added to Git.

### 2026-08-07: Correct horizontal sensor orientation

- Observed symptom: With an ordinary Windows mouse, MAME's Sword X crosshair
  appeared on one side of the screen while the game placed the sensed object
  on the horizontally opposite side.  A left-to-right slash was consequently
  classified as right-to-left.  Vertical placement and direction were correct.
- PC/address involved: Unchanged guest acquisition and gesture paths; this was
  an orientation error in the synthesized sensor image rather than a new
  firmware branch failure.
- I/O or register involved: Virtual `MOUSE0X` and the 32-column AN0 sensor
  stream supplied by `sensor_adc_r()`.
- Hypothesis: The firmware's CU5501A column order is horizontally opposite to
  MAME's screen-space lightgun X axis.
- Experiment: Reverse only the X coordinate used to place the bright region in
  the sensor matrix, while leaving the host lightgun port and crosshair in
  normal screen coordinates.  Reversing the input port itself was rejected
  because it would also reverse the displayed crosshair and preserve the
  mismatch.
- Result: The focused build and driver validation succeed.  In isolated
  runtime tests, host X `0x20` produces guest X metric `0x36`, while host X
  `0xe0` produces metric `0x08`; both runs retain Y metric `0x1e` and a valid
  nine-pixel sensor count.  This confirms the synthesized sensor column order
  is reversed without disturbing vertical placement.  The rebuilt executable
  SHA-256 is
  `363DF7D61CD1F941911D2A8F37D0AB79AE348D346696A4593298BCE600032617`.
- Code changed: `src/mame/tvgames/xavix_2000.cpp`; Y mapping and all shared
  XaviX input paths are unchanged.

### 2026-08-07: Launcher display and full-screen follow-up

- Observed symptom: The default Windows renderer bilinearly filters the
  low-resolution game image, producing a soft appearance when maximized.
- I/O or register involved: Host renderer and Windows OSD input only; no
  emulated hardware register.
- Hypothesis: D3D point sampling plus integer scaling will keep source pixels
  sharp, and Windows MAME's existing OSD shortcut can provide full-screen
  toggling without a custom keyboard hook.
- Experiment: Launch with `-video d3d -nofilter -nounevenstretch -prescale 1`
  and inspect the live calibration display.  Confirm the Windows input module
  maps left/right Alt+Enter to `IPT_OSD_1`, which calls the existing full-screen
  toggle.
- Result: The live display uses visibly discrete, unfiltered pixel blocks with
  integer-scale black borders.  The effective configuration reports D3D,
  filter disabled, uneven stretch disabled, and prescale 1.  Alt+Enter remains
  the built-in Windows MAME toggle and is documented for the user.
- Code changed: The standalone launcher arguments and Traditional Chinese
  README only; MAME's shared renderer and keyboard code are unchanged.

### 2026-08-07: Broad sword-reflection input

- Observed symptom: The narrow virtual sensor target can model ordinary cuts,
  but the original sword also presents its reflective face to prepare magic
  and moves that broad reflection vertically while guarding.  The existing
  model had no way to vary the illuminated area.
- PC/address involved: The sensor postprocessor at ROM `0x008eba-0x008ed0`
  compares the bright-pixel count in `$005c:$005b` against `0x0046` (70) and
  sets zero-page `$006a` for the large-reflection path.  A gameplay dispatcher
  at ROM `0x21e3b5-0x21e3f3` converts that state to event `0x08`; another
  consumer at `0x4be90c-0x4be95f` requires it for three consecutive samples,
  consistent with a held preparation gesture.
- I/O or register involved: A new host-only `SWORD` input selects the optical
  footprint returned through AN0.  It is deliberately separate from IN0/IN1,
  so pressing the host button does not invent a digital input on the original
  console.  Player-one Button 1 maps to the Win32 lightgun's left mouse button
  and also retains Left Ctrl/controller Button 1 alternatives.
- Hypothesis: Keeping the normal 3 by 3 (nine-pixel) image when released and
  presenting a 9 by 9 (81-pixel) image while held should cross the firmware's
  own 70-pixel threshold without directly changing any game state.
- Experiment: Rebuilt the focused target, placed both modes at identical
  center coordinates, and measured the firmware's processed pixel count,
  large-reflection flag, and reported X/Y position with isolated Lua probes.
- Result: Released mode reports count `0x0009`, `$006a=0`, X/Y `0x1e/0x1e`.
  Held mode reports count `0x0051` (81), `$006a=1`, and the same X/Y
  `0x1e/0x1e`.  Thus the game enters its native large-reflection path while
  the sword center remains stationary.  A two-second held vertical sweep then
  produced 120/120 large-reflection samples, kept the count at exactly
  `0x0051`, and moved the firmware's Y metric from `0x10` through `0x2e`.
  This confirms broad-mode motion still updates the raw Y coordinate, although
  the broad event deliberately bypasses the normal direction tracker and its
  handler does not consume Y.  Initial edge probes also showed that natural
  clipping could drop a nominal 9 by 9 image below 70 pixels.  The broad
  footprint is therefore bounded to the physical 32 by 31 sensor matrix; it
  retains 72-81 counted pixels at every host position.  Narrow mode keeps
  natural clipping and its full coordinate range.  Driver validation and ROM
  audit pass.  Actual magic/guard timing remains a gameplay test rather than a
  result inferred from the raw Y movement alone.
- Code changed: `src/mame/tvgames/xavix_2000.h` adds the dedicated input
  finder; `src/mame/tvgames/xavix_2000.cpp` defines the host control and varies
  only the synthesized CU5501A image radius.  No ROM, game RAM, shared input
  port, or game-specific event flag is patched.
- Final focused executable SHA-256:
  `6E0CF0F8B3552679AD2B1B32FB29A2DAF76249EFB5735AFCC52AEB29A8D852DA`.
- Standalone broadside test ZIP SHA-256:
  `95E578937FF44881F226BED253F3012466688057A732F66728EDAB16B4C5E3B0`.
- The test ROM remains unchanged at SHA-256
  `EC6BB5CE5796076C0B976D728B4AFF546DF52E9C8291FE178FDA8F01B96B450D`
  and is not present in the release archive.

### 2026-08-07: Match the host crosshair to the firmware cursor

- Observed symptom: The MAME crosshair and the game's white feather agreed
  near the centre but diverged progressively toward the edges.  In three user
  captures, the crosshair centres were approximately normalized X
  `0.641/0.246/0.468`, while the feather-root hotspots were
  `0.815/0.061/0.438`.  The error was therefore gain-dependent rather than a
  remaining horizontal inversion or a constant pixel offset.
- PC/address involved: The sensor coordinate conversion at ROM
  `0x008d28-0x008d97` writes signed calibrated X/Y displacements to low RAM
  `$026c:$026d` and `$0270:$0271`.
- I/O or register involved: `MOUSE0X/Y`, processed centroids `$0202/$0206`,
  calibration values `$1939-$193c`, and final coordinates `$026c/$0270`.
- Hypothesis: MAME's default crosshair displayed the unprocessed host
  lightgun position, while the game displayed the quantized, calibrated
  32-by-31 optical-sensor position.  The overlay should follow the latter
  without reducing the sensor motion delivered to the guest.
- Experiment: Pixel-measured the feather root in five steady X positions and
  five steady Y positions.  Guest X displacements were
  `-64/-32/0/32/48`; Y displacements were `64/32/0/-32/-64`.  The stable
  final firmware cursor followed `X = 128 + Qx` and `Y = 112 - Qy` in the
  native 256-by-224 visible area.  The asymmetric feather graphic's final
  shaft pixels can sit up to four pixels from this logical hotspot, and the
  first movement after a long idle showed a transient three-pixel difference;
  neither offset belongs in the persistent input mapping.  A diagnostic
  alternative compressed host X to sensor columns 24 through 8; although it
  reduced the positional gain, it also halved gesture displacement and made
  the automated calibration fail its large horizontal cut.  That experiment
  was fully reverted.
- Result: Dedicated crosshair mapper callbacks now read the firmware's signed
  coordinates, so the overlay inherits the same sensor quantization,
  calibration offsets, and update timing as the game.  Ten final native
  captures across the visible horizontal and vertical ranges place the blue
  crosshair at the firmware's logical hotspot; the crosshair ring covers the
  asymmetric feather shaft endpoint, whose drawn pixels can be up to four
  native pixels from that origin.  The full 32-column/31-row sensor mapping is
  unchanged; the normal calibration sequence still completes and reaches the
  king dialogue.  Driver validation and the ROM audit pass.
- Code changed: `src/mame/tvgames/xavix_2000.h` declares two UI-only mapper
  callbacks and a signed-coordinate reader.  `src/mame/tvgames/xavix_2000.cpp`
  attaches the callbacks to `MOUSE0X/Y` and maps `$026c/$0270` into the native
  visible area.  Sensor generation, gesture amplitude, the ROM, EEPROM data,
  and shared XaviX input behavior are unchanged.
- Final focused executable SHA-256:
  `9773100A0C549B12790A21F02488F61FC0F89EF8034B207C9E3037F09E018107`.
- Standalone precision-cursor test ZIP SHA-256:
  `92DC568C2186DBDD8CFFC8FAD366FEC2533205288F4E48B9551952C77843CFFC`.

### 2026-08-07: Correct the sword-face reflection class

- Observed symptom: Holding the host broadside button produced the expected
  81 bright pixels and set `$006a`, but the sword-face tutorial in the user's
  `joy0-3` state did not advance.
- PC/address involved: After delaying the scripted state load until the
  machine had completed startup, the tutorial repeatedly read `$0066` at ROM
  bank `$43`, PCs `$f5d6/$f5dd`.  It did not consume `$006a` at this point.
- I/O or register involved: `SWORD` bit 0, bright-pixel count `$005c:$005b`,
  medium-reflection flag `$0066`, and 70-plus-pixel flag `$006a`.
- Hypothesis: The original 9 by 9 diagnostic footprint was too large for the
  sword-face gesture.  The firmware classifies 14 through 69 pixels as the
  reflection expected by this tutorial; 70 or more enters a different class.
- Experiment: Kept the same centre and input binding, but reduced the held
  footprint to 5 by 5.  Loaded the user's state after 120 startup frames,
  released for 30 frames, then held the broadside input continuously.
- Result: Released mode remained at count `0x0009`.  Held mode became count
  `0x0019` (25), `$0066=1`, and `$006a=0`.  The consumer read the asserted
  `$0066`, the tutorial inset changed to its successful sword-face animation,
  and the game advanced to the following dialogue by frame 120.  This proves
  both the save state and packaged left-button path were sound; the failure
  was the incorrect optical-area class.
- Code changed: `src/mame/tvgames/xavix_2000.cpp` changes only the held sensor
  footprint radius from four to two and documents the evidenced 14-69
  pixel class.  No guest flag, event, ROM byte, or input binding is patched.

### 2026-08-07: Crosshair presentation while the feather is active

- Observed symptom: Mapping the blue MAME crosshair to the firmware position
  made it accurate, but its 32-column sensor quantisation was visually abrupt.
  Showing it simultaneously with the game's white feather also made the same
  position appear as two competing pointers.
- I/O or register involved: This change is confined to MAME's host crosshair
  mapper.  Sensor ADC data, `$026c/$0270`, and all guest input remain unchanged.
- Hypothesis: A short display-only interpolation can soften ordinary overlay
  movement without delaying fast cuts, and the overlay should be moved
  off-screen whenever the game's own feather sprites are on-screen.
- Experiment: Smoothed firmware-coordinate changes below 48 native pixels by
  half the remaining distance per video update, snapping within one pixel and
  snapping immediately for larger cuts.  Identified the feather's nine
  consecutive graphics blocks at `$a17d80-$a18080`, scanning every sprite slot
  and requiring its decoded bounds to intersect the visible area.
- Result: In the user's feather-free tutorial state, a 32-pixel coordinate
  step is displayed over several frames while the blue crosshair remains
  visible.  In automated palace captures at left, centre, and right input
  positions, the feather graphics suppress the blue overlay.  Smoothing state
  is host presentation only, is invalidated on reset/postload, and is not part
  of the save-state signature; the user's existing `joy0-3` state still loads.
- Code changed: `src/mame/tvgames/xavix_2000.cpp/.h` add UI-only smoothing and
  visible-feather detection to the two crosshair mapper callbacks.  The ROM,
  optical image, calibration, and gesture classifier are unchanged.
- Final verification: The rebuilt target passes `-validate` and reports the
  `drgqst` ROM set good.  A fresh delayed load of the diagnostic copy of the
  user's `joy0-3` state again measured nine pixels released and 25 pixels held,
  observed `$0066=1`, displayed the success animation, and reached the next
  dialogue.  The release launcher starts the rebuilt core with sharp pixels.
  The package contains only `COPYING`, the focused core, the launcher, and the
  Traditional Chinese README; it contains no ROM, state, configuration, NVRAM,
  screenshot, or UI file.
- Milestone focused executable SHA-256:
  `B5E96BEBAF70C5A3149195BCC04808954A609A2E17252FA25931E0B99E1C11CD`.
- Milestone standalone package SHA-256:
  `E7BDD7F7CB637C97B536AD06D62EC7B6B03CC03072228336A9D2E01A08ABAA75`.
- The test ROM remains unchanged at SHA-256
  `EC6BB5CE5796076C0B976D728B4AFF546DF52E9C8291FE178FDA8F01B96B450D`
  and is not present in the package.
- At that milestone, the then-current `joy0-3.sta` remained byte-identical to
  its diagnostic copy at
  SHA-256 `76EB8997A2D964E18C90C2FDF5B754BF9F5EE06E51F113992F8F18E3AD79F247`
  and is not present in the package.

### 2026-08-07: Model the forward-step ultimate pose

- Observed symptom: A newer `joy0-3` state shows an inset of the player
  stepping toward the sensor.  Neither the normal narrow image nor the left
  button's sword-face reflection advances the prompt.
- PC/address involved: The state dispatcher points to ROM bank `$4b`, PC
  `$e90c`.  It reads `$006a`, clears debounce counter `$00a0` when the flag is
  absent, and requires three consecutive asserted samples at `$e951-$e957`.
  Success at `$e95c-$e975` sets the native ultimate-move state and proceeds to
  `$e98c`.
- I/O or register involved: Sensor bright-pixel count `$005c:$005b`, medium
  reflection flag `$0066`, very-large reflection flag `$006a`, and the host-only
  `SWORD` input port.  The consumer does not read coordinates, direction,
  `$0064`, `$0269`, or the event slots at `$1b90-$1b94`.
- Hypothesis: Stepping toward the original optical sensor enlarges the apparent
  reflection.  The pose should therefore use the firmware's existing 70-plus
  pixel class rather than synthesizing a forward coordinate or patching a game
  flag.
- Experiment: Loaded a byte-for-byte diagnostic copy of the new state and
  presented centred 3 by 3, 5 by 5, and 9 by 9 images.  Repeated the 9 by 9 test
  at centre, left, right, top and bottom positions, then limited it to a short
  held interval.  After implementation, exercised `SWORD` modes `00`, `01`,
  `02`, and `03` through the real sensor callback.
- Result: 3 by 3 reports nine pixels and remains in the prompt.  5 by 5 reports
  25 pixels, asserts only `$0066`, and remains in the prompt.  9 by 9 reports
  81 pixels when centred, asserts `$006a`, passes the three-sample debounce,
  and remains above the large-reflection threshold at every tested position.
  It enters the ultimate-move magic circle in all of those tests.  Final mode regression
  reports counts `9/25/81/81` for modes `00/01/02/03`; only modes containing
  bit 1 enter the ultimate state, so the right-button mode has precedence when
  both buttons are held.
- Code changed: `SWORD` bit 1 is a host-only `IPT_BUTTON2` field explicitly
  bound to `GUNCODE_BUTTON2`, which is the Win32 lightgun provider's physical
  right mouse button.  The explicit binding excludes `KEYCODE_LALT`, preventing
  Alt+Enter from momentarily selecting the ultimate pose.  `sensor_adc_r()`
  selects radius four for bit 1, radius two for the existing left-button mode,
  and radius one when released.  No guest input bit, RAM flag, ROM byte, state,
  EEPROM, or launcher option is changed.
- The new original state and its diagnostic copy match at SHA-256
  `59A6417D3B63958CD60A8197FFF9D0619F9DE731300C468AF206AFBC635813EE`.
- Final verification: The focused executable passes `-validate`, the `drgqst`
  ROM audit reports good, and the four-mode sensor regression completes with
  zero failures.  The packaged executable was re-hashed from inside the ZIP
  and matches the locally tested executable exactly.
- Final focused executable SHA-256:
  `7599DBF90B91DB742F21C1E959ED1E86E556729B0596A44580572ADC2EDB067C`.
- Standalone right-click ultimate package SHA-256:
  `2FF1688C56D367765BE41AD3A0A08621DA266C6E487F63179A8DC94E313E0B07`.
- The package contains exactly `COPYING`, `drgqst-core.exe`,
  `Play-drgqst-mouse.exe`, and `README-zh-TW.txt`.  It contains no ROM, state,
  configuration, NVRAM, screenshot, or UI file.  The original ROM remains
  unchanged at SHA-256
  `EC6BB5CE5796076C0B976D728B4AFF546DF52E9C8291FE178FDA8F01B96B450D`.

### 2026-08-07: Verify the native adventure-save flow

- Observed symptom: Loading the supplied `joy0-3` state with F7 reaches the
  end-adventure save prompt.  A fast downward slash selects the red yes choice,
  but the following long pauses and final static screen appear to be a hang.
- State used: The original state was copied to an isolated diagnostic tree
  before testing.  Both copies initially matched at SHA-256
  `523DE2033CBF1E3975725B3368813F7F84084A941E67BC13F4DC33D320042AE0`.
- PC/address involved: The first confirmation dispatches through ROM bank
  `$3a`, `$a88f`, and choice callback `$f0dd`.  Direction value `$0269=02`
  selects yes.  The native save path runs `$3a:a0ca`, `$3a:b83e`, and
  `$3a:bd7e`; checksum routine `$3a:c1c3` covers RAM `$1936-$1a05` and stores
  its result at `$1a06-$1a08`.  Each EEPROM page is sent by bank `$00`, PC
  `$b3e7`.  The normal post-save waits are `$3a:a122-$a12a` and
  `$3a:a90d-$a941`; final state `$3a:a97a` deliberately remains in the normal
  frame dispatcher at `$b671`.
- I/O or register involved: The XaviX I/O bit wiring sends SDA on I/O1 bit 3
  and SCL on bit 4 to the configured 24C08 device.  The firmware writes 14
  16-byte pages at EEPROM offsets `$000,$010,...,$0d0`, covering `$000-$0df`,
  with device write byte `$a0`.  ACK handling is at `$00:b55f/$00:b5b1`; the
  ten-attempt retry counter at `$1915` was never consumed.
- Hypothesis: Either the game was stuck retrying a failed EEPROM transaction,
  or its original save-and-power-off sequence was being mistaken for a MAME
  hang.
- Experiment: Loaded a byte-identical diagnostic state, issued exactly one
  downward slash, and then supplied no further input for 600 frames.  Traced
  I2C edges and PCs throughout the transaction.  Separately launched a fresh
  MAME process with the newly written diagnostic NVRAM, without loading an F7
  state, and selected Adventure, Story, and the stage list.
- Result: I2C traffic began at post-load frame 212 and completed all 14 pages
  by frame 225 with no NACK retry or error path.  The game displayed the
  recorded message, waited, faded, and automatically reached its original
  "please turn off the power" screen.  The CPU continued running its regular
  frame idle loop around `$00:3bc5/$3bc7/$3bd4/$3bd6`.  On a cold restart the
  saved EEPROM skipped calibration and exposed the saved Stage 1 story entry.
  This proves there is no first save blocker: the apparent freeze is the
  intended end-adventure terminal state.
- Code changed: No emulator-source or game-specific workaround was added.
  Only `release/drgqst-mouse/README-zh-TW.txt` was updated to explain that one
  confirmation slash is sufficient, the following waits are intentional, and
  MAME should be exited normally from the power-off screen so its NVRAM can be
  flushed to disk.  It also warns that F7 restores the save state's older
  EEPROM snapshot and therefore is not a valid way to check the native save.
- Preservation: The user's original state, NVRAM, and ROM were not modified.
  Their SHA-256 values remain respectively
  `523DE2033CBF1E3975725B3368813F7F84084A941E67BC13F4DC33D320042AE0`,
  `CD8CC8558C2384C2F074AEDC818AEF968BB6D3D4412D5941A5D7196B30A61B3F`,
  and `EC6BB5CE5796076C0B976D728B4AFF546DF52E9C8291FE178FDA8F01B96B450D`.
- Updated native-save guide package SHA-256:
  `C3AA19E8F2819FD1140EF96A97D0834AE9BA0CE85D4DBAE92171287EFF701E5E`.
  It contains exactly `COPYING`, `drgqst-core.exe`,
  `Play-drgqst-mouse.exe`, and `README-zh-TW.txt`; no ROM, state, NVRAM,
  configuration, screenshot, or UI data is present.
- The executable inside that package remains byte-identical to the previously
  tested right-click build at SHA-256
  `7599DBF90B91DB742F21C1E959ED1E86E556729B0596A44580572ADC2EDB067C`.

### 2026-08-07: Restore calibrated cursor mapping in XaviXEmu

- Observed symptom: The standalone player's blue marker again diverged from
  the in-game feather by approximately the same amount as the earlier three
  MAME captures.  The error grew and reversed sign away from the centre.
- PC/address involved: The unchanged firmware conversion at ROM
  `0x008d28-0x008d97` writes its signed final X/Y displacements to low RAM
  `$026c:$026d` and `$0270:$0271`.
- I/O or register involved: Host mouse X/Y, the 32-by-31 CU5501A image, and
  the firmware's calibrated cursor coordinates in `$026c/$0270`.
- Hypothesis: The standalone renderer had retained a raw `g_mouse_x/y`
  overlay even though the game renders its feather from the calibrated,
  quantized sensor result.  This reintroduced the already-diagnosed mismatch;
  the shared 4:3 viewport calculation was not responsible.
- Experiment: Re-measured the three original images.  Normalized raw marker X
  values `0.641/0.246/0.468` correspond to feather hotspots
  `0.815/0.061/0.438`, or approximately
  `feather = 1.899 * mouse - 0.420`.  Added a core read-only cursor helper,
  mapped `X = 128 + s16($026c)` and `Y = 112 - s16($0270)` through the same
  viewport as the game, and ported the existing presentation-only smoothing.
  Large 48-pixel-or-greater cuts snap immediately; smaller changes move half
  the remaining distance per emulated frame and settle within one pixel.
- Result: The full 32-column/31-row sensor input remains unchanged, preserving
  calibration and gesture amplitude.  New signed-coordinate, smoothing,
  native viewport and 4:3 viewport regressions pass.  In a live F7-state test,
  left, centre and right host positions place the marker at the firmware's
  actual judged position, including the expected edge gain and clipping.
  The marker remains hidden while the game's feather sprites are visible.
- Code changed: `standalone/drgqst-player/src/core/drgqst_core.c/.h` exposes
  the read-only firmware hotspot; `cursor_presentation.c/.h` owns UI-only
  smoothing and viewport mapping; `main.c` updates it once per emulated frame
  and invalidates it on ROM or runtime-state load.  Sensor ADC data, ROM,
  EEPROM, save-state payloads and gesture thresholds are unchanged.
