# Early XaviX support investigation

ROMs under test are user-supplied and opened read-only. They are not included
in the source tree, builds, or research notes.

## `epo_hamd`

- Observed symptom: the two-chip set was not recognized by the standalone
  loader.
- Address / interface: U2 maps at `0x000000`; U3 maps at `0x400000` in a
  zero-filled 8 MiB ROM space. The firmware uses the baseline XaviX machine
  path rather than an XaviX 2000 optical profile.
- Hypothesis: the existing CPU, video, and audio path can execute the title if
  the physical ROM layout is assembled exactly and no unrelated optical or
  EEPROM device is attached.
- Experiment: verify each chip independently by size, CRC32, and SHA-1; build
  the sparse image in memory; run 1,200 frames using a baseline XaviX profile.
- Result: the animated title remains active and the CPU continues executing.
  Pulsing each ordinary IN0 bit after the title did not start gameplay, so the
  original dance/run controller is not represented as a simple button bank.
- Controller trace: I/O event 0 starts a firmware-installed receiver at RAM
  `$1673`. It samples eight MSB-first bits from IN1 bit 0, validates an odd
  packet, and separates the two receivers into `$9e/$9f`. Repeated packet
  `$15` decodes to the left-hand state at `$a4`; repeated packet `$13` decodes
  to the right-hand state at `$a5`.
- Hardware reference: Epoch's contemporary product description, reported by
  ASCII.jp, identifies two bell-shaped controllers whose vibration sensors
  transmit shake strength to the console over infrared:
  <https://ascii.jp/elem/000/000/327/327156/>.
