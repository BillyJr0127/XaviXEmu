# ban_onep support investigation

ROM under test: user-supplied `ban_onep.zip` (read-only; not included in builds).

## First blocking problem

- Observed symptom: the unmodified standalone core reaches the title screen, but
  cannot press the displayed circle button and later hangs on a black screen.
- Address: title input is consumed at ROM `0x00c218`; the gameplay stall is in
  copied RAM code beginning at `0x0030a1` (first stable poll at `0x0030a5`).
- I/O involved: the title checks the rising edge of IN0 bit `0x01`.  Gameplay
  waits for ordered IN1 transitions: bit `0x02` low-to-high, followed by bit
  `0x04` high-to-low, while acquiring optical-sensor ADC samples.
- Hypothesis: MAME's independent random placeholders for IN1 bits `0x02/0x04`
  do not model the CU5501-family sensor synchronization waveform.
- Experiment: supply the four-phase sequence `00 -> 02 -> 06 -> 04` and expose
  synthetic reflector images through ADC 0.
- Result: execution leaves the black polling screen and reaches the first 3D
  ship battle.  CPU, timers, banking, video and the normal game flow remain
  active.  The existing Dragon Quest boot oracle also remains unchanged.
- Code changed: recognize the verified ROM, select a ban_onep core profile,
  model the ordered sensor sync, map mouse input to synthetic optical images,
  map the mouse buttons to IN0 bits `0x01`/`0x02`, apply the 24C04 address
  mask, and separate ban_onep EEPROM/runtime-state filenames from Dragon
  Quest saves.

## Menu input and dual reflectors

- Observed symptom: the user's F7 state stopped at the `CONTINUE?` screen and
  the original one-point optical model could not distinguish two wristbands.
- I/O involved: IN0 bit `0x01` is the displayed circle/retry input; IN0 bit
  `0x02` is the displayed cross/cancel input.  ADC 0 contains a dark reference
  pass followed by an illuminated 32 by 31 image.
- Experiment: pulse each IN0 bit independently from the same restored state,
  then render two independent reflector blobs and move the left, right, or
  both blobs toward the mouse aim point.
- Result: bit `0x01` restarts the match, bit `0x02` returns to the title, and
  the three optical experiments produce distinct left-punch, right-punch, and
  two-reflector images.  The camera image is mirrored relative to the arms on
  screen, so the mouse-button-to-reflector mapping compensates for that.  At
  peak extension the firmware measures a single-punch reflected area near
  `0x22` pixels and the simultaneous two-reflector image near `0x54`, proving
  that the broad double gesture remains distinguishable after acquisition.
- Code changed: left click is left punch/circle, right click is right
  punch/cross, and pressing both moves both reflectors forward together.

## Speed calibration

- Observed symptom: with each synchronization phase held for 16 reads, the
  retry state still displayed `START!!` and time 60 about 6.5 seconds later.
- Hypothesis: the arbitrary read divider made the busy-waiting acquisition
  routine consume too much emulated CPU time, slowing gameplay without
  changing the 60 Hz host presentation rate.
- Experiment: compare phase periods 1, 2, 4, 8 and 16 from the identical F7
  state, and compare the timeline with the supplied original-hardware video
  from 50 seconds onward.
- Result: advancing one ordered phase per read reaches `START!!` at about two
  seconds, then decrements the game timer once per real second (60, 59, 58,
  57, 56).  The reference video follows the same mission-title, `START!!`,
  and one-count-per-second sequence.  Period 16 remains several seconds late.
- Code changed: the One Piece profile now advances the synchronization phase
  once per IN1 read.  The CPU master clock and 60 Hz frame scheduler are left
  unchanged because both already match the XaviX configuration.

## Current status

Experimental support reaches gameplay with circle/cross menu input, separate
left/right reflectors, simultaneous two-reflector movement, and reference-rate
timing.  Exact punch strength/direction calibration and later character moves
are not yet hardware-verified, so control feel should be tuned from real play
testing rather than treated as cycle-accurate CU5501 emulation.

## Zoro directional sword input

- Observed symptom: in the user's Zoro F7 state, a stationary right click can
  show a sword pose, but ordinary mouse movement does not consistently produce
  a slash.
- I/O involved: the same illuminated CU5501-family optical image used for the
  wrist reflectors.  The right mouse button also keeps the menu-X IN0 bit
  asserted, which was tested independently and does not prevent the slash.
- Hypothesis: the firmware classifies a time-ordered reflector path rather
  than treating the sword as another momentary button.
- Experiment: from the identical state, feed twelve-frame left-to-right,
  right-to-left, vertical and diagonal paths through either synthetic
  reflector, then repeat the real right-button path at several enemy timings.
- Result: opposite horizontal directions produce distinct attacks.  Depending
  on the vertical component, the game displays horizontal, vertical or
  diagonal sword trails.  Pure vertical movement does not start a slash in
  this scene, showing that deliberate horizontal displacement is the trigger.
  At matching enemy timings both horizontal directions score a hit: the
  counter advances from 11 to 12 and one run displays `1匹!`.
- Code changed: a stationary right click keeps the pre-shaped Luffy right
  punch.  Once the held pointer moves at least six normalized units horizontally,
  the right reflector follows the raw mouse trajectory at full extension until
  release, allowing the ROM's own classifier to choose the Zoro slash.

## Repeated F7 load stability

- Observed symptom: repeatedly pressing or holding F7 can make the GUI stop or
  exit with a high probability.
- Area involved: the front end closed, destroyed and reopened the asynchronous
  Windows `waveOut` device after every successful state load.  F5/F7 also
  accepted keyboard auto-repeat, and One Piece gesture/button phases survived
  a load even though they are live host input rather than machine state.
- Hypothesis: rapid loads repeatedly retire and reuse device buffers while
  also carrying a half-finished synthetic gesture into the restored frame.
- Experiment: keep the already-open audio device, reject repeated keydown
  messages, clear host gesture/sync transients after restore, and drive the
  real hidden Win32 application with distinct F7 key presses.
- Result: the revised application remained alive for 2,000 consecutive loads,
  then for another 1,000 loads using the user's newer Zoro checkpoint.  The
  core state test also performs 5,000 repeated One Piece restores, and all
  eleven automated test programs pass.
- Code changed: F5/F7 act once per physical keydown; F7 no longer recreates
  `waveOut`; restored One Piece states restart synthetic input edges, gesture
  progress and sensor synchronization from a clean host-side state.

## Original-hardware Zoro presentation

- Reference: the supplied original-hardware gameplay video around 4:27-4:57.
- Result: the Zoro fight shows no free-aim cursor.  Directional prompts select
  fixed horizontal/vertical/diagonal first-person sword animations aimed at
  the active enemy.  This supports treating the mouse as a timed directional
  gesture, not promising that the final pointer coordinate is an exact slash
  location.  In local frame probes the drawn line matches an earlier sampled
  mouse point; continued motion before the delayed animation appears explains
  why the current blue marker can look displaced from the sword trail.
