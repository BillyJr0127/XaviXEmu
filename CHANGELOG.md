# Changelog

All notable public changes will be documented in this file. The project uses
semantic versioning while experimental releases carry a pre-release suffix.

## Unreleased

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
- Show the blue host target in LOTR and Star Wars scenes that do not provide a
  game-owned cursor, while suppressing it when the verified cursor sprites are
  visible.

### Changed

- Promote `ban_omt`, `ttv_lotr`, `ttv_sw`, and `ttv_swj` to Playable based on
  direct user gameplay verification; synthetic optical limitations remain
  documented.

### Added

- Opt-in XaviX 2 WAV capture and per-frame hit/audio diagnostics in the
  ROM-dependent boot probe.
- Map Space to LOTR's evidenced broad vertical Fire gesture and, while held,
  to the rotating elongated-reflection gesture accepted by the Star Wars
  lightsaber spin tutorial.

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
