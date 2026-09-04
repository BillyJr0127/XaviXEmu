# Changelog

All notable public changes will be documented in this file. The project uses
semantic versioning while experimental releases carry a pre-release suffix.

## Unreleased
- Reapply the saved 1x-4x window scale after a game is launched from the ROM
  library. A selected 4x window therefore remains 4x after restarting the
  application instead of inheriting the smaller 1080x720 library window.
- Decode XaviX 2 translucent palette entries as the hardware's interleaved
  premultiplied RGB444 plus three-bit `Nalpha`, rather than fixed one-half
  transparency, with `Nalpha=7` as the indexed-texture transparent endpoint.
  Take-copter's water palette now produces the wide cyan/white
  distance haze that hides the sea/sky seam, and its three overlapping cloud
  planes no longer form a dark rectangular background. The same generic path
  is used by Blue Dragon, Naruto and both Dragon Ball titles. Enhanced 2x
  presentation now also retains the 3D contribution beneath partially
  transparent foreground sprites, avoiding a presentation-only seam where
  Dragon Ball's red energy aura crosses 3D geometry while keeping fully opaque
  sprite artwork pixel-sharp. The active-line compatibility path now recognizes
  Dragon Ball's depth-zero tiled fighter followed by its large attack sprite:
  only that later effect is restored after the fighter, so its opaque core
  covers the enemy as on hardware. This includes the weak red sphere, which
  remains depth zero while growing past the generic large-backing threshold,
  as well as the charged nonzero-depth form. Other foreground objects retain their
  established depth ordering, preventing repeated logos on the trial clear
  screen.
- Correct XaviX 2 premultiplied alpha composition from SSD's RPU patent.
  Take-copter's white/cyan horizon haze and Dragon Ball's shared translucent
  layers no longer halve their stored foreground RGB a second time. Match the
  Take-copter F7 flight/fade sequence to hardware at one IRQ-7 event per 60 Hz
  frame in every display mode; the logo/carousel width no longer incorrectly
  doubles GPU submissions and makes menu animation or audio miss real time.
  PCM pitch remains on its independent 98,437,488 Hz source. Implement the RPU
  sprite `Filter=0`
  four-tap sampler, including interpolation of premultiplied alpha, so the
  scaled Take-copter flying characters no longer retain coarse nearest-neighbor
  edges. Emulate the title-programmed `$e620` 2-by-2 Gouraud dither pattern to
  soften RGB555 sky gradients without applying an invented whole-frame blur.
  Merge Take-copter's GPU0/GPU1 submissions across the `$e408/$e414` pair:
  its ROM-provided depth-zero full-screen transition now composites after the
  clouds and characters, so the entire scene fades toward pale blue/white
  together instead of repainting clear sprites over an already-white sky.
  Restore the observed Type-1 backdrop then Nalpha-graded Type-0 water
  class order, reducing the recent hard white horizon strip.
- Add dedicated `epo_dtcj` Take-copter controls. Keyboard directions, right-
  mouse drag, the selected PS/gamepad analogue stick, and Wii pointer input now
  feed the firmware's verified 4-by-4 head-tilt receiver; left mouse/Enter
  supplies a filtered forward/start gesture, and middle mouse/Space or a
  bindable action performs the original quick backward-to-forward boost.
  Per-game settings expose forward, backward, left, right, upright, and boost;
  the former orange host target is hidden because head tilt is not a screen
  cursor.
- Add an in-game Escape confirmation: the first press pauses emulation and
  audio, a second Escape safely saves EEPROM and returns to the game library,
  while Enter resumes without advancing the guest during the prompt.
- Add a PCSX2-style game library. Users can save a ROM directory in the local
  INI, browse every mounted drive, recursively scan exact supported ZIPs in all
  subfolders, view formal title, XaviXEmu's own
  verified green/yellow/red support status, release year, XaviX generation,
  maker, filename and a 160x120 screenshot thumbnail, then sort by any column.
  Double-click/Enter launches; right-click opens a title-specific control guide
  and that ROM's independent INI button bindings. F8 screenshots use the ROM
  shortname so the first capture is reused as the game's thumbnail.
