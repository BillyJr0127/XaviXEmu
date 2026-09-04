# Controls

These mappings translate conventional mouse and keyboard input into synthetic
optical-reflector patterns. They are usability controls, not a claim that the
original accessories used ordinary buttons.

## Common

- Mouse movement: move the active virtual reflector or game-owned cursor.
- F5: save the single runtime-state slot where supported.
- F7: load the runtime-state slot where supported.
- F8: save a local PNG screenshot.
- F9: start a fixed-size, 60 FPS recording. Choose **View > Recording format**
  for MJPEG AVI or H.264/AAC MP4. The default **View > Record at current
  display size** option uses the current game viewport size and records the
  presented host cursor. The recording size remains fixed if the window or
  guest video mode changes while recording.
- F10: stop and finalize the recording. Recordings include synchronized 48 kHz
  stereo sound and are saved under the local `snap` directory. The selected
  format is remembered in `XaviXEmu.ini`.
- F11: toggle runtime timing diagnostics.
- Alt+Enter: toggle borderless fullscreen.
- Escape while a game is running: pause and show the exit confirmation. Press
  Escape again to close the game and return to the library, or Enter to
  resume. Holding Escape cannot accidentally confirm because key repeats are ignored.

## PS / gamepad and Wii Remote

Open **Controller > Controller settings** after loading a game, or right-click
a title whose host controls are already implemented in the game library. The
right-click command is deliberately absent for titles that only boot but do not
yet have usable controls. The dialog shows only that title's real actions—such
as Dragon Quest attack/defense/magic/special, a racer's accelerator/brake and
accessory buttons, or Star Wars saber actions—and hides unused generic slots.
Each game's action-button bindings are stored in a separate
`Controls.<shortname>` section of `XaviXEmu.ini` beside the executable; input
source, dead zone, single-reflector stick, and Wii calibration remain shared
hardware preferences.

- PS and compatible Windows gamepads: the left analog stick supplies
  reflector 1 and the right analog stick supplies reflector 2. The sticks move
  their reflectors like a mouse: releasing a stick leaves its point at the last
  position, and the next movement continues from there. A slight tilt moves
  precisely while full deflection accelerates enough for quick two-hand
  gestures such as Naruto's inward convergence. For games with only one
  physical reflector, choose whether the left or right stick is used.
- Click a game action in the settings window, release all buttons, and press
  the gamepad or Wii Remote button that should perform it. Bindings are kept
  separately for each recognized ROM profile.
- Two Wii Remotes: pair both controllers with Windows and use a powered sensor
  bar. Select the Wii source, aim Wii 1 and Wii 2 at the requested upper-left
  and lower-right screen positions, and press the four calibration buttons.
  Each remote then supplies one independent reflector position.
- Wii IR supplies position but cannot measure the large reflective area of the
  original accessories by itself. Defense, special, two-hand, and deflect
  patterns therefore use the configurable action buttons. Accelerometer-based
  gesture recognition remains future work.

The Wii HID path supports the original Wii Remote and Wii Remote Plus product
IDs through Windows' built-in HID stack. Actual Bluetooth adapters and drivers
vary, so this first implementation remains hardware-validation work in
progress. Mouse control remains the default until another source is selected.

## `drgqst`

- Mouse movement: narrow sword-edge reflection.
- On a gamepad, the selected analog stick starts slowly for precise aiming and
  accelerates only while it remains pushed. A sustained full-tilt stroke can
  therefore cross the original game's vertical-confirm and attack threshold,
  while a quick tap does not throw the point to the screen edge. Releasing or
  reversing the stick resets the acceleration and still holds the last point.
- Hold left mouse while moving: broad sword-face reflection for magic and
  broad-area guarding.
- Hold right mouse: step-forward/ultimate-technique posture.

## `ban_onep`

- Left mouse: left punch and menu O.
- Right mouse: right punch and menu X.
- Both mouse buttons: move both virtual wrist reflectors forward.
- Space: short paired-reflector guard/bazooka gesture.
- Up/Down or mouse wheel: change applicable menu selections.
- During Zoro scenes, hold right mouse and make a deliberate horizontal or
  diagonal drag. The game classifies the path; it is not free-aim input.

## `ban_omt`

- Mouse movement: front reflector for direction selection and seal drawing.
- Left mouse: menu O.
- Right mouse: menu X.
- Space or both mouse buttons: broad reverse-side reflection.

