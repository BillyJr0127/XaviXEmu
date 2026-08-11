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