- Recognize `rad_mtrk`, `rad_snow`, `rad_ssx`, `rad_sbw`, `tak_gin`,
  `tcarnavi`, and `tomthr` by exact size/CRC32/SHA-1. Add an original-XaviX
  plain-input profile for 1/2/4 MiB boards without injecting Hamtaro wireless
  packets, plus independent F5/F7 state identities.
- Add keyboard, mouse, and PS/gamepad analogue controls for the seven early
  racing/vehicle systems. Model Monster Truck's direction bit and 20 Hz wheel
  pulse interrupt, use graduated duty-cycle steering/board lean for fine
  analogue corrections, and expose the observed vehicle accessory functions
  including throttle, brake, reverse, nitro, horn, key, siren, map, lights,
  wipers, menu, and microphone inputs.

- Complete host controls for `ttv_mx` and `tom_jump`: keyboard and mouse keep
  the original digital tilt/buttons, while the PS/gamepad left analogue stick
  uses graduated duty-cycle steering for fine corrections. Configured
  Primary/Confirm, Secondary, and Special actions map to accelerator, brake,
  and pause.
- Add Choro-Q wheel, accelerator, brake, and command controls for mouse,
  keyboard, and PS/gamepad analog sticks. Keyboard steering now ramps slowly
  through the verified useful range, held-button mouse steering is relative
  and lower-sensitivity, and PS/gamepad steering uses a centre-precision curve
  instead of overdriving the wheel input. Encode the centred virtual wheel
  around the hardware's wrapping `$ff` counter; the earlier `$80` neutral value
  was interpreted as a half-turn and permanently biased the car. Restore
  `$6ffa/$6ffb` position-register
  readback so the game's per-scanline scroll tables bend and scale the road;
  partial rendering retains the absolute raster origin so the dashboard is
  not repeated and the road remains in the racing viewport.
  Correct SSD 2000 ADC/SBC behavior:
  the D flag is preserved architecturally, including across interrupts, while
  arithmetic remains binary. This fixes both the CPU-car corner search and the
  command-ring corruption that previously froze the demo after a few seconds.
## 0.3.1 - 2026-08-17

- Correct XaviX 2 Type-1 premultiplied-alpha blending and merge nonzero-depth
  polygons with sprite depth bands. Blue Dragon's deforming boss-shadow
  connector is no longer attenuated twice or hidden behind the dragon-body
  sprites, so it remains visibly joined as transparency increases down the
  strip.
- Fix Kenshin Dragon Quest analog-stick strokes being too slow for the game's
  vertical-confirm and sword-attack motion classifier. Its higher top speed is
  reached through a gradual hold acceleration so brief taps remain precise.
- Add Blue Dragon battle actions to the controller profile. Primary and
  secondary actions close/reopen reflector 1 or 2, while special/two-hand
  closes and reopens both; these now drive the firmware's verified per-hand
  battle events instead of the unrelated Naruto Execute input.
- Add controller settings for PS and compatible Windows gamepads. The left and
  right analog sticks become two independent reflectors, single-reflector
  games can select either stick, and game actions can be rebound per ROM and
  retained in `XaviXEmu.ini`. Analog input is relative like dragging a mouse:
  each reflector stays at its last position when its stick returns to centre.
- Add an initial native Windows HID path for two Wii Remotes. Each remote
  supplies an independently calibrated IR reflector position, while
  configurable buttons provide large-area, defense, special, two-hand, and
  deflect actions that IR position alone cannot identify. This path requires a
  sensor bar. The maintainer does not own Wii Remote hardware, so this path
  remains unverified on physical controllers and awaits user reports.