## 	tv_lotr`, 	tv_sw`, and 	tv_swj`

Move the mouse in a quick horizontal, vertical, or diagonal stroke. These
games draw a special cursor in some scenes. XaviXEmu hides its blue target
while that game-owned cursor is visible and restores the target in scenes that
do not provide one.

- 	tv_lotr`: hold right mouse for the upright broad-face defensive posture
  used to prepare Fire of Arnor. Ordinary mouse strokes remain attacks.
- 	tv_sw` / 	tv_swj`: hold left mouse for the broadside defensive posture.
  The US program receives a one-pixel, one-frame moving-edge sample for an
  ordinary mouse stroke; this avoids confusing motion with a held defense.
  Hold Space for the lightsaber spin gesture; this rotates an elongated
  optical reflection through vertical, diagonal, and horizontal orientations.

## 	tv_mx` and 	om_jump` (experimental)

The original tilt controller reports four digital directions rather than an
optical position. Move the mouse away from the centre of the game picture to
tilt the virtual controller; a neutral region around the centre prevents small
movements from steering. Mouse distance and the PS/gamepad left analogue-stick
deflection are converted to graduated digital pulses, so small movements make
short corrections while near-full deflection holds the tilt switch. Keyboard
directions take priority when held.

- Mouse position or Up/Down/Left/Right or W/S/A/D: tilt direction.
- Left mouse or Space: accelerator.
- Right mouse or Ctrl: brake.
- Middle mouse or P: pause.
- PS/gamepad left analogue stick: proportional virtual tilt.
- Configured Primary/Confirm, Secondary, and Special controller actions:
  accelerator, brake, and pause respectively.

Rendering and the hardware input path are under active verification. These
mappings are usability controls and do not claim to reproduce the physical
shape or travel of the original accessory.

## Early XaviX racing boards (experimental)

All seven profiles accept Up/Down/Left/Right or W/S/A/D, mouse position around
the centre of the game picture, and the PS/gamepad left analogue stick. Mouse
and analogue deflection use graduated digital pulses, so a small deflection
makes short corrections instead of holding the original switch continuously.

- `rad_mtrk`: Left/Right steers the original 20 Hz encoder; Up/Space/left mouse
  or Primary is throttle-high, Down/Ctrl/right mouse or Secondary is
  throttle-low, N/middle mouse/Special is nitro, R/Defense is reverse, and
  Enter/H/Confirm is horn.
- `rad_snow`, `rad_ssx`, `rad_sbw`: Left/Right controls board lean;
  Up/Space/left mouse/Primary is forward/go, and Down/Ctrl/right mouse,
  Enter, Secondary, or Confirm is select.
- 	ak_gin`: four directions directly control the snowboard; Primary/Space or
  left mouse also supplies forward/up.
- 	carnavi`: Left/Right steers; Up/Space/left mouse/Primary accelerates;
  Down/Ctrl/right mouse/Secondary brakes; C/middle mouse/Special is siren or
  transform; R/Defense is reverse; Enter/K/Confirm is the key; L/Deflect is
  lights; H/Two-hand is horn; V is wipers; M opens the menu.
- 	omthr`: directions move and steer the rescue vehicle; Space/left mouse,
  Enter, Primary, or Confirm is horn/select; Ctrl/right mouse, I, or Secondary
  is ignition; M/middle mouse/Special is map; V/Defense is wipers; L/Deflect is
  headlights; Q/Two-hand supplies the microphone input.

These mappings reproduce the observed firmware input bits and encoder timing;
physical accessory travel and later gameplay still require hardware testing.
## 	ak_chq` (experimental)

The original set has a separate steering-wheel grip for each player. The grip
contains accelerator, brake, and rear command buttons.

- Left/Right or A/D: progressively turn the player-one wheel; short taps make
  small corrections, and releasing the keys smoothly returns it to centre.
- Hold left mouse (accelerator) or right mouse (brake) and drag left/right for
  relative steering. Releasing the button lets the wheel return to centre;
  absolute cursor position no longer pins the wheel at a screen edge.
- PS/gamepad left analog X: player-one wheel; right analog X: player-two wheel.
  The centre is deliberately fine-grained and full stick reaches the game's
  verified useful wheel range instead of overdriving the wheel counter.