- Input experiment: send both packets through the actual I/O-event IRQ and
  serial sampling path once per host frame. The title's `りょうてのボタンを
  おしてね!` check accepts the pair and advances to the activity menu.
- Menu experiment: continuously repeating either shake packet makes the menu
  selection oscillate and cannot confirm it. Short four-frame shake bursts
  remove that repetition. At the supplied activity-menu checkpoint, an IN0
  `0x01` pulse enters the current option; this is exposed separately on Enter
  and middle mouse rather than disguised as another wireless packet.
- Code changed: add a strict two-chip loader, a baseline XaviX profile, a
  focused I/O-event trigger, finite left/right shake streams, the digital menu
  confirmation, host mappings, and a unique runtime-state filename. No guest
  RAM or result address is patched.

## `tvpc_dor`

- Observed symptom: the firmware displayed the XaviX and EPOCH logos, then
  entered a live RAM idle loop near PC `$003eaf-$003ec0` while the screen
  remained black.
- Address / interface: MAME identifies the machine as
  `xavix_i2c_24c16_4mb`; I2C is carried on P1 and requires a 24C16 EEPROM with
  2,048 bytes, control-byte address bits 8-10, and a 16-byte page.
- Hypothesis: the prior 24C08-sized model acknowledged an incompatible address
  range, preventing the firmware's first-time EEPROM initialization from
  completing correctly.
- Experiment: implement 24C16 control-byte addressing and 2 KiB persistence,
  then repeat the deterministic frame probe without changing CPU opcodes,
  timing, video, or firmware flow.
- Result: EEPROM initialization produced multiple committed writes and the
  title/main-menu screen appeared by frames 600-900. This identifies the
  24C16 model as the first blocking hardware correction.
- Input trace: the firmware reads IN0 at `$7a00`, then reads counters at
  `$7b10` and `$7b11` near PC `$00a70a-$00a730`. IN0 bit `0x80` is the mouse
  button. Controlled counter changes showed that X increases with positive X
  counts, while Y increases with negative Y counts. The firmware integrates
  these values into its own cursor position rather than accepting absolute
  screen coordinates.
- Input experiment: initialize the guest cursor to its centre and supply
  counters `(1,0)`, `(ff,0)`, `(0,1)`, and `(0,ff)`. The corresponding guest
  positions changed by right, left, up, and down respectively. Repeating a
  non-zero counter moved the game-owned white cursor continuously.
- Keyboard trace: the firmware reads eight active-high rows from external
  addresses `$600001`, `$600002`, `$600004`, ... `$600080` near PC
  `$0280cc-$028104`. Treating this address window as mirrored ROM produced
  false key states (`a0/80/a0/a0`) and prevented all real keyboard input.
- Keyboard experiment: return zero for an idle matrix and sweep individual
  row bits from the supplied take-copter checkpoint. Row 1 bits `0x01` and
  `0x02` produce the two cursor-key poses; alternating them keeps Nobita in
  flight while the no-input control reaches the `ざんねん` result.
- Code changed: add the 24C16 line interface, state-format migration for the
  larger shared EEPROM storage, a dedicated XaviX+24C16 profile, strict ROM
  recognition, per-game EEPROM/runtime-state files, and a host mouse mapping
  that maintains the same cumulative counters and IN0 button bit. Add the
  external keyboard rows, arrow/WASD mappings, and vertical-mouse key pulses.
  The blue host target is suppressed because the game renders its own cursor.

Most TV-PC key labels and the complete gameplay flow remain unknown. The
verified flight checkpoint is an Experimental milestone, not a complete-play
or Playable claim.

## `tvpc_ham` and `tvpc_hk`

- Observed symptom: neither exact 4 MiB image was recognized by the standalone
  loader, so the existing shared TV-PC hardware path could not be evaluated.
- Address / interface: both MAME definitions use the same
  `xavix_i2c_24c16_4mb` machine configuration and `tvpc_tom` input definition
  as `tvpc_dor`. This indicates the same 2 KiB 24C16 board-level storage, but
  does not establish identical host controls or keyboard meanings.
- Hypothesis: strict image recognition plus the existing XaviX+24C16 profile
  is sufficient to reach an initial visual milestone without copying the
  Doraemon-specific host mappings.
- Experiment: verify each image by size, CRC32, and SHA-1, then run deterministic
  600-, 1,800-, and 3,600-frame probes with no guest patches. Track CPU
  execution, frame hashes, EEPROM commits, PCM samples, ANPORT reads, and the
  external keyboard window.
- Result: both titles display stable, correctly composed title/main-menu
  screens and remain live through frame 3,600. `tvpc_ham` ends at PC `$003eb1`
  with frame hash `4a9385e9c754e984`; `tvpc_hk` ends at PC `$00224f` with
  frame hash `a709ae6ce2ec487b`. Both initialize 48 EEPROM bytes across 11
  committed generations and produce nonzero PCM output.
- I/O result: both read motion counters at `$7b10/$7b11`, the ADC path at
  `$7b80`, and the external one-hot keyboard addresses `$600001-$600080`.
  The observed `$7b80` value is `ff`; no hardware change is justified by that
  observation because neither title blocks before the verified menu.
- Keyboard identification: Epoch's official model 37200 manual documents the
  printed QWERTY/kana layout, two cursor-key clusters, input-mode key, and mail
  shortcut: <https://epoch.jp/assets/pdf/support/manuals/37200_TORISETU.pdf>.
- Matrix experiment: enter Hello Kitty's mailbox name editor, branch from one
  runtime state, and assert each of the 64 raw matrix positions independently.
  The displayed kana identifies every printable key; separate screens verify
  Escape, Mail, Backspace, Shift, Enter, Space, and Input Mode.
- Mouse result: cumulative X/Y counter changes and IN0 bit `0x80` produce the
  matching game-owned bow-cursor motion and its single-button click path.
- Code changed: retain the shared 24C16 board profile and independent save
  identities, then enable the cumulative mouse counters, IN0 mouse button,
  external keyboard rows, and host-target suppression for `tvpc_hk`. The
  Doraemon-only vertical-mouse flight pulses remain scoped to `tvpc_dor`.
  `tvpc_ham` remains without host mappings until its printed controls are
  independently identified.

Hello Kitty's mouse and complete printed keyboard are now host-accessible, but
later programs, audio accuracy, and a complete play-through remain unverified.
Hamtaro's controls remain unknown. Both titles therefore retain an
**Experimental** compatibility classification.

## 2026-08-29: Hyper Rescue ADC completion status

- Symptom: `tomthr` displayed the XaviX logo, blanked the display, and then stayed
  alive indefinitely at external PC `$91ffd7` without reaching its warning or
  title screens. Horn, ignition, wiper, map, and combined button states did not
  change the wait.
- Firmware evidence: the loop at ROM `$11ffd0` writes command `$40` to ADC
  control `$7b81`, then repeatedly reads `$7b81` and tests completion bit `$80`
  before accumulating the sample from `$7b80`.
- Root cause: XaviXEmu completed each ADC conversion synchronously but returned
  zero from the control/status read, so the firmware could never observe
  completion.
- Change: retain the requested channel/control bits and return bit `$80` set
  once the synchronous result is latched. A machine-level regression test now
  verifies command `$40` reads back as status `$c0`.
- Result: frame 600 renders the Japanese safety warning, frame 1,500 renders
  the Hyper Rescue title, and pulsing the real horn/select input bit `$10`
  enters the mode-select screen by frame 2,100. The doubled CPU clock and
  base-rate video/audio timing remain unchanged.
- Regression: all 31 locally present non-XaviX2 ROMs completed a fresh 600-frame
  run with `stopped=0` after the change.

## 2026-08-29: Duel Masters base-station boundary

- The exact `duelmast` ZIP contains the 2 MiB Duel Station base-station BIOS.
  It reaches and renders the scanner-gate diagnostic, polls the three cabinet
  buttons and directions, and remains live.
- The original machine exposes a separate external cartridge bus. No Duel
  Masters game cartridge image is present in the local ROM set, so passing the
  diagnostic cannot provide the missing title/game program. The BIOS is kept
  explicitly classified Not working rather than silently patching around or
  inventing cartridge contents.