- Add the 0.3.1 front end: `XaviXEmu.ini` beside the executable stores
  language, display, and controller preferences; Japanese and French join
  Traditional Chinese and English. Automatic selection follows Traditional
  Chinese, Japanese, and French Windows locales, with English as the fallback.
- Correct the internal Japanese/English language-table ordering so manual and
  automatic language selection always load the intended translation.
- Store EEPROM and F5/F7 runtime states in the local `save` directory and
  store both PNG screenshots and MJPEG AVI recordings in `snap`. Existing
  state files beside the executable and the older user-data location remain
  readable and are migrated on the next successful load.
- Add exact image recognition and isolated runtime-state files for five EPOCH
  XaviX 2 titles: `epo_dab2j`, `epo_dtcj`, `epo_pabj`, `epo_ssk2`, and
  `epo_sskj`. The first three reach verified startup/tutorial/name-entry
  screens; the two SASUKE titles remain explicitly not working.
- Route the EPOCH XaviX 2 board's PIO16/P16 SDA and PIO17/P17 SCL traffic to
  the existing 24C04-compatible serial EEPROM model. Bandai XaviX 2 titles
  retain their existing PIO20/PIO21 24C08 route.
- Add separate 512-byte 24C04 save files for the documented `epo_dtcj`,
  `epo_ssk2`, and `epo_sskj` boards. Deferred writes settle to disk during
  play, and F7 preserves the current durable image instead of rewinding
  settings or calibration from an older runtime snapshot.
- Do not inject the Bandai wrist-reflector IRQ into newly recognized EPOCH
  titles. Their controller protocol remains unknown and is kept isolated from
  the already verified Bandai motion profiles.
- Honor XaviX 2 display mode `$08` as a 640x480 startup surface. The BANDAI
  and XaviX logos now use the complete two-sprite frame instead of showing
  only its upper-left 320x240 quarter; normal gameplay remains 320x240.
- Restore XaviX 2 indexed-palette transparency and premultiplied half-alpha
  blending. Blue Dragon's result screen now retains its translucent blue and
  orange record panels instead of discarding their high-bit palette colors.
- Add F9/F10 native-resolution AVI recording with 60 FPS MJPEG video and
  synchronized 48 kHz stereo PCM audio. Runtime timing diagnostics move to F11.
- Restore Naruto's title and menu hit testing by keeping XaviX 2 opcodes
  `$f6/$f7` on the zero flag used by the firmware's immediate equality
  branches. The host cursor and click signal were still moving, but the
  regressed flag mapping made every on-screen target report a miss.
- Correct XaviX2 game and music-event timing from half speed to full speed by
  separating the 120 Hz timer interrupt from 60 Hz vblank.  The CPU and PCM
  clocks remain unchanged, preserving sample pitch; version-1 F5 states migrate
  their timer phase when loaded.
- Remove Naruto's blue host diagnostic target. The game-owned cursor now
  remains the only visible pointer while optical hit testing stays active.
- Decode the XaviX 2 note-release form of the live voice command. Released
  voices now decay cleanly before their active bit clears instead of retaining
  a sustain loop until channel reuse; the channel menu identifies voices in
  this transitional state.

### Added

- Add independent F5/F7 runtime states for all four recognized XaviX 2 games.
  State files preserve CPU, GPU, audio, EEPROM, timing, and controller hardware
  while safely rebinding the current ROM and host callbacks after loading.
- Add a live XaviX 2 channel diagnostic menu. Each of the 64 channels shows its
  current source rate, stereo volume, and loop state and can be muted without
  pausing it or changing the status observed by the game.
- Make XaviX 2 frame resume robust when an IRQ service crosses a vertical-blank
  boundary immediately before an F5 capture, avoiding an unsigned cycle-budget
  underflow on the first frame after F7.