- Up, W, Space, left mouse, or Primary/Confirm: accelerator and menu confirm.
- Down, S, Ctrl, right mouse, or Secondary: brake and menu cancel.
- C, middle mouse, or Special: command button / item use.

The wheel uses the game's wrapping ANPORT2/ANPORT3 counters, whose untouched
centre is `$ff`, while the three buttons use the verified active-high P0 bits
`$20/$10/$80`. The game draws its own menu and race presentation, so no host
target is overlaid.

## `epo_ebox` (experimental)

The original boxing controller exposes ordinary active-high digital controls;
it does not use the optical-sensor or ANPORT models.

- Left mouse or Space: select/OK.
- Right mouse or Ctrl: back/cancel.
- Mouse position outside the central neutral area, or Up/Down/Left/Right or
  W/S/A/D: menu and game directions.

Bits `$04` and `$08` are intentionally unbound. The game draws its own menu
selection graphics, so XaviXEmu does not overlay the blue target. Gameplay and
control feel remain experimental.

## `epo_dtcj` (experimental)

The original Doraemon Take-copter is a head-mounted tilt controller. Its
receiver reports a 4-by-4 forward/back/left/right grid through the game's own
infrared interrupt and filtering code.

- Up/Down/Left/Right or W/S/A/D: tilt the virtual Take-copter.
- Left mouse or Enter: hold a filtered forward tilt long enough to start or
  confirm a screen.
- Mouse: hold the right button and drag left/right for head roll or up/down for
  backward/forward tilt. Releasing the right button returns to neutral, so one
  menu gesture cannot remain held and overlap the following selection.
- PS/gamepad: the analogue stick chosen in Controller settings supplies direct
  head tilt and naturally returns to neutral when released.
- A Wii pointer outside the central neutral area supplies the same tilt.
- No host centre marker is drawn: the physical head-mounted controller is not
  a screen pointer. Tilt remains active through keyboard, mouse and controller.
- Diagonal positions combine one horizontal and one vertical direction.
- Middle mouse or Space: synthesize the filtered quick backward-to-forward
  acceleration gesture. Continuous mouse, analogue-stick and Wii motion is
  passed through directly, so crossing the centre cannot replace the player's
  steering with a prerecorded gesture.
- The per-game controller page exposes forward, backward, left, right, upright
  and boost as separate bindable actions. The selected PS analogue stick still
  supplies continuous direct tilt.
- On the verified opening exercise, hold Left for about one second until its
  gauge completes, then release back to the centre. This advances through the
  game's own state machine.
- Later prompts consume the same filtered directions. Return the pointer to the
  middle whenever the physical controller is expected to be upright.

The native 27-bit packet decoder, opening exercise, clean transition, changing
scene graphics, and continued PCM output are verified. Later activities and a
complete play-through remain experimental. F5/F7 runtime states and the
separate 24C04 EEPROM file are available.

## XaviX 2 wrist-reflector games (experimental)

- Mouse movement sends synthetic wrist-reflector positions.

Naruto (`ban_naru`) places both samples at the same location for its documented
joined-hands guard gesture; left mouse is its verified Execute/confirm input.
XaviXEmu no longer draws its old blue diagnostic target over Naruto; only the
game-owned cursor is presented, while optical hit testing remains active during
transitions where that cursor is temporarily hidden.
Blue Dragon (`ban_bldj`) separates the second sample vertically by two sensor
units because its firmware merges perfectly overlapping blobs. Holding right
mouse or Space therefore operates its on-screen `決定` gesture without a
separate left-click input. Its gamepad profile now exposes the battle depth
gestures that were previously missing from the settings:

- Primary action / left mouse: close and reopen reflector 1 to attack.
- Secondary action / right mouse: close and reopen reflector 2 to attack.
- Special or two-hand action / Space: close and reopen both reflectors.

The mouse buttons continue to provide their existing menu inputs, so the same
profile can enter the game and fight without switching mappings.

DB2J and DBZ receive packets at their verified firmware buffers and now use
their game-owned optical cursors after the signed cursor-bound CPU instruction
was identified. Move the mouse and leave the two markers over an on-screen
target briefly; these menus confirm by dwell rather than by a click.

