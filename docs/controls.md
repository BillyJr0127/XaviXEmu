# Controls

These mappings translate conventional mouse and keyboard input into synthetic
optical-reflector patterns. They are usability controls, not a claim that the
original accessories used ordinary buttons.

## Common

- Mouse movement: move the active virtual reflector or game-owned cursor.
- F5: save the single runtime-state slot where supported.
- F7: load the runtime-state slot where supported.
- F8: save a local PNG screenshot.
- Alt+Enter: toggle borderless fullscreen.
- Escape: leave fullscreen.

## `drgqst`

- Mouse movement: narrow sword-edge reflection.
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

## `ttv_lotr`, `ttv_sw`, and `ttv_swj`

Move the mouse in a quick horizontal, vertical, or diagonal stroke. These
games draw a special cursor in some scenes. XaviXEmu hides its blue target
while that game-owned cursor is visible and restores the target in scenes that
do not provide one.

- `ttv_lotr`: hold right mouse for the upright broad-face defensive posture
  used to prepare Fire of Arnor. Ordinary mouse strokes remain attacks.
- `ttv_sw` / `ttv_swj`: hold left mouse for the broadside defensive posture.
  The US program receives a one-pixel, one-frame moving-edge sample for an
  ordinary mouse stroke; this avoids confusing motion with a held defense.
  Hold Space for the lightsaber spin gesture; this rotates an elongated
  optical reflection through vertical, diagonal, and horizontal orientations.

## `ttv_mx` and `tom_jump` (experimental)

The original tilt controller reports four digital directions rather than an
optical position. Move the mouse away from the centre of the game picture to
tilt the virtual controller; a neutral region around the centre prevents small
movements from steering. Keyboard directions take priority when held.

- Mouse position or Up/Down/Left/Right or W/S/A/D: tilt direction.
- Left mouse or Space: accelerator.
- Right mouse or Ctrl: brake.
- Middle mouse or P: pause.

Rendering and the hardware input path are under active verification. These
mappings are usability controls and do not claim to reproduce the physical
shape or travel of the original accessory.

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

## XaviX 2 wrist-reflector games (experimental)

- Mouse movement sends synthetic wrist-reflector positions.

Naruto (`ban_naru`) places both samples at the same location for its documented
joined-hands guard gesture; left mouse is its verified Execute/confirm input.
Blue Dragon (`ban_bldj`) separates the second sample vertically by two sensor
units because its firmware merges perfectly overlapping blobs. Holding right
mouse or Space therefore operates its on-screen `決定` gesture without a
separate left-click input.

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

Independent two-hand positions and the complete later gameplay gesture
vocabulary are not yet implemented.

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

## `tvpc_dor` (experimental)

- Mouse movement: update the two cumulative hardware mouse counters. The game
  draws its own cursor, so XaviXEmu does not overlay the blue target.
- Left mouse: hardware mouse button.
- Up/Down/Left/Right or W/S/A/D: the TV-PC cursor keys.
- Vertical mouse movement: emit short Up/Down cursor-key pulses. Alternating
  upward and downward motion operates the take-copter flight exercise in the
  supplied diagnostic checkpoint.
- Escape: the TV-PC Escape key while windowed; when fullscreen it first leaves
  fullscreen as usual.

The title, main menu, keyboard scan path, and take-copter input are verified.
F5/F7 runtime states are available, and the game keeps a separate 24C16 EEPROM
save beside the executable. Most text keys and complete program paths remain
unknown.