- Restore DB2J/DBZ optical cursors and per-title calibration, allowing the
  motion-game route to traverse Shenron/Kame House pages, mission selection,
  and its first battle, while DBZ enters a real battle without a firmware
  bypass. Add separate DBZ host actions for basic attack, the two-hand input,
  and the horizontal deflect sweep.
- Render XaviX 2 Type-1 Gouraud polygons and preserve opaque black texels
  separately from transparent palette entries.
- Model the traced XaviX 2 matrix, packed-vertex, projection/culling, and
  dual-GPU submission paths. This makes the Dragon Ball battle polygon lists
  non-empty and establishes the observed sky/polygon/HUD ordering without a
  title- or frame-specific graphics patch; material/light stages remain open.
- Decode the full XaviX 2 six-bit Q2.4 GPU scale fields. This removes Blue
  Dragon's tiled-menu seams, restores its downscaled battle characters, and
  preserves Dragon Ball Z's continuous zoom effects.
- Route IRQ-10 motion packets to the verified per-title low-RAM producer
  buffers for DB2J and DBZ instead of always using Naruto's address.
- Keep Blue Dragon's two synthetic reflector samples distinguishable for its
  firmware-driven confirm gesture without changing Naruto's joined-hands
  behavior.

- Recognize the exact `ban_bldj`, `ban_db2j`, and `ban_dbz` XaviX 2 images.
- Recognize the exact `ttv_mx`, `tom_jump`, and `epo_sdb` XaviX 2000
  images, including their verified controller and persistent-storage paths.
- Recognize the exact 2 MiB `epo_bowl` image with mirrored external-ROM
  mapping, a dedicated 24C04 plus synthetic sensor profile, and independent
  EEPROM/runtime-state files.
- Recognize the exact 4 MiB `tak_chq` image through the generic XaviX 2000
  24C04 profile with independent EEPROM/runtime-state files.
- Recognize the exact 4 MiB `epo_ebox` image with digital controls, isolated
  ANPORT/ADC inputs, independent 4 KiB parallel NVRAM/runtime-state files, and
  verified title, character, level-selection, and PCM milestones.
- Recognize the exact 4 MiB `epo_es2j` image through a plain XaviX 2000
  profile with isolated P1/ANPORT/ADC behavior, independent runtime states,
  and verified title, game-select, attract-mode football, and PCM milestones.
- Recognize the exact 4 MiB `epo_hamc` image with a dedicated no-EEPROM
  synchronization profile, independent runtime states, and verified EPOCH,
  animated-title, and PCM milestones.
- Recognize the exact 4 MiB `tom_dpgm` image with a dedicated 24C08 sensor
  profile, independent EEPROM/runtime states, and verified Disney-logo,
  heart/wand tutorial, and PCM milestones.
- Recognize the exact 4 MiB `epo_mini` image through the 24C08 sensor profile,
  with independent EEPROM/runtime states and verified safety-warning,
  animated-title, EEPROM-initialization, and PCM milestones without a timer
  bypass.
- Recognize the exact 4 MiB `tvpc_ham` and `tvpc_hk` images through the shared
  TV-PC 24C16 profile, with verified title/main-menu, EEPROM-initialization,
  and PCM milestones.
- Add independent 2 KiB EEPROM and runtime-state identities for `tvpc_ham`
  and `tvpc_hk`, plus ROM-independent TV-PC diagnostic probes. The existing
  `tvpc_dor` host controls remain scoped to that title.
- Add ROM-independent tests for separate instruction fetching and interrupt
  acknowledgement/delivery.

### Fixed

- Record F9/F10 MJPEG AVI output through a fixed presentation surface selected
  when recording starts. Fullscreen and enlarged windows can now record at the
  displayed viewport size, guest 640x480-to-320x240 mode changes are rescaled
  instead of leaving a quarter-size image over black, and the presented host
  cursor is included. A View-menu checkbox retains native-size recording.