For DB2J, left mouse selects the red motion-game route at the title and right
mouse selects the blue card route. Left/right mouse or Space also synthesizes
the observed open/closed reflector-area transition for later gesture screens.
When the markers are over a story arrow, the same button pulse briefly moves
them out and back in to generate the edge used to turn verified Shenron and
Kame House pages. Later scenes are still being mapped.

DBZ has no verified PIO button; its receiver-present line and optical dwell
selection are modeled separately. Its battle mappings are:

- Left mouse: close and reopen one hand for a basic attack.
- Right mouse: close and reopen both hands for the two-hand technique input.
- Space: synthesize a rapid horizontal sweep used to deflect an incoming shot.

The game firmware consumes all three actions. Enemy encounters and the full
visible result of each action have not yet been exercised through a complete
play-through, so these mappings remain experimental.

PS/gamepad dual sticks and two Wii Remotes can provide independent two-hand
positions. The complete later gameplay gesture vocabulary is not yet known.

F5 and F7 save and restore an independent runtime-state file for each XaviX 2
ROM. These files include the audio engine state and are suitable for privately
sharing a reproducible problem point without sharing the ROM.

The **XaviX2 聲道 / Channels** menu is a live audio diagnostic. Checked
channels are audible; clearing a check mutes only the host output while the
voice continues advancing and remains active to the game. Active entries show
the calculated source rate, left/right volume, and `loop` state; `release`
identifies a note currently fading after the game's key-off command. **全部開啟 /
Enable all** restores every channel. Different rates are intentional firmware
pitch settings and should not be interpreted as a global speed error.
Use **全部靜音 / Mute all**, then check one active channel at a time, to isolate
a stale loop without changing the game's audio timing.

## `epo_hamd` (experimental)

The original accessory has two wireless bell-shaped controllers with vibration
sensors. XaviXEmu sends a short shake packet for each hand through the
firmware's real I/O-event and serial receive path.

- Left mouse: shake the left controller once.
- Right mouse: shake the right controller once.
- Space: shake both controllers once.
- Enter or middle mouse: confirm the current menu option through the separate
  digital input used by the firmware.

The remaining motion fields and complete activities are still experimental.
F5/F7 runtime states are available.

## 	vpc_dor` (experimental)

- Mouse movement: update the two cumulative hardware mouse counters. The game
  draws its own cursor, so XaviXEmu does not overlay the blue target.
- Left mouse: hardware mouse button.
- Up/Down/Left/Right or W/S/A/D: the TV-PC cursor keys.
- Vertical mouse movement: emit short Up/Down cursor-key pulses. Alternating
  upward and downward motion operates the take-copter flight exercise in the
  supplied diagnostic checkpoint.
- Escape is reserved for the emulator pause/exit confirmation.

The title, main menu, keyboard scan path, and take-copter input are verified.
F5/F7 runtime states are available, and the game keeps a separate 24C16 EEPROM
save under the local `save` directory. Most text keys and complete program paths remain
unknown.

## 	om_dpgm` (experimental)

Disney Princess uses the standard optical-wand path. The game draws its own
wand, so XaviXEmu does not add a blue host target.

- Mouse movement or the configured analog reflector: move the wand.
- Left mouse / Primary action: broad, face-on reflection.
- Right mouse / Secondary action: forward-pointing reflection.
- Circular and sweeping motion are sent continuously rather than as digital
  button taps; the heart-alignment tutorial responds to this path.

F5/F7 runtime states and a separate 24C08 EEPROM save are available.

## 	vpc_hk` (experimental)

Hello Kitty uses the original TV-PC cumulative mouse counters and full 8x8
external keyboard matrix. The game draws its own bow cursor, so XaviXEmu does
not add a blue host target.

- Mouse movement: move the game-owned cursor; left mouse is its single mouse
  button. Double-click by clicking twice normally.
- Number, QWERTY, and punctuation keys: the corresponding printed TV-PC keys.
  Unlike Doraemon, W/A/S/D remain printable letters.
- Up/Down/Left/Right: the first cursor-key cluster.
- Numpad 8/2/4/6: the second cursor-key cluster.
- Escape, Backspace, Enter, left/right Shift, and Space: their matching keys.
- Tab: the TV-PC Input Mode key.
- F2: the Family Mail shortcut.
- Right Ctrl: fallback for the extra Japanese `] / む` key that is absent from
  most US keyboards.

F5/F7 runtime states are available, and Hello Kitty keeps a separate 24C16
EEPROM save under the local `save` directory.
