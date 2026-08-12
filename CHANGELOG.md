# Changelog

All notable public changes will be documented in this file. The project uses
semantic versioning while experimental releases carry a pre-release suffix.

## Unreleased

### Added

- Recognize the exact `ban_bldj`, `ban_db2j`, and `ban_dbz` XaviX 2 images.
- Add ROM-independent tests for separate instruction fetching and interrupt
  acknowledgement/delivery.

### Fixed

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
- Add an F10 runtime timing display with FPS, guest CPU rate, dropped frames,
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