- Implement the XaviX 2 command `$07` signed-vector transform so DBZ supplies
  its firmware-rotated directional light instead of an all-zero vector. Keep
  command `$0b` color modulation disabled: the two plausible signed-dot
  interpretations each created different black terrain faces, so guest RGB555
  colors are preserved until the hardware clamp and rounding are established.
- Recompute Type-0 polygon `Bw/Cw` perspective weights from command `$4d`'s
  transformed vertex depths, and honor the texture record's Filter bit with
  four-tap bilinear sampling. Dragon Ball terrain textures now change with the
  projected geometry instead of retaining stale staging weights.
- Keep XaviX 2 command `$0f` normal results in the traced three-byte XYZ
  allocation. Expanding each result to four or twelve bytes overwrote DBZ's
  adjacent material-remap table, turning valid terrain attributes into random
  texture indices; the Namek and canyon checkpoints now retain firmware-owned
  materials while textured-polygon command `$0e` lighting remains incomplete.
- Update XaviX 2 negative/zero flags when firmware moves a multiply/divide
  result back to a general register.  Naruto's second-stage angle normalizer
  no longer inherits an older comparison flag and adds a false half-turn, so
  the enemy follows a continuous on-screen approach instead of briefly
  flashing at incorrect off-screen positions.
- Decode the XaviX 2 B4/B5 64-bit multiply as signed.  Naruto feeds this form
  negative sine-table values; preserving their sign lets second-stage enemies
  finish each trajectory and naturally advance through changing actions,
  distances, attacks, and replacement objects instead of freezing on one pass.
- Implement the XaviX 2 geometry unit's unsigned square-root command used by
  Naruto for cursor/target distance checks.  A resumed second-stage checkpoint
  now renders its submitted enemy and rotating projectile sprites while attack
  hit testing receives the real distance instead of a stale register value.
- Implement the XaviX 2 indexed large-object projection command used by DB2J.
  Its Q16.16 distance now selects a normal sprite scale, so the firmware-owned
  battle enemy remains visible, changes poses and distance, and receives
  aligned hit effects instead of becoming an invisible collision target or a
  maximum-size body fragment.
- Sort equal-depth XaviX 2 sprite backings by their covered area before smaller
  overlays, then retain firmware order for equal-sized objects. This restores
  both DBZ's XaviX logo and DB2J's tiled dialogue panel instead of fixing one
  by letting the other game's full-screen background cover it. Reopen the
  depth-FF background pass for each empty-GPU0 submission pair without
  covering a later polygon pass.
- Clip XaviX 2's host polygon raster loop to the guest-selected visible
  320x240 or 640x480 window. DB2J's polygon-heavy battle checkpoint keeps the
  exact same output hash while the 300-frame probe improves from roughly 62
  to 123 FPS, removing the avoidable off-screen work that caused GUI stalls.
  The later traced command `$07` signed-vector form retains DBZ's green
  decision target while supplying the battle light-vector registers.
- Restart XaviX 2 looping notes at their primary waveform address. Treating the
  secondary descriptor boundary as a sustain target selected a tiny adjacent
  fragment and produced a timbre absent from the isolated hardware reference.
- Pace the XaviX 2 CPU from the firmware-observed 98,437,488 Hz system source
  instead of the early rounded 98 MHz estimate, keeping event timing aligned
  with the already verified sound-divider clock.
- Decode XaviX 2 opcodes `$06/$07` with their signed 19-bit immediate. The old
  wider sign field pinned both Dragon Ball optical cursors outside menu bounds.
- Follow the guest-selected XaviX 2 visible origin, including the Dragon Ball
  gameplay crop, and distinguish Map-0/Map-1 polygon texture folding.
- Separate the XaviX 2 low-address instruction and data RAM images populated
  by DMA. This lets the two Dragon Ball images complete their destructive RAM
  tests and reach their title/menu screens without a game-specific bypass.
- Keep an accepted interrupt source visible as status while suppressing its
  immediate redelivery, allowing a newly raised DMA interrupt to wake WAIT.

## 0.3.0-alpha - 2026-08-11

### Fixed

- Treat an idle US Star Wars saber as absent from the camera and ordinary
  motion as a one-pixel, one-frame edge sample instead of a stationary 3-by-3
  reflection, so mouse movement no longer becomes a held defense.
- Send Ham-chans controller shakes as finite pulses and expose the firmware's
  separate menu-confirm input on Enter and middle mouse.
- Replace TV-PC ROM mirroring at the external keyboard scan addresses with an
  active-high eight-row keyboard device.

### Added

- Decode the two Ham-chans wireless bell-controller packets and expose them as
  left mouse, right mouse, and Space-for-both host controls.
- Add TV-PC cursor-key mappings for arrow keys/WASD and short Up/Down pulses
  from vertical mouse movement.

## 0.2.0-alpha - 2026-08-10

### Fixed

- Preserve XaviX 2 command-list submission order when GPU objects have equal
  priority, restoring the hidden `はじめから` and `つづきから` menu layers in
  `ban_naru`.
- Loop XaviX 2 PCM voices to their primary sample address instead of treating
  the descriptor's end pointer as a loop address, preventing playback from
  continuing into unrelated ROM data.
- Restore the firmware-derived Q16 XaviX 2 PCM pitch conversion, apply live
  `$c0` pitch/volume commands, and detect terminators crossed by high-rate
  voices.  A spectrum comparison against a short real-hardware reference
  confirms a 1.00 pitch ratio for Q16; the experimental Q15 conversion was
  both too fast and at the wrong frequency.
- Preserve one accumulator guard bit before XaviX 2 PCM output conversion,
  preventing ordinary polyphonic passages from hard-clipping while retaining
  the firmware-derived pitch and relative channel volumes.
- Show the blue host target in LOTR and Star Wars scenes that do not provide a
  game-owned cursor, while suppressing it when the verified cursor sprites are
  visible.

### Changed

- Promote `ban_omt`, `ttv_lotr`, `ttv_sw`, and `ttv_swj` to Playable based on
  direct maintainer gameplay verification; synthetic optical limitations
  remain documented.

### Added

- Opt-in XaviX 2 WAV capture and per-frame hit/audio diagnostics in the
  ROM-dependent boot probe.
- Add an F11 runtime timing display with FPS, guest CPU rate, dropped frames,
  and audio delivery counters for diagnosing host-dependent slowdown.
- Model LOTR right-mouse defense as a stationary upright broad reflection, and
  map held Space to the rotating elongated-reflection gesture accepted by the
  Star Wars lightsaber spin tutorial.

## 0.1.0-alpha - 2026-08-10

Initial public source release, imported from the real pre-Git local
development snapshot.

### Added

- Compact XaviX 2000 CPU, bus, video, audio, timer, DMA, and mathematics paths.
- 24C02, 24C04, and 24C08 EEPROM models.
- Synthetic CU5501/CU5501A-family optical input profiles for seven identified
  game images.
- Playable Dragon Quest and One Piece profiles with mouse controls.
- Experimental Onmyou Taisenki, LOTR, Star Wars US/Japan, and XaviX 2 Naruto
  profiles.
- Per-game XaviX 2000 EEPROM and portable runtime-state storage.
- Native Win32 front end with bilingual menus, sharp scaling, 4:3 presentation,
  maximized mode, fullscreen, local screenshots, and WinMM audio.
- ROM-independent automated tests and optional ROM-dependent diagnostic probes.
- Research notes documenting observed blockers and experiments.

### Known limitations

- Several titles have only early menu milestones and no complete play-through.
- Optical motion is synthesized rather than cycle-accurate.
- XaviX 2 CPU, GPU, sound, gesture, EEPROM, and state support remain incomplete.
- Windows is the only supported host platform.
